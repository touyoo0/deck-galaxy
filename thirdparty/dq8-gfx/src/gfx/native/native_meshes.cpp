// The native pipeline's mesh draws (Rogue Galaxy: added; the game's
// docs/NATIVE-PIPELINE.md steps 4b and 5a): the frame's MeshDraw records,
// in order, into the targets they name. Depth targets are float, reversed
// (cleared to 0 at their first use in a frame, nearer is greater, as the
// game compares). Nothing of the GS here.

#include "gfx/native/native_pipeline.h"

#include "native_mesh.frag.spv.h"
#include "native_mesh.frag.nodiscard.spv.h"
#include "native_mesh.vert.spv.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace dq8::gfx::native
{
namespace
{
// A vertex as the mesh shader takes it.
struct GpuVertex
{
    float position[3];
    float uv[2];
    float color[4];
    float fog;
};
static_assert(sizeof(GpuVertex) == 40u, "tightly packed");
static_assert(sizeof(Vertex) == sizeof(GpuVertex) && offsetof(Vertex, position) == offsetof(GpuVertex, position) &&
                  offsetof(Vertex, uv) == offsetof(GpuVertex, uv) &&
                  offsetof(Vertex, color) == offsetof(GpuVertex, color) &&
                  offsetof(Vertex, fog) == offsetof(GpuVertex, fog),
              "Vertex must match GpuVertex for direct upload");

struct DrawUniforms
{
    float clip[16];
    float viewport[16];
    float target[4];
    float fog[4];
    float depthRow[4];   // reversed depth (w - z) / 2 straight from the position
    float depthLimit[4]; // x: the program's depthMax as this depth (times w); y: 1 when it applies
};

struct MaterialUniforms
{
    uint32_t mode[4];
    uint32_t test[4];
    float size[4];
    float blend[4];
    float fogColor[4];
    float texa[4];
    uint32_t region[4]; // min u, max u, min v, max v (the GS's CLAMP)
    uint32_t wrap[4];   // u, v: 0 repeat, 1 clamp, 2 region clamp, 3 region repeat
};

// a x vp in double (vp = view x projection, precomputed per camera), rounded once:
// the clip matrix of a model under a camera, and the reversed-depth row taken
// from it before rounding.
// (w - z) / 2 computed in the shader from a float matrix loses most of its
// bits to cancellation (w and z are nearly equal over most of the scene),
// and two meshes on one surface (a wall and the light laid on it, terrain
// layers) then got different depths and fought.
//
// The program's depthScale S multiplies the window depth Z = V22 z/w + V32
// (V the camera's viewport). With d = (1 - z/w) / 2 that is
// d' = S d + (1 - S)(1 + V32 / V22) / 2, the constant times w folded into
// the row. Without it a decal (S = 1.01) tied the surface it lies on and
// lost about half its pixels each frame where the GS has it hundreds of
// units in front.
void clipAndDepth(const float a[16], const double vp[16], float clip[16], float depthRow[4],
                  double depthScale, double depthConstant)
{
    double abc[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k)
                sum += double(a[i * 4 + k]) * vp[k * 4 + j];
            abc[i * 4 + j] = sum;
        }
    for (int i = 0; i < 16; ++i)
        clip[i] = float(abc[i]);
    // Row vectors: clip_j = sum_i p_i M[i][j]; w is column 3, z column 2.
    for (int i = 0; i < 4; ++i)
        depthRow[i] = float(depthScale * 0.5 * (abc[i * 4 + 3] - abc[i * 4 + 2]) + depthConstant * abc[i * 4 + 3]);
}

void windowModelClipAndDepth(const float a[16], double zScale, double zBias, float clip[16], float depthRow[4],
                             double depthScale, double depthConstant)
{
    for (int i = 0; i < 4; ++i)
    {
        const double m0 = double(a[i * 4 + 0]);
        const double m1 = double(a[i * 4 + 1]);
        const double m2 = double(a[i * 4 + 2]);
        const double m3 = double(a[i * 4 + 3]);
        const double z = m2 * zScale + m3 * zBias;
        clip[i * 4 + 0] = float(m0);
        clip[i * 4 + 1] = float(m1);
        clip[i * 4 + 2] = float(z);
        clip[i * 4 + 3] = float(m3);
        depthRow[i] = float(depthScale * 0.5 * (m3 - z) + depthConstant * m3);
    }
}

// The pipeline's state as one key: depth compare (3 bits), depth write (1),
// depth used at all (1), color write mask (4), a depth target attached to
// the pass (1), blending (1), its source and destination factors (3 each),
// op (2), and no-discard Early-Z shader variant (1).
uint32_t pipelineKey(const Material &m, bool attached)
{
    const bool depth = m.depthTest != Compare::Always || m.depthWrite;
    const bool noDiscard =
        m.destinationAlphaTest < 0 && (m.alphaTest == Compare::Always || m.alphaFail != AlphaFail::Nothing);
    uint32_t key = static_cast<uint32_t>(m.depthTest) | (m.depthWrite ? 1u << 3 : 0u) | (depth ? 1u << 4 : 0u) |
                   (static_cast<uint32_t>(m.colorWrite & 0xFu) << 5) | (attached ? 1u << 9 : 0u) |
                   (noDiscard ? 1u << 19 : 0u);
    if (m.blending)
        key |= (1u << 10) | (static_cast<uint32_t>(m.srcFactor) << 11) | (static_cast<uint32_t>(m.dstFactor) << 14) |
               (static_cast<uint32_t>(m.blendOp) << 17);
    return key;
}

SDL_GPUBlendFactor blendFactor(BlendFactor f)
{
    switch (f)
    {
    case BlendFactor::Zero: return SDL_GPU_BLENDFACTOR_ZERO;
    case BlendFactor::One: return SDL_GPU_BLENDFACTOR_ONE;
    case BlendFactor::SrcAlpha: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case BlendFactor::InvDstAlpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    case BlendFactor::Fixed: return SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
    case BlendFactor::InvFixed: return SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR;
    }
    return SDL_GPU_BLENDFACTOR_ONE;
}

SDL_GPUCompareOp compareOp(Compare c)
{
    switch (c)
    {
    case Compare::Always: return SDL_GPU_COMPAREOP_ALWAYS;
    case Compare::Never: return SDL_GPU_COMPAREOP_NEVER;
    case Compare::Less: return SDL_GPU_COMPAREOP_LESS;
    case Compare::LessEqual: return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    case Compare::Equal: return SDL_GPU_COMPAREOP_EQUAL;
    case Compare::GreaterEqual: return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
    case Compare::Greater: return SDL_GPU_COMPAREOP_GREATER;
    case Compare::NotEqual: return SDL_GPU_COMPAREOP_NOT_EQUAL;
    }
    return SDL_GPU_COMPAREOP_ALWAYS;
}

bool growBuffer(SDL_GPUDevice *device, SDL_GPUBuffer *&buffer, uint32_t &capacity, uint32_t needed,
                SDL_GPUBufferUsageFlags usage, std::string &error)
{
    if (buffer && capacity >= needed)
        return true;
    if (buffer)
        SDL_ReleaseGPUBuffer(device, buffer);
    uint32_t size = std::max<uint32_t>(capacity, 1u << 20);
    while (size < needed)
        size *= 2u;
    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size = size;
    buffer = SDL_CreateGPUBuffer(device, &info);
    capacity = buffer ? size : 0u;
    if (!buffer)
        error = std::string("SDL_CreateGPUBuffer(native meshes): ") + SDL_GetError();
    return buffer != nullptr;
}
} // namespace

bool Renderer::ensureMeshes(SDL_GPUDevice *device, std::string &error)
{
    if (m_meshVertex && m_meshFragment && m_meshFragmentOpaque)
        return true;
    if (m_meshFailed)
        return false;
    m_meshFailed = true; // until made
    if (!(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV))
    {
        error = "no SPIR-V: the native mesh shaders have no other build yet";
        return false;
    }
    SDL_GPUShaderCreateInfo vertexInfo{};
    vertexInfo.code = reinterpret_cast<const uint8_t *>(kNativeMeshVertSpirv);
    vertexInfo.code_size = sizeof(kNativeMeshVertSpirv);
    vertexInfo.entrypoint = "main";
    vertexInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexInfo.num_uniform_buffers = 1u;
    SDL_GPUShaderCreateInfo fragmentInfo{};
    fragmentInfo.code = reinterpret_cast<const uint8_t *>(kNativeMeshFragSpirv);
    fragmentInfo.code_size = sizeof(kNativeMeshFragSpirv);
    fragmentInfo.entrypoint = "main";
    fragmentInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragmentInfo.num_samplers = 2u;
    fragmentInfo.num_uniform_buffers = 1u;
    SDL_GPUShaderCreateInfo opaqueInfo = fragmentInfo;
    opaqueInfo.code = reinterpret_cast<const uint8_t *>(kNativeMeshOpaqueFragSpirv);
    opaqueInfo.code_size = sizeof(kNativeMeshOpaqueFragSpirv);
    m_meshVertex = SDL_CreateGPUShader(device, &vertexInfo);
    m_meshFragment = m_meshVertex ? SDL_CreateGPUShader(device, &fragmentInfo) : nullptr;
    m_meshFragmentOpaque = m_meshFragment ? SDL_CreateGPUShader(device, &opaqueInfo) : nullptr;
    if (!m_meshVertex || !m_meshFragment || !m_meshFragmentOpaque)
    {
        error = std::string("SDL_CreateGPUShader(native mesh): ") + SDL_GetError();
        return false;
    }
    m_depthFormat = SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                                 SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                        ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
                        : SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    m_depthCanSample = SDL_GPUTextureSupportsFormat(
        device, m_depthFormat, SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
    // A white texel for untextured draws: the shader ignores it, but a
    // sampler binding must be made.
    m_white = createTexture(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 1u, 1u, 1u, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                            error);
    if (!m_white)
        return false;
    static const Bytes white = std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{255, 255, 255, 255});
    m_uploads.push_back({m_white, 1u, 1u, white, false});
    m_meshFailed = false;
    return true;
}

SDL_GPUTexture *Renderer::targetTexture(SDL_GPUDevice *device, Id id, bool depth, uint32_t minW, uint32_t minH,
                                        uint32_t targetScale, bool &fresh, std::string &error)
{
    GpuTarget &t = m_gpuTargets[id];
    fresh = false;
    if (t.texture && t.width >= minW && t.height >= minH && (id == kFeedbackCopy || t.scale == targetScale))
        return t.texture;
    const uint32_t width = (id == kFeedbackCopy || t.scale == targetScale) ? std::max(t.width, minW) : minW;
    const uint32_t height = (id == kFeedbackCopy || t.scale == targetScale) ? std::max(t.height, minH) : minH;
    if (t.texture)
        SDL_ReleaseGPUTexture(device, t.texture);
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = depth ? m_depthFormat : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = depth ? (m_depthCanSample ? (SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)
                                           : SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                       : (SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    t.texture = SDL_CreateGPUTexture(device, &info);
    t.width = t.texture ? width : 0u;
    t.height = t.texture ? height : 0u;
    t.scale = t.texture ? targetScale : 0u;
    if (!t.texture)
        error = std::string("SDL_CreateGPUTexture(native target): ") + SDL_GetError();
    fresh = t.texture != nullptr;
    return t.texture;
}

SDL_GPUSampler *Renderer::meshSampler(SDL_GPUDevice *device, uint32_t key, bool remaster)
{
    SDL_GPUSampler *&sampler = remaster ? m_remasterSamplers[key & 7u] : m_meshSamplers[key & 7u];
    if (sampler)
        return sampler;
    SDL_GPUSamplerCreateInfo info{};
    const bool linear = key & 1u;
    if (remaster)
    {
        info.min_filter = SDL_GPU_FILTER_LINEAR;
        info.mag_filter = linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        info.enable_anisotropy = true;
        info.max_anisotropy = 16.0f;
        info.mip_lod_bias = -0.35f;
        info.min_lod = 0.0f;
        info.max_lod = 1000.0f;
    }
    else
    {
        info.min_filter = linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        info.mag_filter = info.min_filter;
        info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        info.min_lod = 0.0f;
        info.max_lod = 0.0f;
    }
    info.address_mode_u = (key & 2u) ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_v = (key & 4u) ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler = SDL_CreateGPUSampler(device, &info);
    return sampler;
}

void Renderer::prepareRemasterTextures(SDL_GPUDevice *device, SDL_GPUCommandBuffer *commands)
{
    if (!remasterEnabled())
        return;
    if (!m_bc7Checked)
    {
        m_bc7Checked = true;
        m_bc7Supported = SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM,
                                                      SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER);
        m_aiCache.init(m_bc7Supported);
    }
    pollAiCache(device, commands);

    struct PendingUpscale
    {
        SDL_GPUTexture *src = nullptr;
        SDL_GPUTexture *dst = nullptr;
        uint32_t srcW = 0, srcH = 0;
        uint32_t dstW = 0, dstH = 0;
        bool generateMips = false;
        bool clampU = false, clampV = false;
        bool releaseSrc = false;
        bool useCnn = false;
        bool is2d = false;
    };
    std::vector<PendingUpscale> upscaleJobs;
    std::string xbrErr, cnnErr;
    const bool xbrReady = ensureXbr(device, xbrErr);
    const bool cnnReady = ensureCnn(device, cnnErr);
    bool added = false;
    uint64_t lastCheckedKey = 0u;
    bool lastCheckedIs2d = false;
    for (const MeshDraw &d : m_frameMeshes)
    {
        const Material &m = d.material;
        if (d.indexCount == 0u || m.sourceTarget != 0u || m.texture == 0u || m.destinationAlphaTest >= 0)
            continue;
        const bool is2d = !d.hasModel;
        const uint64_t fastKey = (uint64_t(m.texture) << 32u) | uint64_t(m.palette);
        if (fastKey == lastCheckedKey && is2d == lastCheckedIs2d)
            continue;
        lastCheckedKey = fastKey;
        lastCheckedIs2d = is2d;
        auto it = m_images.find(m.texture);
        if (it == m_images.end())
            continue;
        Image &img = it->second;
        const bool clampU = m.clampU || m.wrapU >= 2u;
        const bool clampV = m.clampV || m.wrapV >= 2u;
        const bool isPow2 = (img.width & (img.width - 1u)) == 0u && (img.height & (img.height - 1u)) == 0u;
        // Skip large 2D RGBA8 uploads (>256px, e.g. 512x512 FMV video frames) so transient movie frames never churn m_resolved.
        // Cap 2D Index8 2xBR at 512x512 (1024x1024 output = 4 MiB max) so 1024px sheets don't blow the 256 MiB cache budget.
        const bool sizeOk =
            is2d ? (img.format == PixelFormat::Rgba8
                        ? (isPow2 && img.width >= 8u && img.height >= 8u && img.width <= 256u && img.height <= 256u)
                        : (img.width >= 8u && img.height >= 8u && img.width <= 512u && img.height <= 512u))
                 : (img.width >= 16u && img.height >= 16u && img.width <= 512u && img.height <= 512u);

        if (img.format == PixelFormat::Rgba8)
        {
            const bool canUpscale = sizeOk && (is2d ? xbrReady : (cnnReady || xbrReady));
            if (!canUpscale)
                continue;
            const uint64_t key = uint64_t(m.texture) << 32u;
            if (m_resolved.find(key) != m_resolved.end())
                continue;
            const bool useCnn = !is2d && cnnReady;
            const uint32_t outW = img.width * 2u, outH = img.height * 2u;
            uint32_t dim = std::max(outW, outH), levels = 1u;
            while (dim > 1u) { dim >>= 1u; ++levels; }
            std::string err;
            SDL_GPUTexture *dstTex = createTexture(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, outW, outH, levels,
                                                   SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
                                                   err);
            if (!dstTex)
                continue;
            upscaleJobs.push_back(
                {img.texture, dstTex, img.width, img.height, outW, outH, levels > 1u, clampU, clampV, false, useCnn, is2d});
            const uint64_t bytes = uint64_t(outW) * outH * 4u;
            m_resolved[key] = Image{dstTex, PixelFormat::Rgba8, outW, outH, 0u, bytes, nullptr};
            m_resolvedOrder.push_back(key);
            m_resolvedBytes += bytes;
            if (is2d)
                ++m_resolved2dCount;
            if (m_aiCache.enabled() && img.cpuBytes)
            {
                const uint64_t hash = AiTextureCache::hashRgba(img.width, img.height, img.cpuBytes->data(),
                                                               size_t(img.width) * img.height * 4u);
                m_aiCache.request(key, hash, img.width, img.height, img.cpuBytes, !is2d);
                img.cpuBytes.reset();
            }
            added = true;
            continue;
        }
        if (img.format != PixelFormat::Index8 || !img.cpuBytes)
            continue;
        const Id palId = m.palette ? m.palette : img.palette;
        const auto p = m_palettes.find(palId);
        if (p == m_palettes.end() || !p->second.cpuBytes || p->second.width == 0u)
            continue;
        const uint64_t key = (uint64_t(m.texture) << 32u) | uint64_t(palId);
        if (m_resolved.find(key) != m_resolved.end())
            continue;
        const bool doUpscale = sizeOk && (is2d ? xbrReady : (cnnReady || xbrReady));
        const bool useCnn = doUpscale && !is2d && cnnReady;
        const Image &pal = p->second;
        const size_t pixelCount = size_t(img.width) * img.height;
        if (pixelCount == 0u || img.cpuBytes->size() < pixelCount || pal.cpuBytes->size() < size_t(pal.width) * 4u)
            continue;
        auto rgba = std::make_shared<std::vector<uint8_t>>(pixelCount * 4u);
        const uint8_t *src = img.cpuBytes->data();
        const uint8_t *palData = pal.cpuBytes->data();
        const uint32_t maxIdx = pal.width - 1u;
        uint8_t *dst = rgba->data();
        for (size_t k = 0; k < pixelCount; ++k)
        {
            const uint32_t idx = std::min<uint32_t>(src[k], maxIdx);
            std::memcpy(dst + k * 4u, palData + size_t(idx) * 4u, 4u);
        }
        const uint32_t outW = doUpscale ? img.width * 2u : img.width;
        const uint32_t outH = doUpscale ? img.height * 2u : img.height;
        uint32_t dim = std::max(outW, outH);
        uint32_t levels = 1u;
        while (dim > 1u)
        {
            dim >>= 1u;
            ++levels;
        }
        const SDL_GPUTextureUsageFlags dstUsage =
            (levels > 1u || doUpscale) ? (SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)
                                       : SDL_GPU_TEXTUREUSAGE_SAMPLER;
        std::string err;
        SDL_GPUTexture *dstTex = createTexture(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, outW, outH, levels,
                                               dstUsage, err);
        if (!dstTex)
            continue;
        if (doUpscale)
        {
            SDL_GPUTexture *srcTex = createTexture(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, img.width, img.height,
                                                   1u, SDL_GPU_TEXTUREUSAGE_SAMPLER, err);
            if (!srcTex)
            {
                SDL_ReleaseGPUTexture(device, dstTex);
                continue;
            }
            m_uploads.push_back({srcTex, img.width, img.height, rgba, false});
            upscaleJobs.push_back(
                {srcTex, dstTex, img.width, img.height, outW, outH, levels > 1u, clampU, clampV, true, useCnn, is2d});
        }
        else
        {
            m_uploads.push_back({dstTex, img.width, img.height, rgba, levels > 1u});
        }
        const uint64_t bytes = uint64_t(outW) * outH * 4u;
        m_resolved[key] = Image{dstTex, PixelFormat::Rgba8, outW, outH, palId, bytes, nullptr};
        m_resolvedOrder.push_back(key);
        m_resolvedBytes += bytes;
        if (is2d)
            ++m_resolved2dCount;
        if (m_aiCache.enabled() && sizeOk)
        {
            const uint64_t hash = AiTextureCache::hashRgba(img.width, img.height, rgba->data(), rgba->size());
            m_aiCache.request(key, hash, img.width, img.height, rgba, !is2d);
        }
        added = true;
    }
    if (added)
    {
        std::string failed;
        if (!m_uploads.empty() && !upload(device, commands, failed))
        {
            std::fprintf(stderr, "[native-pipeline] remaster texture upload failed: %s\n", failed.c_str());
            m_uploads.clear();
            for (const PendingUpscale &job : upscaleJobs)
                if (job.releaseSrc)
                    SDL_ReleaseGPUTexture(device, job.src);
            return;
        }
        for (const PendingUpscale &job : upscaleJobs)
        {
            if (job.useCnn)
            {
                runCnnUpscale(device, commands, job.src, job.dst, job.srcW, job.srcH, job.clampU, job.clampV);
                if (job.generateMips)
                    SDL_GenerateMipmapsForGPUTexture(commands, job.dst);
            }
            else
            {
                SDL_GPUColorTargetInfo target{};
                target.texture = job.dst;
                target.load_op = SDL_GPU_LOADOP_DONT_CARE;
                target.store_op = SDL_GPU_STOREOP_STORE;
                SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &target, 1u, nullptr);
                if (pass)
                {
                    const SDL_GPUViewport viewport{0.0f, 0.0f, float(job.dstW), float(job.dstH), 0.0f, 1.0f};
                    SDL_SetGPUViewport(pass, &viewport);
                    SDL_BindGPUGraphicsPipeline(pass, job.is2d ? m_xbr2dPipeline : m_xbrPipeline);
                    const SDL_GPUTextureSamplerBinding binding{job.src, m_casSampler};
                    SDL_BindGPUFragmentSamplers(pass, 0u, &binding, 1u);
                    const uint32_t wrap[4] = {job.clampU ? 1u : 0u, job.clampV ? 1u : 0u, 0u, 0u};
                    SDL_PushGPUFragmentUniformData(commands, 0u, wrap, sizeof(wrap));
                    SDL_DrawGPUPrimitives(pass, 3u, 1u, 0u, 0u);
                    SDL_EndGPURenderPass(pass);
                    if (job.generateMips)
                        SDL_GenerateMipmapsForGPUTexture(commands, job.dst);
                }
            }
            if (job.releaseSrc)
                SDL_ReleaseGPUTexture(device, job.src);
        }
        evictResolved(device);
    }
}

SDL_GPUGraphicsPipeline *Renderer::meshPipeline(SDL_GPUDevice *device, uint32_t key)
{
    if (const auto it = m_meshPipelines.find(key); it != m_meshPipelines.end())
        return it->second;
    const Compare depthTest = static_cast<Compare>(key & 7u);
    const bool depthWrite = (key >> 3) & 1u;
    const bool attached = (key >> 9) & 1u;
    const bool depth = ((key >> 4) & 1u) && attached;

    SDL_GPUColorTargetDescription color{};
    color.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUColorTargetBlendState &b = color.blend_state;
    b.enable_color_write_mask = true;
    b.color_write_mask = static_cast<SDL_GPUColorComponentFlags>((key >> 5) & 0xFu);
    b.enable_blend = (key >> 10) & 1u;
    b.src_color_blendfactor = blendFactor(static_cast<BlendFactor>((key >> 11) & 7u));
    b.dst_color_blendfactor = blendFactor(static_cast<BlendFactor>((key >> 14) & 7u));
    const BlendOp op = static_cast<BlendOp>((key >> 17) & 3u);
    b.color_blend_op = op == BlendOp::Subtract          ? SDL_GPU_BLENDOP_SUBTRACT
                       : op == BlendOp::ReverseSubtract ? SDL_GPU_BLENDOP_REVERSE_SUBTRACT
                                                        : SDL_GPU_BLENDOP_ADD;
    // The stored alpha is the source's, as the game's blend leaves it.
    b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUVertexBufferDescription buffer{};
    buffer.slot = 0u;
    buffer.pitch = sizeof(GpuVertex);
    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[4]{};
    attributes[0] = {0u, 0u, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, position)};
    attributes[1] = {1u, 0u, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuVertex, uv)};
    attributes[2] = {2u, 0u, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuVertex, color)};
    attributes[3] = {3u, 0u, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, offsetof(GpuVertex, fog)};

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = m_meshVertex;
    info.fragment_shader = ((key >> 19) & 1u) ? m_meshFragmentOpaque : m_meshFragment;
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1u;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 4u;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE; // the producer's indices are already culled
    // Clamped, not clipped, at the far plane: the game's sky sits past it.
    info.rasterizer_state.enable_depth_clip = false;
    info.depth_stencil_state.enable_depth_test = depth;
    info.depth_stencil_state.enable_depth_write = depth && depthWrite;
    info.depth_stencil_state.compare_op = compareOp(depthTest);
    info.target_info.color_target_descriptions = &color;
    info.target_info.num_color_targets = 1u;
    info.target_info.has_depth_stencil_target = attached;
    info.target_info.depth_stencil_format = attached ? m_depthFormat : SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    if (!pipeline)
        std::fprintf(stderr, "[native-pipeline] mesh pipeline %x refused: %s\n", key, SDL_GetError());
    m_meshPipelines.emplace(key, pipeline); // a refusal is kept too: not retried every draw
    return pipeline;
}

bool Renderer::uploadMeshes(SDL_GPUDevice *device, SDL_GPUCommandBuffer *commands, std::string &error)
{
    const uint64_t vertices = m_frameVerts.size(), indices = m_frameIndices.size();
    if (indices == 0u || vertices == 0u)
        return true;
    const uint64_t vertexBytes = vertices * sizeof(GpuVertex), indexBytes = indices * sizeof(uint32_t);
    if (vertexBytes + indexBytes > (1ull << 31))
    {
        error = "native meshes: a frame over 2 GiB of vertices";
        return false;
    }
    if (!growBuffer(device, m_vertexBuffer, m_vertexCapacity, static_cast<uint32_t>(vertexBytes),
                    SDL_GPU_BUFFERUSAGE_VERTEX, error) ||
        !growBuffer(device, m_indexBuffer, m_indexCapacity, static_cast<uint32_t>(indexBytes),
                    SDL_GPU_BUFFERUSAGE_INDEX, error))
        return false;
    const uint32_t total = static_cast<uint32_t>(vertexBytes + indexBytes);
    if (!m_meshTransfer || m_transferCapacity < total)
    {
        if (m_meshTransfer)
            SDL_ReleaseGPUTransferBuffer(device, m_meshTransfer);
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = std::max(total, m_transferCapacity * 2u);
        m_meshTransfer = SDL_CreateGPUTransferBuffer(device, &info);
        m_transferCapacity = m_meshTransfer ? info.size : 0u;
        if (!m_meshTransfer)
        {
            error = std::string("SDL_CreateGPUTransferBuffer(native meshes): ") + SDL_GetError();
            return false;
        }
    }
    auto *mapped = static_cast<uint8_t *>(SDL_MapGPUTransferBuffer(device, m_meshTransfer, true));
    if (!mapped)
    {
        error = std::string("SDL_MapGPUTransferBuffer(native meshes): ") + SDL_GetError();
        return false;
    }
    std::memcpy(mapped, m_frameVerts.data(), vertexBytes);
    std::memcpy(mapped + vertexBytes, m_frameIndices.data(), indexBytes);
    SDL_UnmapGPUTransferBuffer(device, m_meshTransfer);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTransferBufferLocation source{m_meshTransfer, 0u};
    SDL_GPUBufferRegion destination{m_vertexBuffer, 0u, static_cast<uint32_t>(vertexBytes)};
    SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    source.offset = static_cast<uint32_t>(vertexBytes);
    destination = {m_indexBuffer, 0u, static_cast<uint32_t>(indexBytes)};
    SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    SDL_EndGPUCopyPass(copy);
    return true;
}

void Renderer::drawMeshes(SDL_GPUDevice *device, SDL_GPUCommandBuffer *commands, SDL_GPURenderPass *pass,
                          size_t first, size_t last, Id target, bool depthAttached, float targetW, float targetH,
                          uint32_t targetScale, uint32_t sceneScale, SDL_GPUTexture *copy, uint32_t copyW, uint32_t copyH)
{
    if (!m_vertexBuffer || !m_indexBuffer)
        return;
    const SDL_GPUBufferBinding vertexBinding{m_vertexBuffer, 0u};
    SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
    const SDL_GPUBufferBinding indexBinding{m_indexBuffer, 0u};
    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_GPUGraphicsPipeline *bound = nullptr;
    SDL_GPUTexture *boundImage = nullptr, *boundPalette = nullptr;
    SDL_GPUSampler *boundSampler = nullptr;
    SDL_Rect boundScissor{-1, -1, -1, -1};
    float boundBlendConst = -1.0f;
    Id lastCamId = 0u;
    const CachedCamera *lastCam = nullptr;
    bool hasCached3d = false;
    Id cached3dCam = 0u;
    float cached3dModel[16] = {};
    float cached3dDepthScale = 0.0f, cached3dDepthMax = 0.0f;
    float cached3dClip[16] = {}, cached3dDepthRow[4] = {}, cached3dDepthLimit[2] = {};
    const bool remaster = remasterEnabled();
    Id lastTexId = 0u, lastPalId = 0u;
    bool lastTexResolved = false;
    SDL_GPUTexture *lastTexImage = nullptr, *lastTexPalette = nullptr;
    float lastTexOwnW = 1.0f, lastTexOwnH = 1.0f;
    uint32_t lastTexMode0 = 0u, lastTexMode3 = 0u;
    bool hasLastDu = false, hasLastMu = false;
    DrawUniforms lastDu{};
    MaterialUniforms lastMu{};
    for (size_t n = first; n < last; ++n)
    {
        const MeshDraw &d = m_frameMeshes[n];
        if (d.indexCount == 0u)
            continue;
        const CachedCamera *cachedCam = nullptr;
        if (d.camera != 0u)
        {
            if (d.camera == lastCamId)
                cachedCam = lastCam;
            else
            {
                const auto it = m_cameras.find(d.camera);
                cachedCam = it != m_cameras.end() ? &it->second : nullptr;
                lastCamId = d.camera;
                lastCam = cachedCam;
            }
        }
        const Camera *camera = cachedCam ? &cachedCam->cam : nullptr;
        const Material &m = d.material;
        // A 3D draw needs its camera; a window-space one only to map its
        // depth when it tests it. One that only writes depth (test ALWAYS:
        // the menus' text, with no 3D drawn yet) writes a constant without
        // one.
        if (d.hasModel && !d.windowModel ? !camera : (m.depthTest != Compare::Always && !camera && !d.windowModel))
        {
            ++m_meshCounts.noCamera;
            continue;
        }
        MaterialUniforms mu{};
        mu.texa[0] = m.fixedTextureAlpha;
        mu.texa[1] = m.alphaZeroOnBlack ? 1.0f : 0.0f;
        SDL_GPUTexture *image = m_white, *palette = m_white;
        float ownW = 1.0f, ownH = 1.0f;
        const bool remaster3d = remaster && d.hasModel && m.sourceTarget == 0u && m.wrapU < 2u && m.wrapV < 2u &&
                                m.destinationAlphaTest < 0;
        const bool remasterResolve = remaster && m.sourceTarget == 0u && m.destinationAlphaTest < 0;
        if (m.sourceTarget != 0u)
        {
            // A target drawn earlier this frame (or kept from before), read
            // in game pixels: each target carries its own scale.
            if (m.sourceTarget == target)
            {
                // The copy drawFrame took just before (only the run's
                // first draw is given one).
                if (!copy || n != first)
                {
                    ++m_meshCounts.feedback;
                    continue;
                }
                const float s = float(std::max(targetScale, 1u));
                image = palette = copy;
                ownW = float(copyW) / s;
                ownH = float(copyH) / s;
            }
            else if (m.sourceTarget == m_sceneTarget)
            {
                const float s = float(std::max(sceneScale, 1u));
                image = palette = m_scene;
                ownW = float(m_sceneWidth) / s;
                ownH = float(m_sceneHeight) / s;
            }
            else
            {
                const auto it = m_gpuTargets.find(m.sourceTarget);
                if (it == m_gpuTargets.end() || !it->second.texture)
                {
                    ++m_meshCounts.sourceNotDrawn;
                    continue;
                }
                const float s = float(std::max(it->second.scale ? it->second.scale : sceneScale, 1u));
                image = palette = it->second.texture;
                ownW = float(it->second.width) / s;
                ownH = float(it->second.height) / s;
            }
            mu.mode[0] = 3u;
            mu.mode[1] = static_cast<uint32_t>(m.function);
            mu.mode[2] = m.textureAlpha ? 1u : 0u;
            ++m_meshCounts.fromTarget;
        }
        else if (m.texture != 0u)
        {
            if (m.texture == lastTexId && m.palette == lastPalId && remasterResolve == lastTexResolved &&
                lastTexImage != nullptr)
            {
                image = lastTexImage;
                palette = lastTexPalette;
                ownW = lastTexOwnW;
                ownH = lastTexOwnH;
                mu.mode[0] = lastTexMode0;
                mu.mode[3] = lastTexMode3;
            }
            else
            {
                const auto it = m_images.find(m.texture);
                if (it == m_images.end())
                {
                    ++m_meshCounts.noTexture;
                    continue;
                }
                const Image &img = it->second;
                image = palette = img.texture;
                ownW = float(img.width);
                ownH = float(img.height);
                mu.mode[0] = 1u;
                if (img.format == PixelFormat::Rgba8 && remasterResolve)
                {
                    if (const auto r = m_resolved.find(uint64_t(m.texture) << 32u); r != m_resolved.end())
                        image = palette = r->second.texture;
                }
                else if (img.format == PixelFormat::Index8)
                {
                    const Id palId = m.palette ? m.palette : img.palette;
                    const uint64_t rkey = (uint64_t(m.texture) << 32u) | uint64_t(palId);
                    if (const auto r = remasterResolve ? m_resolved.find(rkey) : m_resolved.end(); r != m_resolved.end())
                    {
                        image = palette = r->second.texture;
                        mu.mode[0] = 1u;
                    }
                    else
                    {
                        const auto p = m_palettes.find(palId);
                        if (p == m_palettes.end())
                        {
                            ++m_meshCounts.noTexture;
                            continue;
                        }
                        palette = p->second.texture;
                        mu.mode[0] = 2u;
                        mu.mode[3] = p->second.width;
                    }
                }
                lastTexId = m.texture;
                lastPalId = m.palette;
                lastTexResolved = remasterResolve;
                lastTexImage = image;
                lastTexPalette = palette;
                lastTexOwnW = ownW;
                lastTexOwnH = ownH;
                lastTexMode0 = mu.mode[0];
                lastTexMode3 = mu.mode[3];
            }
            mu.mode[1] = static_cast<uint32_t>(m.function);
            mu.mode[2] = m.textureAlpha ? 1u : 0u;
        }
        if (m.destinationAlphaTest >= 0)
        {
            // The target's alpha, from the copy, at the pixel (the palette
            // slot, which an Index8 texture would need too).
            if (!copy || n != first || mu.mode[0] == 2u)
            {
                ++m_meshCounts.feedback;
                continue;
            }
            palette = copy;
            mu.texa[2] = 1.0f + float(m.destinationAlphaTest);
            ++m_meshCounts.destinationAlpha;
        }
        SDL_GPUGraphicsPipeline *pipeline = meshPipeline(device, pipelineKey(m, depthAttached));
        if (!pipeline)
        {
            ++m_meshCounts.noPipeline;
            continue;
        }
        if (pipeline != bound)
        {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            bound = pipeline;
            hasLastDu = false;
            hasLastMu = false;
        }
        mu.test[0] = static_cast<uint32_t>(m.alphaTest);
        mu.test[1] = m.alphaRef;
        mu.test[2] = m.alphaFail == AlphaFail::Nothing ? 1u : 0u;
        mu.test[3] = m.fog ? 1u : 0u;
        mu.size[0] = m.textureWidth ? float(m.textureWidth) : ownW;
        mu.size[1] = m.textureHeight ? float(m.textureHeight) : ownH;
        mu.size[2] = ownW;
        mu.size[3] = ownH;
        mu.blend[0] = m.shuffle == ChannelShuffle::GreenToAlpha ? 1.0f : 0.0f;
        mu.blend[1] = remaster ? (d.hasModel ? 2.0f : 1.0f) : 0.0f;
        mu.blend[2] = m.clampU ? 1.0f : 0.0f;
        mu.blend[3] = m.clampV ? 1.0f : 0.0f;
        mu.wrap[0] = m.wrapU;
        mu.wrap[1] = m.wrapV;
        mu.region[0] = m.regionU[0];
        mu.region[1] = m.regionU[1];
        mu.region[2] = m.regionV[0];
        mu.region[3] = m.regionV[1];
        for (int i = 0; i < 3; ++i)
            mu.fogColor[i] = float(d.fogColor[i]) / 255.0f;

        DrawUniforms du{};
        if (d.hasModel && d.windowModel)
        {
            // Object straight to the target's pixels (homogeneous): only the
            // depth needs mapping, Zgs to the clip z (Zgs - V32) / V22 as the
            // window-space draws below, with the program's depthScale.
            const double v22 = camera ? camera->viewport[10] : 0.0, v32 = camera ? camera->viewport[14] : 0.0;
            const double zScale = v22 != 0.0 ? (1.0 / v22) : 0.0;
            const double zBias = v22 != 0.0 ? (-v32 / v22) : 0.0;
            const bool scaled = d.depthScale != 1.0f && v22 != 0.0;
            const double s = scaled ? double(d.depthScale) : 1.0;
            windowModelClipAndDepth(d.model, zScale, zBias, du.clip, du.depthRow, s,
                                    scaled ? (1.0 - s) * (1.0 + v32 / v22) * 0.5 : 0.0);
            if (v22 != 0.0 && d.depthMax < 3.0e38f)
            {
                du.depthLimit[0] = float((1.0 - (double(d.depthMax) - v32) / v22) * 0.5);
                du.depthLimit[1] = 1.0f;
            }
            for (int i = 0; i < 4; ++i)
                du.viewport[i * 5] = 1.0f;
            ++m_meshCounts.window;
        }
        else if (d.hasModel)
        {
            if (hasCached3d && cached3dCam == d.camera && cached3dDepthScale == d.depthScale &&
                cached3dDepthMax == d.depthMax && std::memcmp(cached3dModel, d.model, sizeof(cached3dModel)) == 0)
            {
                std::memcpy(du.clip, cached3dClip, sizeof(du.clip));
                std::memcpy(du.depthRow, cached3dDepthRow, sizeof(du.depthRow));
                du.depthLimit[0] = cached3dDepthLimit[0];
                du.depthLimit[1] = cached3dDepthLimit[1];
            }
            else
            {
                // The program's depthScale and depthMax (see clipAndDepth). The
                // cap is per vertex, before interpolation, as the program's.
                const double v22 = camera->viewport[10], v32 = camera->viewport[14];
                const bool scaled = d.depthScale != 1.0f && v22 != 0.0;
                const double s = scaled ? double(d.depthScale) : 1.0;
                clipAndDepth(d.model, cachedCam->viewProj, du.clip, du.depthRow, s,
                             scaled ? (1.0 - s) * (1.0 + v32 / v22) * 0.5 : 0.0);
                if (v22 != 0.0 && d.depthMax < 3.0e38f)
                {
                    du.depthLimit[0] = float((1.0 - (double(d.depthMax) - v32) / v22) * 0.5);
                    du.depthLimit[1] = 1.0f;
                }
                hasCached3d = true;
                cached3dCam = d.camera;
                std::memcpy(cached3dModel, d.model, sizeof(cached3dModel));
                cached3dDepthScale = d.depthScale;
                cached3dDepthMax = d.depthMax;
                std::memcpy(cached3dClip, du.clip, sizeof(cached3dClip));
                std::memcpy(cached3dDepthRow, du.depthRow, sizeof(cached3dDepthRow));
                cached3dDepthLimit[0] = du.depthLimit[0];
                cached3dDepthLimit[1] = du.depthLimit[1];
            }
            std::memcpy(du.viewport, camera->viewport, sizeof(du.viewport));
        }
        else
        {
            // Window space: x and y are the target's pixels, w is 1. The
            // game's window depth is linear in the projection's z / w
            // (Zgs = V22 * z / w + V32, V the camera's viewport), so the
            // clip z the vertex shader turns into reversed depth is
            // (Zgs - V32) / V22. With no camera, depth is unused.
            for (int i = 0; i < 4; ++i)
                du.clip[i * 5] = du.viewport[i * 5] = 1.0f;
            du.clip[10] = 0.0f;
            if (camera && camera->viewport[10] != 0.0f)
            {
                du.clip[10] = 1.0f / camera->viewport[10];
                du.clip[14] = -camera->viewport[14] / camera->viewport[10];
                // (w - z) / 2 with w = 1 and z = (Zgs - V32) / V22, in double.
                const double v22 = camera->viewport[10], v32 = camera->viewport[14];
                du.depthRow[2] = float(-0.5 / v22);
                du.depthRow[3] = float(0.5 * (1.0 + v32 / v22));
            }
            else
                du.depthRow[3] = 0.5f; // (1 - 0) / 2, as before
            ++m_meshCounts.window;
        }
        du.target[0] = targetW;
        du.target[1] = targetH;
        du.target[2] = d.hasModel ? 0.0f : 1.0f; // fog per vertex
        std::memcpy(du.fog, d.fog, sizeof(du.fog));
        du.depthLimit[2] = d.clips ? 1.0f : 0.0f;

        if (m.blending && (m.srcFactor == BlendFactor::Fixed || m.srcFactor == BlendFactor::InvFixed ||
                           m.dstFactor == BlendFactor::Fixed || m.dstFactor == BlendFactor::InvFixed))
        {
            const float f = std::min(float(m.blendFactor) / 128.0f, 1.0f);
            if (f != boundBlendConst)
            {
                SDL_SetGPUBlendConstants(pass, SDL_FColor{f, f, f, f});
                boundBlendConst = f;
            }
        }
        if (!hasLastDu || std::memcmp(&du, &lastDu, sizeof(du)) != 0)
        {
            SDL_PushGPUVertexUniformData(commands, 0, &du, sizeof(du));
            lastDu = du;
            hasLastDu = true;
        }
        if (!hasLastMu || std::memcmp(&mu, &lastMu, sizeof(mu)) != 0)
        {
            SDL_PushGPUFragmentUniformData(commands, 0, &mu, sizeof(mu));
            lastMu = mu;
            hasLastMu = true;
        }
        const uint32_t samplerKey = (m.linear ? 1u : 0u) | (m.clampU ? 2u : 0u) | (m.clampV ? 4u : 0u);
        SDL_GPUSampler *sampler = meshSampler(device, samplerKey, remaster3d && mu.mode[0] == 1u);
        if (image != boundImage || palette != boundPalette || sampler != boundSampler)
        {
            const SDL_GPUTextureSamplerBinding bindings[2] = {{image, sampler}, {palette, sampler}};
            SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
            boundImage = image;
            boundPalette = palette;
            boundSampler = sampler;
        }

        const int32_t sx0 = std::clamp(d.scissor.x0, 0, int32_t(targetW));
        const int32_t sy0 = std::clamp(d.scissor.y0, 0, int32_t(targetH));
        const int32_t sx1 = std::clamp(d.scissor.x1, sx0, int32_t(targetW));
        const int32_t sy1 = std::clamp(d.scissor.y1, sy0, int32_t(targetH));
        const int32_t s = int32_t(std::max(targetScale, 1u));
        const SDL_Rect scissor{sx0 * s, sy0 * s, (sx1 - sx0) * s, (sy1 - sy0) * s};
        if (scissor.x != boundScissor.x || scissor.y != boundScissor.y || scissor.w != boundScissor.w ||
            scissor.h != boundScissor.h)
        {
            SDL_SetGPUScissor(pass, &scissor);
            boundScissor = scissor;
        }
        // Merge consecutive draws sharing identical GPU state into one indexed draw call
        // (indices are pre-rebased into m_frameIndices, so vertexOffset is 0).
        const uint32_t startIndex = d.firstIndex;
        uint32_t totalIndices = d.indexCount;
        const bool isFeedback = (m.sourceTarget == target || m.destinationAlphaTest >= 0);
        if (!isFeedback)
        {
            while (n + 1 < last)
            {
                const MeshDraw &next = m_frameMeshes[n + 1];
                if (next.indexCount == 0u || next.firstIndex != startIndex + totalIndices)
                    break;
                const Material &nm = next.material;
                if (nm.sourceTarget == target || nm.destinationAlphaTest >= 0)
                    break;
                if (next.hasModel != d.hasModel || next.windowModel != d.windowModel || next.camera != d.camera ||
                    next.clips != d.clips || next.depthScale != d.depthScale || next.depthMax != d.depthMax ||
                    next.scissor.x0 != d.scissor.x0 || next.scissor.y0 != d.scissor.y0 ||
                    next.scissor.x1 != d.scissor.x1 || next.scissor.y1 != d.scissor.y1 ||
                    std::memcmp(next.fog, d.fog, sizeof(d.fog)) != 0 ||
                    std::memcmp(next.fogColor, d.fogColor, sizeof(d.fogColor)) != 0)
                    break;
                if (d.hasModel && std::memcmp(next.model, d.model, sizeof(d.model)) != 0)
                    break;
                if (nm.texture != m.texture || nm.palette != m.palette || nm.sourceTarget != m.sourceTarget ||
                    nm.fixedTextureAlpha != m.fixedTextureAlpha || nm.alphaZeroOnBlack != m.alphaZeroOnBlack ||
                    nm.textureWidth != m.textureWidth || nm.textureHeight != m.textureHeight ||
                    nm.linear != m.linear || nm.clampU != m.clampU || nm.clampV != m.clampV ||
                    nm.wrapU != m.wrapU || nm.wrapV != m.wrapV ||
                    nm.regionU[0] != m.regionU[0] || nm.regionU[1] != m.regionU[1] ||
                    nm.regionV[0] != m.regionV[0] || nm.regionV[1] != m.regionV[1] ||
                    nm.function != m.function || nm.shuffle != m.shuffle || nm.textureAlpha != m.textureAlpha ||
                    nm.blending != m.blending || nm.srcFactor != m.srcFactor || nm.dstFactor != m.dstFactor ||
                    nm.blendOp != m.blendOp || nm.blendFactor != m.blendFactor ||
                    nm.alphaTest != m.alphaTest || nm.alphaRef != m.alphaRef || nm.alphaFail != m.alphaFail ||
                    nm.depthTest != m.depthTest || nm.depthWrite != m.depthWrite || nm.colorWrite != m.colorWrite ||
                    nm.fog != m.fog)
                    break;
                totalIndices += next.indexCount;
                ++n;
                ++m_meshCounts.merged;
            }
        }
        SDL_DrawGPUIndexedPrimitives(pass, totalIndices, 1, startIndex, 0, 0);
        ++m_meshCounts.drawn;
    }
}

bool Renderer::drawFrame(SDL_GPUDevice *device, SDL_GPUCommandBuffer *commands, uint32_t scale, std::string &error)
{
    scale = std::max(scale, 1u);
    // The scene starts each frame cleared (the game's own clears are not
    // records yet); every other target keeps what it holds.
    {
        SDL_GPUColorTargetInfo clear{};
        clear.texture = m_scene;
        clear.load_op = SDL_GPU_LOADOP_CLEAR;
        clear.store_op = SDL_GPU_STOREOP_STORE;
        clear.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &clear, 1, nullptr);
        if (!pass)
        {
            error = std::string("SDL_BeginGPURenderPass(native scene clear): ") + SDL_GetError();
            return false;
        }
        SDL_EndGPURenderPass(pass);
    }
    if (!uploadMeshes(device, commands, error))
        return false;
    prepareRemasterTextures(device, commands);
    // RG_NATIVE_FRAME_DUMP=N: the N-th frame's draws, one line each.
    static const uint64_t dumpFrame = [] {
        const char *v = std::getenv("RG_NATIVE_FRAME_DUMP");
        return v ? std::strtoull(v, nullptr, 10) : 0ull;
    }();
    // Or the frame after build/run/dump-gpu appears (the producer's trigger).
    // Check once every 60 frames instead of issuing an unlink() syscall every frame.
    const bool triggered = (m_meshCounts.frames % 60u == 0u) && std::remove("build/run/dump-gpu") == 0;
    if (triggered)
    {
        std::fprintf(stderr, "[frame-dump] frame %llu (%zu draws)\n", (unsigned long long)m_meshCounts.frames,
                     m_frameMeshes.size());
        m_dumpTargets = true;
    }
    if ((dumpFrame != 0u && m_meshCounts.frames == dumpFrame) || triggered)
    {
        for (size_t n = 0; n < m_frameMeshes.size(); ++n)
        {
            const MeshDraw &d = m_frameMeshes[n];
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            const Vertex *vs = d.vertexCount ? (m_frameVerts.data() + d.firstVertex) : nullptr;
            for (uint32_t k = 0; k < d.vertexCount; ++k)
            {
                const Vertex &v = vs[k];
                x0 = std::min(x0, v.position[0]);
                y0 = std::min(y0, v.position[1]);
                x1 = std::max(x1, v.position[0]);
                y1 = std::max(y1, v.position[1]);
            }
            const Material &m = d.material;
            const float *c = vs ? vs[0].color : nullptr;
            std::fprintf(stderr,
                         "[frame-dump] %4zu target %u depth %u %s src %s%u fn %d tcc %d blend %d/%d/%d fix %u write %x "
                         "atest %d/%u ztest %d zw %d tris %u box %.0f,%.0f-%.0f,%.0f rgba %.0f,%.0f,%.0f,%.0f z %.0f fog %.0f cam %u%s\n",
                         n, d.target, d.depthTarget, d.hasModel ? "3d" : "2d", m.sourceTarget ? "target " : "tex ",
                         m.sourceTarget ? m.sourceTarget : m.texture, int(m.function), int(m.textureAlpha),
                         m.blending ? int(m.srcFactor) : -1, int(m.dstFactor), int(m.blendOp), m.blendFactor, m.colorWrite, int(m.alphaTest), m.alphaRef, int(m.depthTest),
                         int(m.depthWrite), d.indexCount / 3u, d.hasModel ? 0.f : x0, d.hasModel ? 0.f : y0,
                         d.hasModel ? 0.f : x1, d.hasModel ? 0.f : y1, c ? c[0] : 0.f, c ? c[1] : 0.f,
                         c ? c[2] : 0.f, c ? c[3] : 0.f, vs ? vs[0].position[2] : 0.f,
                         vs ? vs[0].fog : 0.f, d.camera, m.known ? "" : " (unknown)");
        }
    }
    std::vector<Id> depthsCleared;
    const size_t count = m_frameMeshes.size();
    // Locate the last 3D draw into the scene target so Remaster SSAO composites
    // right after 3D geometry and before 2D HUD/menus are drawn.
    size_t last3dSceneDraw = count;
    if (remasterEnabled() && m_depthCanSample)
    {
        for (size_t k = count; k-- > 0;)
        {
            const MeshDraw &d = m_frameMeshes[k];
            if (d.target == m_sceneTarget && d.hasModel && d.depthTarget != 0u && d.indexCount != 0u)
            {
                last3dSceneDraw = k;
                break;
            }
        }
    }
    bool ssaoApplied = false;
    size_t i = 0;
    while (i < count)
    {
        const Id target = m_frameMeshes[i].target;
        size_t j = i;
        Id depthId = 0u;
        while (j < count && m_frameMeshes[j].target == target)
        {
            const Id d = m_frameMeshes[j].depthTarget;
            if (d != 0u && depthId == 0u)
                depthId = d;
            else if (d != 0u && d != depthId)
                ++m_meshCounts.depthMismatch;
            ++j;
        }
        const size_t first = i;
        i = j;
        const auto declared = m_targets.find(target);
        if (declared == m_targets.end() || declared->second.width == 0u || declared->second.height == 0u)
        {
            m_meshCounts.noTarget += j - first;
            continue;
        }
        if (declared->second.format == PixelFormat::Depth32)
        {
            m_meshCounts.depthAsColor += j - first;
            continue;
        }
        const uint32_t gameW = declared->second.width, gameH = declared->second.height;
        const bool scene = target == m_sceneTarget;
        if (!scene)
            m_meshCounts.offscreen += j - first;
        // Small offscreen effect buffers (blur/bloom, <= 256x256, not sharing a
        // full-size depth target) run at DQ8_GFX_EFFECT_SCALE (default 1; 0 = scene scale).
        static const uint32_t effectScale = [] {
            const char *v = std::getenv("DQ8_GFX_EFFECT_SCALE");
            if (!v || !*v)
                return 1u;
            return static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        }();
        bool smallEffect = !scene && declared->second.role == TargetRole::Offscreen && gameW <= 256u && gameH <= 256u;
        if (smallEffect && depthId != 0u)
        {
            const auto dt = m_targets.find(depthId);
            if (dt == m_targets.end() || dt->second.role == TargetRole::SceneDepth || dt->second.width > 256u ||
                dt->second.height > 256u)
                smallEffect = false;
        }
        const uint32_t targetScale = (smallEffect && effectScale != 0u) ? std::min(scale, effectScale) : scale;
        SDL_GPUTexture *color = m_scene;
        uint32_t colorW = m_sceneWidth, colorH = m_sceneHeight;
        bool fresh = false;
        if (!scene)
        {
            color = targetTexture(device, target, false, gameW * targetScale, gameH * targetScale, targetScale, fresh, error);
            if (!color)
                return false;
            colorW = m_gpuTargets[target].width;
            colorH = m_gpuTargets[target].height;
        }
        SDL_GPUColorTargetInfo colorInfo{};
        colorInfo.texture = color;
        colorInfo.load_op = fresh ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        colorInfo.store_op = SDL_GPU_STOREOP_STORE;
        colorInfo.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f};
        SDL_GPUDepthStencilTargetInfo depthInfo{};
        if (depthId != 0u)
        {
            bool depthFresh = false;
            depthInfo.texture = targetTexture(device, depthId, true, colorW, colorH, targetScale, depthFresh, error);
            if (!depthInfo.texture)
                return false;
            const bool firstUse = std::find(depthsCleared.begin(), depthsCleared.end(), depthId) == depthsCleared.end();
            if (firstUse)
                depthsCleared.push_back(depthId);
            depthInfo.load_op = firstUse || depthFresh ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthInfo.store_op = SDL_GPU_STOREOP_STORE;
            depthInfo.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depthInfo.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            depthInfo.clear_depth = 0.0f;
        }
        // A draw that reads the target it draws into (its texels, or its
        // alpha for the destination alpha test) reads a copy taken just
        // before it: the pass ends there, and the next one loads.
        const auto readsTarget = [&](const MeshDraw &d) {
            return d.material.sourceTarget == target || d.material.destinationAlphaTest >= 0;
        };
        size_t a = first;
        while (a < j)
        {
            const bool reads = readsTarget(m_frameMeshes[a]);
            size_t b = a + 1;
            while (b < j && !readsTarget(m_frameMeshes[b]))
                ++b;
            // End the pass right after the last 3D scene draw so Remaster SSAO
            // composites onto 3D geometry before any 2D HUD/menus are drawn.
            if (!ssaoApplied && scene && depthId != 0u && a <= last3dSceneDraw && last3dSceneDraw + 1u < b)
                b = last3dSceneDraw + 1u;
            SDL_GPUTexture *copy = nullptr;
            if (reads)
            {
                bool copyFresh = false;
                copy = targetTexture(device, kFeedbackCopy, false, colorW, colorH, targetScale, copyFresh, error);
                if (!copy)
                    return false;
                // Copy only the texel region that draw `a` actually reads, at the
                // same (x, y) offset in `copy` so shader UVs & gl_FragCoord stay identical.
                const MeshDraw &fd = m_frameMeshes[a];
                int32_t rx0 = int32_t(gameW), ry0 = int32_t(gameH), rx1 = 0, ry1 = 0;
                bool fullCopy = fd.hasModel || fd.vertexCount == 0u;
                if (!fullCopy && fd.material.sourceTarget == target)
                {
                    const float sx = fd.material.textureWidth ? float(fd.material.textureWidth) : float(colorW) / float(targetScale);
                    const float sy = fd.material.textureHeight ? float(fd.material.textureHeight) : float(colorH) / float(targetScale);
                    float umin = 1e9f, vmin = 1e9f, umax = -1e9f, vmax = -1e9f;
                    const Vertex *vs = m_frameVerts.data() + fd.firstVertex;
                    for (uint32_t k = 0; k < fd.vertexCount; ++k)
                    {
                        const float u = vs[k].uv[0] * sx;
                        const float v = vs[k].uv[1] * sy;
                        umin = std::min(umin, u);
                        vmin = std::min(vmin, v);
                        umax = std::max(umax, u);
                        vmax = std::max(vmax, v);
                    }
                    // If repeat wrap crosses the target boundary, fall back to full copy.
                    if ((!fd.material.clampU && (umin < 0.0f || umax > float(gameW))) ||
                        (!fd.material.clampV && (vmin < 0.0f || vmax > float(gameH))))
                        fullCopy = true;
                    else
                    {
                        rx0 = std::min(rx0, int32_t(std::floor(umin)) - 2);
                        ry0 = std::min(ry0, int32_t(std::floor(vmin)) - 2);
                        rx1 = std::max(rx1, int32_t(std::ceil(umax)) + 2);
                        ry1 = std::max(ry1, int32_t(std::ceil(vmax)) + 2);
                    }
                }
                if (!fullCopy && fd.material.destinationAlphaTest >= 0)
                {
                    rx0 = std::min(rx0, fd.scissor.x0);
                    ry0 = std::min(ry0, fd.scissor.y0);
                    rx1 = std::max(rx1, fd.scissor.x1);
                    ry1 = std::max(ry1, fd.scissor.y1);
                }
                uint32_t cx = 0u, cy = 0u, cw = colorW, ch = colorH;
                if (!fullCopy)
                {
                    rx0 = std::clamp(rx0, 0, int32_t(gameW));
                    ry0 = std::clamp(ry0, 0, int32_t(gameH));
                    rx1 = std::clamp(rx1, rx0, int32_t(gameW));
                    ry1 = std::clamp(ry1, ry0, int32_t(gameH));
                    cx = uint32_t(rx0) * targetScale;
                    cy = uint32_t(ry0) * targetScale;
                    cw = std::min(uint32_t(rx1 - rx0) * targetScale, colorW - std::min(cx, colorW));
                    ch = std::min(uint32_t(ry1 - ry0) * targetScale, colorH - std::min(cy, colorH));
                }
                if (cw > 0u && ch > 0u)
                {
                    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(commands);
                    SDL_GPUTextureLocation from{}, to{};
                    from.texture = color;
                    from.x = cx;
                    from.y = cy;
                    to.texture = copy;
                    to.x = cx;
                    to.y = cy;
                    SDL_CopyGPUTextureToTexture(pass, &from, &to, cw, ch, 1, false);
                    SDL_EndGPUCopyPass(pass);
                    ++m_meshCounts.copies;
                }
            }
            SDL_GPURenderPass *pass =
                SDL_BeginGPURenderPass(commands, &colorInfo, 1, depthId ? &depthInfo : nullptr);
            if (!pass)
            {
                error = std::string("SDL_BeginGPURenderPass(native target): ") + SDL_GetError();
                return false;
            }
            ++m_meshCounts.passes;
            const SDL_GPUViewport viewport{0.0f, 0.0f, float(gameW * targetScale), float(gameH * targetScale), 0.0f, 1.0f};
            SDL_SetGPUViewport(pass, &viewport);
            const GpuTarget &copied = m_gpuTargets[kFeedbackCopy]; // its texture may be the larger
            drawMeshes(device, commands, pass, a, b, target, depthId != 0u, float(gameW), float(gameH), targetScale,
                       scale, copy, copied.width, copied.height);
            SDL_EndGPURenderPass(pass);
            if (!ssaoApplied && scene && depthId != 0u && depthInfo.texture &&
                a <= last3dSceneDraw && last3dSceneDraw < b)
            {
                applySsao(device, commands, m_scene, depthInfo.texture, m_sceneWidth, m_sceneHeight);
                ssaoApplied = true;
            }
            colorInfo.load_op = SDL_GPU_LOADOP_LOAD;
            depthInfo.load_op = SDL_GPU_LOADOP_LOAD;
            a = b;
        }
    }
    return true;
}

void Renderer::releaseMeshes(SDL_GPUDevice *device)
{
    for (auto &[key, pipeline] : m_meshPipelines)
        if (pipeline)
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    m_meshPipelines.clear();
    for (SDL_GPUSampler *&sampler : m_meshSamplers)
    {
        if (sampler)
            SDL_ReleaseGPUSampler(device, sampler);
        sampler = nullptr;
    }
    for (SDL_GPUSampler *&sampler : m_remasterSamplers)
    {
        if (sampler)
            SDL_ReleaseGPUSampler(device, sampler);
        sampler = nullptr;
    }
    if (m_meshVertex)
        SDL_ReleaseGPUShader(device, m_meshVertex);
    if (m_meshFragment)
        SDL_ReleaseGPUShader(device, m_meshFragment);
    if (m_meshFragmentOpaque)
        SDL_ReleaseGPUShader(device, m_meshFragmentOpaque);
    for (auto &[id, target] : m_gpuTargets)
        if (target.texture)
            SDL_ReleaseGPUTexture(device, target.texture);
    m_gpuTargets.clear();
    if (m_white)
        SDL_ReleaseGPUTexture(device, m_white);
    if (m_vertexBuffer)
        SDL_ReleaseGPUBuffer(device, m_vertexBuffer);
    if (m_indexBuffer)
        SDL_ReleaseGPUBuffer(device, m_indexBuffer);
    if (m_meshTransfer)
        SDL_ReleaseGPUTransferBuffer(device, m_meshTransfer);
    m_meshVertex = m_meshFragment = m_meshFragmentOpaque = nullptr;
    m_white = nullptr;
    m_vertexBuffer = m_indexBuffer = nullptr;
    m_meshTransfer = nullptr;
    m_vertexCapacity = m_indexCapacity = m_transferCapacity = 0u;
    m_meshFailed = false;
}
} // namespace dq8::gfx::native
