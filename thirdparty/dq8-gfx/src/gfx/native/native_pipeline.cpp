#include "gfx/native/native_pipeline.h"

#include "native_cas.frag.spv.h"
#include "native_cas.vert.spv.h"
#include "native_cnn_conv0.frag.spv.h"
#include "native_cnn_conv1.frag.spv.h"
#include "native_cnn_conv2.frag.spv.h"
#include "native_cnn_conv3.frag.spv.h"
#include "native_cnn_conv4.frag.spv.h"
#include "native_cnn_conv5.frag.spv.h"
#include "native_cnn_conv6.frag.spv.h"
#include "native_cnn_d2s.frag.spv.h"
#include "native_cnn_last.frag.spv.h"
#include "native_fxaa.frag.spv.h"
#include "native_ssao.frag.spv.h"
#include "native_ssao_blur.frag.spv.h"
#include "native_upscale2x.frag.spv.h"
#include "native_upscale2xbr.frag.spv.h"
#include "native_view.frag.spv.h"
#include "native_view.vert.spv.h"

#include <algorithm>
#include <filesystem>
#include <tuple>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <unordered_map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <type_traits>

namespace dq8::gfx::native
{
namespace
{
std::atomic<bool (*)()> g_outer{nullptr};
std::atomic<bool> g_remaster{true};

constexpr uint64_t kResolvedBudget = 256ull << 20;

uint32_t mipLevelsFor(uint32_t width, uint32_t height)
{
    uint32_t dim = std::max(width, height);
    uint32_t levels = 1u;
    while (dim > 1u)
    {
        dim >>= 1u;
        ++levels;
    }
    return levels;
}

struct Queue
{
    std::mutex mutex;
    std::vector<Record> records;
    std::vector<Vertex> meshVertices;
    std::vector<uint32_t> meshIndices;
    std::vector<Id> evicted;
    std::atomic<bool> evictedPending{false};
    std::function<std::string()> producerStats;
};
Queue &queue()
{
    static Queue instance;
    return instance;
}

// The scene target until a producer declares one (the game's frame size).
constexpr uint32_t kDefaultWidth = 640u;
constexpr uint32_t kDefaultHeight = 448u;
// The textures held, past which the oldest are let go (and their producers
// told, takeEvicted).
constexpr uint64_t kImageBudget = 256ull << 20;
// The debug view's grid.
constexpr uint32_t kViewColumns = 8u;
constexpr uint32_t kViewRows = 6u;

constexpr const char *kRecordNames[] = {"texture", "palette", "target",     "camera",   "mesh",      "sprite",
                                        "effect",  "frame",   "present",    "readback", "depth-query", "fill"};
static_assert(std::size(kRecordNames) == std::variant_size_v<Record>, "a name per record kind");

// Step 7b's depth reads: what was asked since the renderer last took them,
// and the last answer for each place in that order.
struct DepthMailbox
{
    std::mutex mutex;
    std::vector<DepthBlock> wanted;
    uint32_t asked = 0; // reads since the renderer last took them
    struct Answer
    {
        DepthBlock block;
        std::vector<float> values;
        bool valid = false;
    };
    Answer answers[kMaxDepthReads];
};
DepthMailbox &depthMailbox()
{
    static DepthMailbox instance;
    return instance;
}

std::vector<DepthBlock> takeDepthReads()
{
    DepthMailbox &m = depthMailbox();
    std::lock_guard lock(m.mutex);
    m.asked = 0u;
    std::vector<DepthBlock> out;
    out.swap(m.wanted);
    return out;
}

void publishDepth(uint32_t index, const DepthBlock &block, std::vector<float> &&values)
{
    if (index >= kMaxDepthReads)
        return;
    DepthMailbox &m = depthMailbox();
    std::lock_guard lock(m.mutex);
    m.answers[index].block = block;
    m.answers[index].values = std::move(values);
    m.answers[index].valid = true;
}
// Step 7c's colour reads, by id.
struct ColorMailbox
{
    std::mutex mutex;
    std::condition_variable done;
    uint32_t next = 1;
    struct Entry
    {
        ColorRead read;
        bool finished = false, ok = false;
        std::vector<uint8_t> rgba;
        uint32_t width = 0, height = 0; // of the answer, in game pixels
    };
    std::unordered_map<uint32_t, Entry> entries;
};
ColorMailbox &colorMailbox()
{
    static ColorMailbox instance;
    return instance;
}
} // namespace

uint32_t beginColorRead(const ColorRead &read)
{
    ColorMailbox &m = colorMailbox();
    std::lock_guard lock(m.mutex);
    uint32_t id = m.next++;
    if (id == 0u) // wrapped: 0 is none
        id = m.next++;
    m.entries[id].read = read;
    return id;
}

bool colorReadsWaiting()
{
    ColorMailbox &m = colorMailbox();
    std::lock_guard lock(m.mutex);
    for (const auto &entry : m.entries)
        if (!entry.second.finished)
            return true;
    return false;
}

bool waitColorRead(uint32_t id, std::vector<uint8_t> &rgba, uint32_t timeoutMs)
{
    uint32_t width = 0, height = 0;
    return waitColorReadSized(id, rgba, width, height, timeoutMs);
}

bool waitColorReadSized(uint32_t id, std::vector<uint8_t> &rgba, uint32_t &width, uint32_t &height,
                        uint32_t timeoutMs)
{
    ColorMailbox &m = colorMailbox();
    std::unique_lock lock(m.mutex);
    const bool finished = m.done.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
        const auto e = m.entries.find(id);
        return e == m.entries.end() || e->second.finished;
    });
    const auto e = m.entries.find(id);
    if (e == m.entries.end())
        return false;
    const bool ok = finished && e->second.finished && e->second.ok;
    if (ok)
    {
        rgba = std::move(e->second.rgba);
        width = e->second.width;
        height = e->second.height;
    }
    m.entries.erase(e); // a late answer finds nothing to fill
    return ok;
}

bool readDepth(const DepthBlock &block, std::vector<float> &out)
{
    if (!enabled() || block.target == 0u || block.width == 0u || block.height == 0u ||
        block.width > kMaxDepthSide || block.height > kMaxDepthSide)
        return false;
    DepthMailbox &m = depthMailbox();
    std::lock_guard lock(m.mutex);
    const uint32_t index = m.asked++;
    if (m.wanted.size() < kMaxDepthReads)
        m.wanted.push_back(block);
    if (index >= kMaxDepthReads)
        return false;
    const DepthMailbox::Answer &a = m.answers[index];
    if (!a.valid || a.block.target != block.target || a.block.width != block.width ||
        a.block.height != block.height)
        return false;
    out = a.values;
    return true;
}

void setOuterSwitch(bool (*outer)())
{
    g_outer.store(outer, std::memory_order_release);
}

void setRemaster(bool on)
{
    g_remaster.store(on, std::memory_order_relaxed);
}

bool remasterEnabled()
{
    return g_remaster.load(std::memory_order_relaxed);
}

bool enabled()
{
    static const bool on = [] {
        bool (*outer)() = g_outer.load(std::memory_order_acquire);
        const char *v = std::getenv("RG_NATIVE_PIPELINE");
        const bool given = v && *v;
        // On unless RG_NATIVE_PIPELINE=0, where the native renderer is on.
        const bool asked = given ? *v != '0' : true;
        const bool result = asked && outer && outer();
        if (given && asked && !result)
            std::fprintf(stderr, "[native-pipeline] RG_NATIVE_PIPELINE ignored: the native renderer is off\n");
        else if (result)
            std::fprintf(stderr, "[native-pipeline] on\n");
        return result;
    }();
    return on;
}

void push(Record &&record)
{
    if (!enabled())
        return;
    Queue &q = queue();
    std::lock_guard lock(q.mutex);
    q.records.push_back(std::move(record));
}

void pushMesh(MeshDraw &&draw, const Vertex *vertices, uint32_t vertexCount, const uint32_t *indices,
              uint32_t indexCount)
{
    if (!enabled())
        return;
    Queue &q = queue();
    std::lock_guard lock(q.mutex);
    draw.firstVertex = static_cast<uint32_t>(q.meshVertices.size());
    draw.vertexCount = vertexCount;
    if (vertexCount != 0u)
    {
        if (q.meshVertices.capacity() < q.meshVertices.size() + vertexCount)
            q.meshVertices.reserve(std::max<size_t>(q.meshVertices.size() * 2u + vertexCount, 65536u));
        q.meshVertices.insert(q.meshVertices.end(), vertices, vertices + vertexCount);
    }
    draw.firstIndex = static_cast<uint32_t>(q.meshIndices.size());
    draw.indexCount = indexCount;
    if (indexCount != 0u)
    {
        if (q.meshIndices.capacity() < q.meshIndices.size() + indexCount)
            q.meshIndices.reserve(std::max<size_t>(q.meshIndices.size() * 2u + indexCount, 131072u));
        q.meshIndices.insert(q.meshIndices.end(), indices, indices + indexCount);
    }
    q.records.push_back(std::move(draw));
}

bool hasEvicted()
{
    return queue().evictedPending.load(std::memory_order_acquire);
}

std::vector<Id> takeEvicted()
{
    Queue &q = queue();
    if (!q.evictedPending.load(std::memory_order_acquire))
        return {};
    std::lock_guard lock(q.mutex);
    std::vector<Id> out;
    out.swap(q.evicted);
    q.evictedPending.store(false, std::memory_order_release);
    return out;
}

void setProducerStats(std::function<std::string()> line)
{
    Queue &q = queue();
    std::lock_guard lock(q.mutex);
    q.producerStats = std::move(line);
}

void Renderer::count(const MeshDraw &draw)
{
    Readiness &r = m_readiness;
    ++r.meshDraws;
    if (draw.camera != 0u && draw.hasModel && m_cameras.count(draw.camera))
        ++r.cameraAndModel;
    if (draw.object != 0u)
        ++r.objectIdentity;
    if (const auto t = m_targets.find(draw.target);
        t != m_targets.end() && t->second.role == TargetRole::SceneColor && draw.floatDepth)
        ++r.sceneFloatDepth;
    if (draw.material.known)
        ++r.materialKnown;
    if (draw.hasNormals)
        ++r.normals;
}

SDL_GPUTexture *Renderer::createTexture(SDL_GPUDevice *device, SDL_GPUTextureFormat format, uint32_t width,
                                        uint32_t height, uint32_t numLevels, SDL_GPUTextureUsageFlags usage,
                                        std::string &error)
{
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = usage;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = std::max(numLevels, 1u);
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &info);
    if (!texture)
        error = std::string("SDL_CreateGPUTexture(native texture): ") + SDL_GetError();
    return texture;
}

void Renderer::apply(SDL_GPUDevice *device, Record &record, bool inPlaceMeshes, std::string &error)
{
    ++m_records[record.index()];
    std::visit(
        [&](auto &r) {
            using T = std::decay_t<decltype(r)>;
            if constexpr (std::is_same_v<T, Target>)
            {
                m_targets[r.id] = r;
                if (r.role == TargetRole::SceneColor)
                    m_sceneTarget = r.id;
            }
            else if constexpr (std::is_same_v<T, TargetFill>)
            {
                // Drawn into the target at the next frame (render).
                if (r.id != 0u && r.width != 0u && r.height != 0u && r.rgba &&
                    r.rgba->size() >= size_t(r.width) * r.height * 4u)
                    m_fills.push_back(std::move(r));
            }
            else if constexpr (std::is_same_v<T, Camera>)
            {
                CachedCamera cached{};
                cached.cam = r;
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j)
                    {
                        double sum = 0.0;
                        for (int k = 0; k < 4; ++k)
                            sum += double(r.view[i * 4 + k]) * double(r.projection[k * 4 + j]);
                        cached.viewProj[i * 4 + j] = sum;
                    }
                m_cameras[r.id] = cached;
                // A moving camera is a new one every frame: keep the window
                // the producer can still name, not the whole session.
                m_newestCamera = std::max(m_newestCamera, r.id);
                if (m_cameras.size() > 4u * kCameraWindow && m_newestCamera > 2u * kCameraWindow)
                    std::erase_if(m_cameras, [&](const auto &c) { return c.first < m_newestCamera - 2u * kCameraWindow; });
            }
            else if constexpr (std::is_same_v<T, MeshDraw>)
            {
                count(r);
                if (inPlaceMeshes)
                {
                    if (r.indexCount != 0u && r.firstVertex != 0u)
                    {
                        uint32_t *dstI = m_meshIndices.data() + r.firstIndex;
                        const uint32_t baseV = r.firstVertex;
                        for (uint32_t k = 0; k < r.indexCount; ++k)
                            dstI[k] += baseV;
                    }
                }
                else
                {
                    const uint32_t baseVertex = static_cast<uint32_t>(m_meshVerts.size());
                    const uint32_t baseIndex = static_cast<uint32_t>(m_meshIndices.size());
                    if (r.vertexCount != 0u)
                    {
                        m_meshVerts.resize(baseVertex + r.vertexCount);
                        std::memcpy(m_meshVerts.data() + baseVertex, m_drainedVerts.data() + r.firstVertex,
                                    size_t(r.vertexCount) * sizeof(Vertex));
                    }
                    if (r.indexCount != 0u)
                    {
                        const uint32_t *srcI = m_drainedIndices.data() + r.firstIndex;
                        m_meshIndices.resize(baseIndex + r.indexCount);
                        uint32_t *dstI = m_meshIndices.data() + baseIndex;
                        for (uint32_t k = 0; k < r.indexCount; ++k)
                            dstI[k] = srcI[k] + baseVertex;
                    }
                    r.firstVertex = baseVertex;
                    r.firstIndex = baseIndex;
                }
                // Any target (step 5a); one not declared is counted when drawn.
                m_meshes.push_back(std::move(r));
            }
            else if constexpr (std::is_same_v<T, Present>)
            {
                // The draws since the last one are a frame; a frame not yet
                // drawn when the next closes is dropped (the newest shows).
                m_frameMeshes.swap(m_meshes);
                m_frameVerts.swap(m_meshVerts);
                m_frameIndices.swap(m_meshIndices);
                m_meshes.clear();
                m_meshVerts.clear();
                m_meshIndices.clear();
                m_frameReady = true;
                ++m_meshCounts.frames;
            }
            else if constexpr (std::is_same_v<T, Texture>)
            {
                const uint32_t bpp = r.format == PixelFormat::Rgba8 ? 4u : r.format == PixelFormat::Index8 ? 1u : 0u;
                const uint64_t bytes = uint64_t(r.width) * r.height * bpp;
                if (r.id == 0u || bpp == 0u || bytes == 0u || !r.texels || r.texels->size() < bytes)
                {
                    ++m_refused;
                    return;
                }
                const bool rgba = (bpp == 4u);
                const uint32_t levels = rgba ? mipLevelsFor(r.width, r.height) : 1u;
                const SDL_GPUTextureUsageFlags usage =
                    levels > 1u ? (SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)
                                : SDL_GPU_TEXTUREUSAGE_SAMPLER;
                SDL_GPUTexture *texture = createTexture(
                    device, rgba ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM : SDL_GPU_TEXTUREFORMAT_R8_UNORM,
                    r.width, r.height, levels, usage, error);
                if (!texture)
                    return;
                if (const auto old = m_images.find(r.id); old != m_images.end())
                {
                    SDL_ReleaseGPUTexture(device, old->second.texture);
                    m_imageBytes -= old->second.bytes;
                    m_imageOrder.erase(std::find(m_imageOrder.begin(), m_imageOrder.end(), r.id));
                    invalidateResolved(device, r.id, false);
                }
                const bool keepRgbaCpu = rgba && r.width >= 16u && r.height >= 16u && r.width <= 512u && r.height <= 512u;
                m_images[r.id] = Image{texture, r.format, r.width, r.height, r.palette, bytes,
                                       (!rgba || keepRgbaCpu) ? r.texels : nullptr};
                m_imageOrder.push_back(r.id);
                m_imageBytes += bytes;
                m_uploads.push_back({texture, r.width, r.height, r.texels, levels > 1u});
                m_recent.erase(std::remove(m_recent.begin(), m_recent.end(), r.id), m_recent.end());
                m_recent.push_front(r.id);
                if (m_recent.size() > kViewColumns * kViewRows)
                    m_recent.pop_back();
            }
            else if constexpr (std::is_same_v<T, Palette>)
            {
                if (r.id == 0u || (r.entries != 16u && r.entries != 256u) || !r.colors ||
                    r.colors->size() < r.entries * 4u)
                {
                    ++m_refused;
                    return;
                }
                SDL_GPUTexture *texture =
                    createTexture(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, r.entries, 1u, 1u,
                                  SDL_GPU_TEXTUREUSAGE_SAMPLER, error);
                if (!texture)
                    return;
                if (const auto old = m_palettes.find(r.id); old != m_palettes.end())
                {
                    SDL_ReleaseGPUTexture(device, old->second.texture);
                    invalidateResolved(device, r.id, true);
                }
                m_palettes[r.id] = Image{texture, PixelFormat::Rgba8, r.entries, 1u, 0u, r.entries * 4u, r.colors};
                m_uploads.push_back({texture, r.entries, 1u, r.colors, false});
            }
            // The rest is drawn from step 4 on; counted above.
        },
        record);
}

bool Renderer::upload(SDL_GPUDevice *device, SDL_GPUCommandBuffer *commands, std::string &error)
{
    if (m_uploads.empty())
        return true;
    uint64_t total = 0u;
    for (const PendingUpload &u : m_uploads)
        total += u.bytes->size();
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = static_cast<uint32_t>(total);
    SDL_GPUTransferBuffer *buffer = SDL_CreateGPUTransferBuffer(device, &info);
    if (!buffer)
    {
        error = std::string("SDL_CreateGPUTransferBuffer(native textures): ") + SDL_GetError();
        return false;
    }
    auto *mapped = static_cast<uint8_t *>(SDL_MapGPUTransferBuffer(device, buffer, false));
    if (!mapped)
    {
        error = std::string("SDL_MapGPUTransferBuffer(native textures): ") + SDL_GetError();
        SDL_ReleaseGPUTransferBuffer(device, buffer);
        return false;
    }
    uint32_t offset = 0u;
    for (const PendingUpload &u : m_uploads)
    {
        std::memcpy(mapped + offset, u.bytes->data(), u.bytes->size());
        offset += static_cast<uint32_t>(u.bytes->size());
    }
    SDL_UnmapGPUTransferBuffer(device, buffer);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
    offset = 0u;
    for (const PendingUpload &u : m_uploads)
    {
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = buffer;
        source.offset = offset;
        source.pixels_per_row = u.width;
        source.rows_per_layer = u.height;
        SDL_GPUTextureRegion destination{};
        destination.texture = u.texture;
        destination.w = u.width;
        destination.h = u.height;
        destination.d = 1u;
        SDL_UploadToGPUTexture(copy, &source, &destination, false);
        offset += static_cast<uint32_t>(u.bytes->size());
    }
    SDL_EndGPUCopyPass(copy);
    for (const PendingUpload &u : m_uploads)
    {
        if (u.generateMips)
            SDL_GenerateMipmapsForGPUTexture(commands, u.texture);
    }
    // Freed once the GPU is done with it.
    SDL_ReleaseGPUTransferBuffer(device, buffer);
    m_uploads.clear();
    return true;
}

void Renderer::invalidateResolved(SDL_GPUDevice *device, Id id, bool isPalette)
{
    for (auto it = m_resolved.begin(); it != m_resolved.end();)
    {
        const Id texId = static_cast<Id>(it->first >> 32u);
        const Id palId = static_cast<Id>(it->first & 0xFFFFFFFFu);
        if ((isPalette ? palId : texId) == id)
        {
            SDL_ReleaseGPUTexture(device, it->second.texture);
            m_resolvedBytes -= it->second.bytes;
            it = m_resolved.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void Renderer::evictResolved(SDL_GPUDevice *device)
{
    while (m_resolvedBytes > kResolvedBudget && !m_resolvedOrder.empty())
    {
        const uint64_t key = m_resolvedOrder.front();
        m_resolvedOrder.pop_front();
        const auto it = m_resolved.find(key);
        if (it == m_resolved.end())
            continue;
        SDL_ReleaseGPUTexture(device, it->second.texture);
        m_resolvedBytes -= it->second.bytes;
        m_resolved.erase(it);
    }
}

void Renderer::evict(SDL_GPUDevice *device)
{
    std::vector<Id> gone;
    while (m_imageBytes > kImageBudget && !m_imageOrder.empty())
    {
        const Id id = m_imageOrder.front();
        m_imageOrder.pop_front();
        const auto it = m_images.find(id);
        if (it == m_images.end())
            continue;
        SDL_ReleaseGPUTexture(device, it->second.texture);
        m_imageBytes -= it->second.bytes;
        m_images.erase(it);
        invalidateResolved(device, id, false);
        m_recent.erase(std::remove(m_recent.begin(), m_recent.end(), id), m_recent.end());
        gone.push_back(id);
        ++m_evicted;
    }
    if (gone.empty())
        return;
    Queue &q = queue();
    std::lock_guard lock(q.mutex);
    q.evicted.insert(q.evicted.end(), gone.begin(), gone.end());
    q.evictedPending.store(true, std::memory_order_release);
}

bool Renderer::ensureView(SDL_GPUDevice *device, std::string &error)
{
    if (m_viewPipeline)
        return true;
    if (m_viewFailed)
        return false;
    m_viewFailed = true; // until it is made
    // SPIR-V only so far (no MSL build of these shaders): no view elsewhere.
    if (!(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV))
        return false;
    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_viewSampler = SDL_CreateGPUSampler(device, &samplerInfo);
    if (!m_viewSampler)
    {
        error = std::string("SDL_CreateGPUSampler(native view): ") + SDL_GetError();
        return false;
    }
    SDL_GPUShaderCreateInfo vertexInfo{};
    vertexInfo.code = reinterpret_cast<const uint8_t *>(kNativeViewVertSpirv);
    vertexInfo.code_size = sizeof(kNativeViewVertSpirv);
    vertexInfo.entrypoint = "main";
    vertexInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexInfo.num_uniform_buffers = 1u;
    SDL_GPUShaderCreateInfo fragmentInfo{};
    fragmentInfo.code = reinterpret_cast<const uint8_t *>(kNativeViewFragSpirv);
    fragmentInfo.code_size = sizeof(kNativeViewFragSpirv);
    fragmentInfo.entrypoint = "main";
    fragmentInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragmentInfo.num_samplers = 2u;
    fragmentInfo.num_uniform_buffers = 1u;
    SDL_GPUShader *vertex = SDL_CreateGPUShader(device, &vertexInfo);
    SDL_GPUShader *fragment = vertex ? SDL_CreateGPUShader(device, &fragmentInfo) : nullptr;
    if (!vertex || !fragment)
    {
        error = std::string("SDL_CreateGPUShader(native view): ") + SDL_GetError();
        if (vertex)
            SDL_ReleaseGPUShader(device, vertex);
        return false;
    }
    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    info.target_info.color_target_descriptions = &colorTarget;
    info.target_info.num_color_targets = 1u;
    m_viewPipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    SDL_ReleaseGPUShader(device, vertex);
    SDL_ReleaseGPUShader(device, fragment);
    if (!m_viewPipeline)
    {
        error = std::string("SDL_CreateGPUGraphicsPipeline(native view): ") + SDL_GetError();
        return false;
    }
    m_viewFailed = false;
    return true;
}

// The newest textures, newest first, each fitted into a cell of the grid.
void Renderer::drawView(SDL_GPUCommandBuffer *commands, SDL_GPURenderPass *pass)
{
    SDL_BindGPUGraphicsPipeline(pass, m_viewPipeline);
    const float cellW = 2.0f / kViewColumns, cellH = 2.0f / kViewRows;
    const float aspect = float(m_sceneWidth) / float(std::max(m_sceneHeight, 1u));
    uint32_t cell = 0u;
    for (const Id id : m_recent)
    {
        const auto it = m_images.find(id);
        if (it == m_images.end())
            continue;
        const Image &image = it->second;
        uint32_t mode[4] = {0u, 0u, 0u, 0u};
        SDL_GPUTexture *palette = image.texture;
        if (image.format == PixelFormat::Index8)
        {
            const auto p = m_palettes.find(image.palette);
            if (p != m_palettes.end())
            {
                mode[0] = 1u;
                mode[1] = p->second.width;
                palette = p->second.texture;
            }
            else
                mode[0] = 2u;
        }
        // The cell, less a margin, then the texture's shape inside it (in
        // clip space, whose x and y units differ by the scene's aspect).
        const float x0 = -1.0f + (cell % kViewColumns) * cellW, top = 1.0f - (cell / kViewColumns) * cellH;
        const float margin = 0.08f;
        float w = cellW * (1.0f - margin), h = cellH * (1.0f - margin);
        const float shape = float(image.width) / float(std::max(image.height, 1u)); // width over height
        if (shape * h / aspect > w)
            h = w * aspect / shape;
        else
            w = shape * h / aspect;
        const float cx = x0 + cellW * 0.5f, cy = top - cellH * 0.5f;
        const float rect[4] = {cx - w * 0.5f, cy + h * 0.5f, cx + w * 0.5f, cy - h * 0.5f};
        SDL_PushGPUVertexUniformData(commands, 0, rect, sizeof(rect));
        SDL_PushGPUFragmentUniformData(commands, 0, mode, sizeof(mode));
        const SDL_GPUTextureSamplerBinding bindings[2] = {{image.texture, m_viewSampler}, {palette, m_viewSampler}};
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
        SDL_DrawGPUPrimitives(pass, 4, 1, 0, 0);
        if (++cell == kViewColumns * kViewRows)
            break;
    }
}

bool Renderer::render(SDL_GPUDevice *device, SDL_GPUCommandBuffer *commands, uint32_t scale, Frame &out,
                      std::string &error)
{
    {
        Queue &q = queue();
        std::lock_guard lock(q.mutex);
        m_drained.swap(q.records);
        m_drainedVerts.swap(q.meshVertices);
        m_drainedIndices.swap(q.meshIndices);
    }
    // Fast path: when m_meshes is empty and no MeshDraw appears after a Present
    // in this batch (the steady-state 1-frame-per-render case), swap vertex and
    // index staging buffers directly into m_meshVerts/m_meshIndices (zero copy).
    bool inPlaceMeshes = m_meshes.empty() && m_meshVerts.empty() && m_meshIndices.empty();
    if (inPlaceMeshes)
    {
        bool seenPresent = false;
        for (const Record &rec : m_drained)
        {
            if (std::holds_alternative<Present>(rec))
                seenPresent = true;
            else if (seenPresent && std::holds_alternative<MeshDraw>(rec))
            {
                inPlaceMeshes = false;
                break;
            }
        }
    }
    if (inPlaceMeshes)
    {
        m_meshVerts.swap(m_drainedVerts);
        m_meshIndices.swap(m_drainedIndices);
    }
    else
    {
        m_meshVerts.reserve(m_meshVerts.size() + m_drainedVerts.size());
        m_meshIndices.reserve(m_meshIndices.size() + m_drainedIndices.size());
    }
    // A texture the GPU refuses is counted and left out, not the frame lost.
    static bool told = false;
    for (Record &record : m_drained)
    {
        std::string refused;
        apply(device, record, inPlaceMeshes, refused);
        if (!refused.empty())
        {
            ++m_refused;
            if (!told)
                std::fprintf(stderr, "[native-pipeline] refused: %s\n", refused.c_str());
            told = true;
        }
    }
    m_drained.clear();
    m_drainedVerts.clear();
    m_drainedIndices.clear();
    std::string meshError;
    const bool meshes = ensureMeshes(device, meshError);
    static bool toldMeshes = false;
    if (!meshes && !toldMeshes)
    {
        std::fprintf(stderr, "[native-pipeline] no mesh draws: %s\n", meshError.c_str());
        toldMeshes = true;
    }
    // Targets a save state put back: their pixels go up as textures and are
    // drawn into the targets, at the target's scale, before any draw.
    struct FillBlit
    {
        SDL_GPUTexture *texture;
        Id id;
        uint32_t width, height;
    };
    std::vector<FillBlit> fillBlits;
    for (TargetFill &fill : m_fills)
    {
        std::string failed;
        SDL_GPUTexture *texture =
            createTexture(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, fill.width, fill.height, 1u,
                          SDL_GPU_TEXTUREUSAGE_SAMPLER, failed);
        if (!texture)
            continue;
        m_uploads.push_back({texture, fill.width, fill.height, fill.rgba, false});
        fillBlits.push_back({texture, fill.id, fill.width, fill.height});
    }
    m_fills.clear();
    if (std::string failed; !upload(device, commands, failed))
    {
        std::fprintf(stderr, "[native-pipeline] uploads lost: %s\n", failed.c_str());
        m_uploads.clear();
    }
    for (const FillBlit &fill : fillBlits)
    {
        const uint32_t s = std::max(scale, 1u);
        bool fresh = false;
        std::string failed;
        SDL_GPUTexture *target =
            targetTexture(device, fill.id, false, fill.width * s, fill.height * s, s, fresh, failed);
        if (target)
        {
            SDL_GPUBlitInfo blit{};
            blit.source.texture = fill.texture;
            blit.source.w = fill.width;
            blit.source.h = fill.height;
            blit.destination.texture = target;
            blit.destination.w = fill.width * s;
            blit.destination.h = fill.height * s;
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = SDL_GPU_FILTER_NEAREST;
            SDL_BlitGPUTexture(commands, &blit);
        }
        SDL_ReleaseGPUTexture(device, fill.texture);
    }
    evict(device);
    // The depth reads, from the last frame drawn (submitted ahead of this
    // one's draws).
    serveDepth(device);

    uint32_t width = kDefaultWidth, height = kDefaultHeight;
    if (const auto t = m_targets.find(m_sceneTarget); t != m_targets.end() && t->second.width && t->second.height)
    {
        width = t->second.width;
        height = t->second.height;
    }
    scale = scale ? scale : 1u;
    width *= scale;
    height *= scale;
    bool fresh = false; // the scene target holds nothing yet
    if (!m_scene || m_sceneWidth != width || m_sceneHeight != height)
    {
        fresh = true;
        if (m_scene)
            SDL_ReleaseGPUTexture(device, m_scene);
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        m_scene = SDL_CreateGPUTexture(device, &info);
        if (!m_scene)
        {
            error = std::string("SDL_CreateGPUTexture(native scene): ") + SDL_GetError();
            return false;
        }
        m_sceneWidth = width;
        m_sceneHeight = height;
    }

    const bool remaster = remasterEnabled();
    if (remaster != m_lastRemaster && !m_frameMeshes.empty())
        m_frameReady = true;
    m_lastRemaster = remaster;

    // The scene: the meshes of the last frame a Present record closed, over
    // black, with a reversed depth target of its own. Kept as it is until
    // the next frame closes (the host presents more often than the game).
    static const bool grid = [] {
        const char *v = std::getenv("RG_NATIVE_TEXTURE_GRID");
        return v && *v && *v != '0';
    }();
    const bool draw = meshes && m_frameReady;
    if (!draw && !fresh && !grid)
    {
        if (remaster && m_casTarget && m_casWidth == m_sceneWidth && m_casHeight == m_sceneHeight)
        {
            out.texture = m_casTarget;
            out.width = m_casWidth;
            out.height = m_casHeight;
            return true;
        }
        out.texture = m_scene;
        out.width = m_sceneWidth;
        out.height = m_sceneHeight;
        return true;
    }
    bool drawn = false;
    if (draw)
    {
        std::string failed;
        drawn = drawFrame(device, commands, scale, failed);
        if (drawn)
            m_frameScale = scale;
        if (!drawn)
            std::fprintf(stderr, "[native-pipeline] frame lost: %s\n", failed.c_str());
    }
    m_frameReady = false;
    std::string viewError;
    const bool view = grid && ensureView(device, viewError);
    if (!viewError.empty())
        std::fprintf(stderr, "[native-pipeline] no texture view: %s\n", viewError.c_str());
    if (!drawn || view)
    {
        // A new scene target with no frame for it yet is cleared; the grid
        // goes over whatever the scene holds.
        SDL_GPUColorTargetInfo target{};
        target.texture = m_scene;
        target.load_op = drawn ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        target.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        if (!pass)
        {
            error = std::string("SDL_BeginGPURenderPass(native scene): ") + SDL_GetError();
            return false;
        }
        if (view)
            drawView(commands, pass);
        SDL_EndGPURenderPass(pass);
    }

    if (remaster && !view)
    {
        std::string casError;
        if (applyCas(device, commands, m_scene, m_sceneWidth, m_sceneHeight, casError))
        {
            out.texture = m_casTarget;
            out.width = m_casWidth;
            out.height = m_casHeight;
            return true;
        }
        if (!casError.empty())
            std::fprintf(stderr, "[native-pipeline] CAS skipped: %s\n", casError.c_str());
    }

    out.texture = m_scene;
    out.width = m_sceneWidth;
    out.height = m_sceneHeight;
    return true;
}

bool Renderer::ensureCas(SDL_GPUDevice *device, std::string &error)
{
    if (m_casPipeline && m_fxaaPipeline)
        return true;
    if (m_casFailed)
        return false;
    m_casFailed = true;
    if (!(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV))
        return false;
    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_casSampler = SDL_CreateGPUSampler(device, &samplerInfo);
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    m_fxaaSampler = SDL_CreateGPUSampler(device, &samplerInfo);
    if (!m_casSampler || !m_fxaaSampler)
    {
        error = std::string("SDL_CreateGPUSampler(native remaster post): ") + SDL_GetError();
        return false;
    }
    SDL_GPUShaderCreateInfo vertexInfo{};
    vertexInfo.code = reinterpret_cast<const uint8_t *>(kNativeCasVertSpirv);
    vertexInfo.code_size = sizeof(kNativeCasVertSpirv);
    vertexInfo.entrypoint = "main";
    vertexInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    SDL_GPUShaderCreateInfo casFragInfo{};
    casFragInfo.code = reinterpret_cast<const uint8_t *>(kNativeCasFragSpirv);
    casFragInfo.code_size = sizeof(kNativeCasFragSpirv);
    casFragInfo.entrypoint = "main";
    casFragInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    casFragInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    casFragInfo.num_samplers = 1u;
    casFragInfo.num_uniform_buffers = 1u;
    SDL_GPUShaderCreateInfo fxaaFragInfo{};
    fxaaFragInfo.code = reinterpret_cast<const uint8_t *>(kNativeFxaaFragSpirv);
    fxaaFragInfo.code_size = sizeof(kNativeFxaaFragSpirv);
    fxaaFragInfo.entrypoint = "main";
    fxaaFragInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fxaaFragInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fxaaFragInfo.num_samplers = 1u;
    SDL_GPUShader *vertex = SDL_CreateGPUShader(device, &vertexInfo);
    SDL_GPUShader *casFrag = vertex ? SDL_CreateGPUShader(device, &casFragInfo) : nullptr;
    SDL_GPUShader *fxaaFrag = casFrag ? SDL_CreateGPUShader(device, &fxaaFragInfo) : nullptr;
    if (!vertex || !casFrag || !fxaaFrag)
    {
        error = std::string("SDL_CreateGPUShader(native remaster post): ") + SDL_GetError();
        if (vertex)
            SDL_ReleaseGPUShader(device, vertex);
        if (casFrag)
            SDL_ReleaseGPUShader(device, casFrag);
        if (fxaaFrag)
            SDL_ReleaseGPUShader(device, fxaaFrag);
        return false;
    }
    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = casFrag;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.target_info.color_target_descriptions = &colorTarget;
    info.target_info.num_color_targets = 1u;
    m_casPipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    info.fragment_shader = fxaaFrag;
    m_fxaaPipeline = m_casPipeline ? SDL_CreateGPUGraphicsPipeline(device, &info) : nullptr;
    SDL_ReleaseGPUShader(device, fxaaFrag);
    SDL_ReleaseGPUShader(device, casFrag);
    SDL_ReleaseGPUShader(device, vertex);
    if (!m_casPipeline || !m_fxaaPipeline)
    {
        error = std::string("SDL_CreateGPUGraphicsPipeline(native remaster post): ") + SDL_GetError();
        return false;
    }
    m_casFailed = false;
    return true;
}

bool Renderer::ensureXbr(SDL_GPUDevice *device, std::string &error)
{
    if (m_xbrPipeline && m_xbr2dPipeline && m_casSampler)
        return true;
    if (m_xbrFailed)
        return false;
    m_xbrFailed = true;
    if (!(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV))
        return false;
    if (!m_casSampler)
    {
        SDL_GPUSamplerCreateInfo samplerInfo{};
        samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
        samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
        samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        m_casSampler = SDL_CreateGPUSampler(device, &samplerInfo);
        if (!m_casSampler)
        {
            error = std::string("SDL_CreateGPUSampler(native xbr): ") + SDL_GetError();
            return false;
        }
    }
    SDL_GPUShaderCreateInfo vertexInfo{};
    vertexInfo.code = reinterpret_cast<const uint8_t *>(kNativeCasVertSpirv);
    vertexInfo.code_size = sizeof(kNativeCasVertSpirv);
    vertexInfo.entrypoint = "main";
    vertexInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    SDL_GPUShaderCreateInfo fragInfo{};
    fragInfo.code = reinterpret_cast<const uint8_t *>(kNativeUpscale2xFragSpirv);
    fragInfo.code_size = sizeof(kNativeUpscale2xFragSpirv);
    fragInfo.entrypoint = "main";
    fragInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fragInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragInfo.num_samplers = 1u;
    fragInfo.num_uniform_buffers = 1u;
    SDL_GPUShaderCreateInfo frag2dInfo = fragInfo;
    frag2dInfo.code = reinterpret_cast<const uint8_t *>(kNativeUpscale2xbrFragSpirv);
    frag2dInfo.code_size = sizeof(kNativeUpscale2xbrFragSpirv);
    SDL_GPUShader *vertex = SDL_CreateGPUShader(device, &vertexInfo);
    SDL_GPUShader *frag = vertex ? SDL_CreateGPUShader(device, &fragInfo) : nullptr;
    SDL_GPUShader *frag2d = frag ? SDL_CreateGPUShader(device, &frag2dInfo) : nullptr;
    if (!vertex || !frag || !frag2d)
    {
        error = std::string("SDL_CreateGPUShader(native upscale2x/2xbr): ") + SDL_GetError();
        if (vertex)
            SDL_ReleaseGPUShader(device, vertex);
        if (frag)
            SDL_ReleaseGPUShader(device, frag);
        if (frag2d)
            SDL_ReleaseGPUShader(device, frag2d);
        return false;
    }
    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = frag;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.target_info.color_target_descriptions = &colorTarget;
    info.target_info.num_color_targets = 1u;
    m_xbrPipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    info.fragment_shader = frag2d;
    m_xbr2dPipeline = m_xbrPipeline ? SDL_CreateGPUGraphicsPipeline(device, &info) : nullptr;
    SDL_ReleaseGPUShader(device, frag2d);
    SDL_ReleaseGPUShader(device, frag);
    SDL_ReleaseGPUShader(device, vertex);
    if (!m_xbrPipeline || !m_xbr2dPipeline)
    {
        error = std::string("SDL_CreateGPUGraphicsPipeline(native upscale2x/2xbr): ") + SDL_GetError();
        return false;
    }
    m_xbrFailed = false;
    return true;
}

bool Renderer::ensureCnn(SDL_GPUDevice *device, std::string &error)
{
    if (m_cnnD2sPipeline && m_cnnLastPipeline && m_cnnConvPipelines[6])
        return true;
    if (m_cnnFailed)
        return false;
    m_cnnFailed = true;
    if (!(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV))
        return false;
    if (!SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                      SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER))
        return false;
    if (!m_casSampler)
    {
        std::string xbrErr;
        if (!ensureXbr(device, xbrErr))
            return false;
    }

    SDL_GPUShaderCreateInfo vertexInfo{};
    vertexInfo.code = reinterpret_cast<const uint8_t *>(kNativeCasVertSpirv);
    vertexInfo.code_size = sizeof(kNativeCasVertSpirv);
    vertexInfo.entrypoint = "main";
    vertexInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    SDL_GPUShader *vertex = SDL_CreateGPUShader(device, &vertexInfo);
    if (!vertex)
    {
        error = std::string("SDL_CreateGPUShader(native cnn vert): ") + SDL_GetError();
        return false;
    }

    auto makePipeline = [&](const void *code, size_t size, uint32_t numSamplers, uint32_t numUniforms,
                            SDL_GPUTextureFormat outFormat) -> SDL_GPUGraphicsPipeline * {
        SDL_GPUShaderCreateInfo fi{};
        fi.code = reinterpret_cast<const uint8_t *>(code);
        fi.code_size = size;
        fi.entrypoint = "main";
        fi.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fi.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fi.num_samplers = numSamplers;
        fi.num_uniform_buffers = numUniforms;
        SDL_GPUShader *frag = SDL_CreateGPUShader(device, &fi);
        if (!frag)
            return nullptr;
        SDL_GPUColorTargetDescription ct{};
        ct.format = outFormat;
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vertex;
        pi.fragment_shader = frag;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1u;
        SDL_GPUGraphicsPipeline *pipe = SDL_CreateGPUGraphicsPipeline(device, &pi);
        SDL_ReleaseGPUShader(device, frag);
        return pipe;
    };

    const struct
    {
        const void *code;
        size_t size;
    } convShaders[7] = {
        {kNativeCnnConv0FragSpirv, sizeof(kNativeCnnConv0FragSpirv)},
        {kNativeCnnConv1FragSpirv, sizeof(kNativeCnnConv1FragSpirv)},
        {kNativeCnnConv2FragSpirv, sizeof(kNativeCnnConv2FragSpirv)},
        {kNativeCnnConv3FragSpirv, sizeof(kNativeCnnConv3FragSpirv)},
        {kNativeCnnConv4FragSpirv, sizeof(kNativeCnnConv4FragSpirv)},
        {kNativeCnnConv5FragSpirv, sizeof(kNativeCnnConv5FragSpirv)},
        {kNativeCnnConv6FragSpirv, sizeof(kNativeCnnConv6FragSpirv)},
    };
    bool ok = true;
    for (int i = 0; i < 7; ++i)
    {
        m_cnnConvPipelines[i] =
            makePipeline(convShaders[i].code, convShaders[i].size, 1u, 1u, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
        if (!m_cnnConvPipelines[i])
        {
            ok = false;
            break;
        }
    }
    if (ok)
        m_cnnLastPipeline = makePipeline(kNativeCnnLastFragSpirv, sizeof(kNativeCnnLastFragSpirv), 7u, 0u,
                                         SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    if (ok && m_cnnLastPipeline)
        m_cnnD2sPipeline = makePipeline(kNativeCnnD2sFragSpirv, sizeof(kNativeCnnD2sFragSpirv), 2u, 1u,
                                        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    SDL_ReleaseGPUShader(device, vertex);
    if (!ok || !m_cnnLastPipeline || !m_cnnD2sPipeline)
    {
        error = std::string("SDL_CreateGPUGraphicsPipeline(native cnn): ") + SDL_GetError();
        return false;
    }
    m_cnnFailed = false;
    m_cnnSupported = true;
    return true;
}

bool Renderer::ensureCnnScratch(SDL_GPUDevice *device, uint32_t width, uint32_t height)
{
    if (m_cnnScratch[0] && m_cnnScratchW >= width && m_cnnScratchH >= height)
        return true;
    const uint32_t newW = std::max(m_cnnScratchW, width);
    const uint32_t newH = std::max(m_cnnScratchH, height);
    for (SDL_GPUTexture *&t : m_cnnScratch)
    {
        if (t)
            SDL_ReleaseGPUTexture(device, t);
        t = nullptr;
    }
    std::string err;
    for (SDL_GPUTexture *&t : m_cnnScratch)
    {
        t = createTexture(device, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, newW, newH, 1u,
                          SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, err);
        if (!t)
        {
            m_cnnScratchW = m_cnnScratchH = 0u;
            return false;
        }
    }
    m_cnnScratchW = newW;
    m_cnnScratchH = newH;
    return true;
}

void Renderer::runCnnUpscale(SDL_GPUDevice *device, SDL_GPUCommandBuffer *commands, SDL_GPUTexture *src,
                             SDL_GPUTexture *dst, uint32_t srcW, uint32_t srcH, bool clampU, bool clampV)
{
    if (!ensureCnnScratch(device, srcW, srcH))
        return;
    const SDL_GPUViewport srcVp{0.0f, 0.0f, float(srcW), float(srcH), 0.0f, 1.0f};
    const uint32_t wrap[4] = {clampU ? 1u : 0u, clampV ? 1u : 0u, srcW, srcH};

    // Passes 0..6: 4x3x3 CReLU convolution stack at source resolution.
    for (int i = 0; i < 7; ++i)
    {
        SDL_GPUColorTargetInfo target{};
        target.texture = m_cnnScratch[i];
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &target, 1u, nullptr);
        if (!pass)
            return;
        SDL_SetGPUViewport(pass, &srcVp);
        SDL_BindGPUGraphicsPipeline(pass, m_cnnConvPipelines[i]);
        const SDL_GPUTextureSamplerBinding binding{i == 0 ? src : m_cnnScratch[i - 1], m_casSampler};
        SDL_BindGPUFragmentSamplers(pass, 0u, &binding, 1u);
        SDL_PushGPUFragmentUniformData(commands, 0u, wrap, sizeof(wrap));
        SDL_DrawGPUPrimitives(pass, 3u, 1u, 0u, 0u);
        SDL_EndGPURenderPass(pass);
    }

    // Pass 7: 1x1 dense skip-connection aggregation across conv0..conv6 -> conv_last.
    {
        SDL_GPUColorTargetInfo target{};
        target.texture = m_cnnScratch[7];
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &target, 1u, nullptr);
        if (!pass)
            return;
        SDL_SetGPUViewport(pass, &srcVp);
        SDL_BindGPUGraphicsPipeline(pass, m_cnnLastPipeline);
        SDL_GPUTextureSamplerBinding bindings[7];
        for (int i = 0; i < 7; ++i)
            bindings[i] = {m_cnnScratch[i], m_casSampler};
        SDL_BindGPUFragmentSamplers(pass, 0u, bindings, 7u);
        SDL_DrawGPUPrimitives(pass, 3u, 1u, 0u, 0u);
        SDL_EndGPURenderPass(pass);
    }

    // Pass 8: 2x Depth-to-Space + premultiplied Catmull-Rom base -> dst (2*srcW x 2*srcH).
    {
        SDL_GPUColorTargetInfo target{};
        target.texture = dst;
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &target, 1u, nullptr);
        if (!pass)
            return;
        const SDL_GPUViewport dstVp{0.0f, 0.0f, float(srcW * 2u), float(srcH * 2u), 0.0f, 1.0f};
        SDL_SetGPUViewport(pass, &dstVp);
        SDL_BindGPUGraphicsPipeline(pass, m_cnnD2sPipeline);
        const SDL_GPUTextureSamplerBinding bindings[2] = {{src, m_casSampler}, {m_cnnScratch[7], m_casSampler}};
        SDL_BindGPUFragmentSamplers(pass, 0u, bindings, 2u);
        SDL_PushGPUFragmentUniformData(commands, 0u, wrap, sizeof(wrap));
        SDL_DrawGPUPrimitives(pass, 3u, 1u, 0u, 0u);
        SDL_EndGPURenderPass(pass);
    }
}

void Renderer::pollAiCache(SDL_GPUDevice *device, SDL_GPUCommandBuffer *commands)
{
    if (!m_aiCache.enabled())
        return;
    std::vector<AiTextureCache::ReadyTexture> ready;
    m_aiCache.takeReady(ready);
    if (ready.empty())
        return;

    for (AiTextureCache::ReadyTexture &tex : ready)
    {
        auto it = m_resolved.find(tex.resolvedKey);
        if (it == m_resolved.end() || tex.mips.empty())
            continue;
        const uint32_t mipCount = static_cast<uint32_t>(tex.mips.size());
        std::string err;
        SDL_GPUTexture *bc7Tex = createTexture(device, SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM, tex.width, tex.height,
                                               mipCount, SDL_GPU_TEXTUREUSAGE_SAMPLER, err);
        if (!bc7Tex)
            continue;

        uint64_t totalBytes = 0u;
        for (const auto &lvl : tex.mips)
            totalBytes += lvl.bc7Data.size();

        SDL_GPUTransferBufferCreateInfo tbInfo{};
        tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbInfo.size = static_cast<uint32_t>(totalBytes);
        SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
        if (!tb)
        {
            SDL_ReleaseGPUTexture(device, bc7Tex);
            continue;
        }
        auto *mapped = static_cast<uint8_t *>(SDL_MapGPUTransferBuffer(device, tb, false));
        if (!mapped)
        {
            SDL_ReleaseGPUTransferBuffer(device, tb);
            SDL_ReleaseGPUTexture(device, bc7Tex);
            continue;
        }
        uint32_t offset = 0u;
        for (const auto &lvl : tex.mips)
        {
            std::memcpy(mapped + offset, lvl.bc7Data.data(), lvl.bc7Data.size());
            offset += static_cast<uint32_t>(lvl.bc7Data.size());
        }
        SDL_UnmapGPUTransferBuffer(device, tb);

        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
        offset = 0u;
        for (uint32_t m = 0; m < mipCount; ++m)
        {
            const auto &lvl = tex.mips[m];
            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = tb;
            src.offset = offset;
            SDL_GPUTextureRegion dst{};
            dst.texture = bc7Tex;
            dst.mip_level = m;
            dst.w = lvl.width;
            dst.h = lvl.height;
            dst.d = 1u;
            SDL_UploadToGPUTexture(copy, &src, &dst, false);
            offset += static_cast<uint32_t>(lvl.bc7Data.size());
        }
        SDL_EndGPUCopyPass(copy);
        SDL_ReleaseGPUTransferBuffer(device, tb);

        // Hot-swap into m_resolved, freeing the temporary 2x RGBA8 texture.
        SDL_ReleaseGPUTexture(device, it->second.texture);
        m_resolvedBytes -= it->second.bytes;
        it->second.texture = bc7Tex;
        it->second.width = tex.width;
        it->second.height = tex.height;
        it->second.bytes = totalBytes;
        m_resolvedBytes += totalBytes;
    }
}

bool Renderer::ensureSsao(SDL_GPUDevice *device, std::string &error)
{
    if (m_ssaoPipeline && m_ssaoBlurPipeline && m_ssaoCompositePipeline)
        return true;
    if (m_ssaoFailed)
        return false;
    m_ssaoFailed = true;
    if (!(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV))
        return false;
    if (!ensureCas(device, error))
        return false;
    SDL_GPUShaderCreateInfo vertexInfo{};
    vertexInfo.code = reinterpret_cast<const uint8_t *>(kNativeCasVertSpirv);
    vertexInfo.code_size = sizeof(kNativeCasVertSpirv);
    vertexInfo.entrypoint = "main";
    vertexInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    SDL_GPUShaderCreateInfo ssaoInfo{};
    ssaoInfo.code = reinterpret_cast<const uint8_t *>(kNativeSsaoFragSpirv);
    ssaoInfo.code_size = sizeof(kNativeSsaoFragSpirv);
    ssaoInfo.entrypoint = "main";
    ssaoInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    ssaoInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    ssaoInfo.num_samplers = 1u;
    SDL_GPUShaderCreateInfo blurInfo{};
    blurInfo.code = reinterpret_cast<const uint8_t *>(kNativeSsaoBlurFragSpirv);
    blurInfo.code_size = sizeof(kNativeSsaoBlurFragSpirv);
    blurInfo.entrypoint = "main";
    blurInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    blurInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    blurInfo.num_samplers = 2u;
    blurInfo.num_uniform_buffers = 1u;
    SDL_GPUShader *vertex = SDL_CreateGPUShader(device, &vertexInfo);
    SDL_GPUShader *ssaoFrag = vertex ? SDL_CreateGPUShader(device, &ssaoInfo) : nullptr;
    SDL_GPUShader *blurFrag = ssaoFrag ? SDL_CreateGPUShader(device, &blurInfo) : nullptr;
    if (!vertex || !ssaoFrag || !blurFrag)
    {
        error = std::string("SDL_CreateGPUShader(native ssao): ") + SDL_GetError();
        if (vertex)
            SDL_ReleaseGPUShader(device, vertex);
        if (ssaoFrag)
            SDL_ReleaseGPUShader(device, ssaoFrag);
        if (blurFrag)
            SDL_ReleaseGPUShader(device, blurFrag);
        return false;
    }
    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = ssaoFrag;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.target_info.color_target_descriptions = &colorTarget;
    info.target_info.num_color_targets = 1u;
    m_ssaoPipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    info.fragment_shader = blurFrag;
    m_ssaoBlurPipeline = m_ssaoPipeline ? SDL_CreateGPUGraphicsPipeline(device, &info) : nullptr;
    // Composite pass: multiply AO onto m_scene RGB while preserving m_scene alpha.
    colorTarget.blend_state.enable_blend = true;
    colorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR;
    colorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    colorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    colorTarget.blend_state.enable_color_write_mask = true;
    colorTarget.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
    m_ssaoCompositePipeline = m_ssaoBlurPipeline ? SDL_CreateGPUGraphicsPipeline(device, &info) : nullptr;
    SDL_ReleaseGPUShader(device, blurFrag);
    SDL_ReleaseGPUShader(device, ssaoFrag);
    SDL_ReleaseGPUShader(device, vertex);
    if (!m_ssaoPipeline || !m_ssaoBlurPipeline || !m_ssaoCompositePipeline)
    {
        error = std::string("SDL_CreateGPUGraphicsPipeline(native ssao): ") + SDL_GetError();
        return false;
    }
    m_ssaoFailed = false;
    return true;
}

void Renderer::applySsao(SDL_GPUDevice *device, SDL_GPUCommandBuffer *commands, SDL_GPUTexture *sceneColor,
                         SDL_GPUTexture *sceneDepth, uint32_t width, uint32_t height)
{
    if (!sceneColor || !sceneDepth || width < 4u || height < 4u)
        return;
    std::string error;
    if (!ensureSsao(device, error))
        return;
    const uint32_t hw = std::max(width / 2u, 1u);
    const uint32_t hh = std::max(height / 2u, 1u);
    if (!m_ssaoRaw || !m_ssaoBlur || m_ssaoWidth != hw || m_ssaoHeight != hh)
    {
        if (m_ssaoRaw)
            SDL_ReleaseGPUTexture(device, m_ssaoRaw);
        if (m_ssaoBlur)
            SDL_ReleaseGPUTexture(device, m_ssaoBlur);
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = hw;
        info.height = hh;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        m_ssaoRaw = SDL_CreateGPUTexture(device, &info);
        m_ssaoBlur = m_ssaoRaw ? SDL_CreateGPUTexture(device, &info) : nullptr;
        if (!m_ssaoRaw || !m_ssaoBlur)
        {
            m_ssaoWidth = m_ssaoHeight = 0u;
            return;
        }
        m_ssaoWidth = hw;
        m_ssaoHeight = hh;
    }
    const SDL_GPUViewport halfVp{0.0f, 0.0f, float(hw), float(hh), 0.0f, 1.0f};
    const SDL_GPUViewport fullVp{0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f};
    // Pass 1: half-res SSAO from SceneDepth.
    {
        SDL_GPUColorTargetInfo target{};
        target.texture = m_ssaoRaw;
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &target, 1u, nullptr);
        if (!pass)
            return;
        SDL_SetGPUViewport(pass, &halfVp);
        SDL_BindGPUGraphicsPipeline(pass, m_ssaoPipeline);
        const SDL_GPUTextureSamplerBinding binding{sceneDepth, m_casSampler};
        SDL_BindGPUFragmentSamplers(pass, 0u, &binding, 1u);
        SDL_DrawGPUPrimitives(pass, 3u, 1u, 0u, 0u);
        SDL_EndGPURenderPass(pass);
    }
    // Pass 2: horizontal bilateral blur (m_ssaoRaw -> m_ssaoBlur).
    {
        SDL_GPUColorTargetInfo target{};
        target.texture = m_ssaoBlur;
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &target, 1u, nullptr);
        if (!pass)
            return;
        SDL_SetGPUViewport(pass, &halfVp);
        SDL_BindGPUGraphicsPipeline(pass, m_ssaoBlurPipeline);
        const SDL_GPUTextureSamplerBinding bindings[2] = {{m_ssaoRaw, m_fxaaSampler}, {sceneDepth, m_casSampler}};
        SDL_BindGPUFragmentSamplers(pass, 0u, bindings, 2u);
        const float dir[4] = {1.0f / float(hw), 0.0f, 1.0f / float(hw), 1.0f / float(hh)};
        SDL_PushGPUFragmentUniformData(commands, 0u, dir, sizeof(dir));
        SDL_DrawGPUPrimitives(pass, 3u, 1u, 0u, 0u);
        SDL_EndGPURenderPass(pass);
    }
    // Pass 3: vertical bilateral blur + multiplicative composite onto m_scene (before UI/HUD).
    {
        SDL_GPUColorTargetInfo target{};
        target.texture = sceneColor;
        target.load_op = SDL_GPU_LOADOP_LOAD;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &target, 1u, nullptr);
        if (!pass)
            return;
        SDL_SetGPUViewport(pass, &fullVp);
        SDL_BindGPUGraphicsPipeline(pass, m_ssaoCompositePipeline);
        const SDL_GPUTextureSamplerBinding bindings[2] = {{m_ssaoBlur, m_fxaaSampler}, {sceneDepth, m_casSampler}};
        SDL_BindGPUFragmentSamplers(pass, 0u, bindings, 2u);
        const float dir[4] = {0.0f, 1.0f / float(hh), 1.0f / float(width), 1.0f / float(height)};
        SDL_PushGPUFragmentUniformData(commands, 0u, dir, sizeof(dir));
        SDL_DrawGPUPrimitives(pass, 3u, 1u, 0u, 0u);
        SDL_EndGPURenderPass(pass);
    }
}

bool Renderer::applyCas(SDL_GPUDevice *device, SDL_GPUCommandBuffer *commands, SDL_GPUTexture *source, uint32_t width,
                        uint32_t height, std::string &error)
{
    if (!source || width == 0u || height == 0u || !ensureCas(device, error))
        return false;
    if (!m_casTarget || !m_fxaaTarget || m_casWidth != width || m_casHeight != height)
    {
        if (m_casTarget)
            SDL_ReleaseGPUTexture(device, m_casTarget);
        if (m_fxaaTarget)
            SDL_ReleaseGPUTexture(device, m_fxaaTarget);
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        m_fxaaTarget = SDL_CreateGPUTexture(device, &info);
        m_casTarget = m_fxaaTarget ? SDL_CreateGPUTexture(device, &info) : nullptr;
        if (!m_fxaaTarget || !m_casTarget)
        {
            error = std::string("SDL_CreateGPUTexture(native remaster post): ") + SDL_GetError();
            m_casWidth = m_casHeight = 0u;
            return false;
        }
        m_casWidth = width;
        m_casHeight = height;
    }
    const SDL_GPUViewport viewport{0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f};
    // Pass 1: FXAA 3.11 Quality (m_scene -> m_fxaaTarget).
    {
        SDL_GPUColorTargetInfo target{};
        target.texture = m_fxaaTarget;
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &target, 1u, nullptr);
        if (!pass)
        {
            error = std::string("SDL_BeginGPURenderPass(native fxaa): ") + SDL_GetError();
            return false;
        }
        SDL_SetGPUViewport(pass, &viewport);
        SDL_BindGPUGraphicsPipeline(pass, m_fxaaPipeline);
        const SDL_GPUTextureSamplerBinding binding{source, m_fxaaSampler};
        SDL_BindGPUFragmentSamplers(pass, 0u, &binding, 1u);
        SDL_DrawGPUPrimitives(pass, 3u, 1u, 0u, 0u);
        SDL_EndGPURenderPass(pass);
    }
    // Pass 2: FidelityFX CAS + subtle vibrance/contrast (m_fxaaTarget -> m_casTarget).
    {
        SDL_GPUColorTargetInfo target{};
        target.texture = m_casTarget;
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &target, 1u, nullptr);
        if (!pass)
        {
            error = std::string("SDL_BeginGPURenderPass(native cas): ") + SDL_GetError();
            return false;
        }
        SDL_SetGPUViewport(pass, &viewport);
        SDL_BindGPUGraphicsPipeline(pass, m_casPipeline);
        const SDL_GPUTextureSamplerBinding binding{m_fxaaTarget, m_casSampler};
        SDL_BindGPUFragmentSamplers(pass, 0u, &binding, 1u);
        const float params[4] = {0.42f, 0.10f, 0.02f, 0.0f};
        SDL_PushGPUFragmentUniformData(commands, 0u, params, sizeof(params));
        SDL_DrawGPUPrimitives(pass, 3u, 1u, 0u, 0u);
        SDL_EndGPURenderPass(pass);
    }
    return true;
}

std::string Renderer::statsLine()
{
    std::string line = "[native-pipeline] records:";
    for (size_t i = 0; i < std::size(kRecordNames); ++i)
    {
        line += std::string(" ") + kRecordNames[i] + " " + std::to_string(m_records[i] - m_reported[i]);
        m_reported[i] = m_records[i];
    }
    const auto d = [](uint64_t a, uint64_t b) { return std::to_string(a - b); };
    line += " | held: textures " + std::to_string(m_images.size()) + " (" + std::to_string(m_imageBytes >> 20) +
            " MiB), palettes " + std::to_string(m_palettes.size()) + ", evicted " + d(m_evicted, m_evictedReported) +
            ", refused " + d(m_refused, m_refusedReported);
    if (remasterEnabled())
        line += " | remaster: resolved " + std::to_string(m_resolved.size()) + " (" +
                std::to_string(m_resolvedBytes >> 20) + " MiB, 2d " + std::to_string(m_resolved2dCount) + "), cnn " +
                (m_cnnSupported ? "on" : "off") + ", ai-bc7 hits " + std::to_string(m_aiCache.diskHits()) +
                " gen " + std::to_string(m_aiCache.generated()) + ", ssao " +
                (m_depthCanSample && m_ssaoPipeline ? "on" : "off");
    m_evictedReported = m_evicted;
    m_refusedReported = m_refused;
    const Readiness &now = m_readiness, &was = m_readinessReported;
    line += " | readiness of " + d(now.meshDraws, was.meshDraws) + " mesh draws: R1 camera+model " +
            d(now.cameraAndModel, was.cameraAndModel) + ", R2 object " + d(now.objectIdentity, was.objectIdentity) +
            ", R3 scene float depth " + d(now.sceneFloatDepth, was.sceneFloatDepth) + ", R4 material " +
            d(now.materialKnown, was.materialKnown) + ", R5 normals " + d(now.normals, was.normals);
    const MeshCounts &mc = m_meshCounts, &mr = m_meshReported;
    line += " | meshes drawn " + d(mc.drawn, mr.drawn) + " (merged " + d(mc.merged, mr.merged) + ", offscreen " +
            d(mc.offscreen, mr.offscreen) + ") over " +
            d(mc.frames, mr.frames) + " frames in " + d(mc.passes, mr.passes) +
            " passes; not drawn: target not declared " + d(mc.noTarget, mr.noTarget) + ", a depth buffer as color " +
            d(mc.depthAsColor, mr.depthAsColor) + ", no camera " + d(mc.noCamera, mr.noCamera) +
            ", texture not held " + d(mc.noTexture, mr.noTexture) + ", no pipeline " +
            d(mc.noPipeline, mr.noPipeline) + ", source target never drawn " +
            d(mc.sourceNotDrawn, mr.sourceNotDrawn) + ", reading its own target uncopied " + d(mc.feedback, mr.feedback) + ", copies for those that do " +
            d(mc.copies, mr.copies) + " (destination alpha tests " + d(mc.destinationAlpha, mr.destinationAlpha) + ")" +
            "; window space " + d(mc.window, mr.window) + ", from a target " + d(mc.fromTarget, mr.fromTarget) +
            "; a second depth target in a pass " +
            d(mc.depthMismatch, mr.depthMismatch) + "\n";
    const DepthCounts &dc = m_depthCounts, &dr = m_depthReported;
    if (dc.asked != dr.asked)
        line += "  [native-pipeline] depth reads: asked " + d(dc.asked, dr.asked) + ", copied " + d(dc.copied, dr.copied) +
                ", published " + d(dc.published, dr.published) + ", no such depth target (the clear's) " +
                d(dc.noTarget, dr.noTarget) + ", left for a copy in flight " + d(dc.waiting, dr.waiting) +
                ", unserved " + d(dc.unserved, dr.unserved) + "\n";
    m_depthReported = m_depthCounts;
    if (m_colorReads != m_colorReadsReported || m_colorReadsFailed != m_colorFailedReported)
        line += "  [native-pipeline] colour reads: served " + d(m_colorReads, m_colorReadsReported) + ", failed " +
                d(m_colorReadsFailed, m_colorFailedReported) + "\n";
    m_colorReadsReported = m_colorReads;
    m_colorFailedReported = m_colorReadsFailed;
    m_meshReported = m_meshCounts;
    m_readinessReported = m_readiness;
    std::function<std::string()> producer;
    {
        Queue &q = queue();
        std::lock_guard lock(q.mutex);
        producer = q.producerStats;
    }
    if (producer)
        line += "  " + producer() + "\n";
    return line;
}

void Renderer::serveDepth(SDL_GPUDevice *device)
{
    // The copy the GPU has finished: each block brought to the game's
    // pixels (the nearest of the pixels each covers) and published.
    if (m_depthFence)
    {
        if (!SDL_QueryGPUFence(device, m_depthFence))
        {
            m_depthCounts.waiting += takeDepthReads().size(); // asked again next frame
            return;
        }
        SDL_ReleaseGPUFence(device, m_depthFence);
        m_depthFence = nullptr;
        const auto *mapped = static_cast<const float *>(SDL_MapGPUTransferBuffer(device, m_depthDownload, false));
        if (mapped)
        {
            const uint32_t s = m_depthCopyScale;
            size_t offset = 0u;
            for (const auto &[index, block] : m_depthCopied)
            {
                const uint32_t w = block.width * s, h = block.height * s;
                std::vector<float> values(size_t(block.width) * block.height, 0.0f);
                for (uint32_t y = 0; y < h; ++y)
                    for (uint32_t x = 0; x < w; ++x)
                    {
                        float &v = values[size_t(y / s) * block.width + x / s];
                        v = std::max(v, mapped[offset + size_t(y) * w + x]);
                    }
                offset += size_t(w) * h;
                publishDepth(index, block, std::move(values));
                ++m_depthCounts.published;
            }
            SDL_UnmapGPUTransferBuffer(device, m_depthDownload);
        }
        m_depthCopied.clear();
    }

    const std::vector<DepthBlock> wanted = takeDepthReads();
    if (wanted.empty())
        return;
    m_depthCounts.asked += wanted.size();
    // Only a float depth is copied out as it is.
    if (m_depthFailed || m_depthFormat != SDL_GPU_TEXTUREFORMAT_D32_FLOAT || m_frameScale == 0u)
    {
        m_depthCounts.unserved += wanted.size();
        return;
    }
    const uint32_t s = m_frameScale;
    std::vector<std::pair<uint32_t, DepthBlock>> copies;
    size_t floats = 0u;
    for (uint32_t index = 0; index < wanted.size(); ++index)
    {
        const DepthBlock &b = wanted[index];
        const auto t = m_gpuTargets.find(b.target);
        if (t == m_gpuTargets.end() || !t->second.texture || b.x < 0 || b.y < 0 ||
            (uint32_t(b.x) + b.width) * s > t->second.width || (uint32_t(b.y) + b.height) * s > t->second.height)
        {
            // Never drawn here: the clear's depth, as the GS's memory held.
            publishDepth(index, b, std::vector<float>(size_t(b.width) * b.height, 0.0f));
            ++m_depthCounts.noTarget;
            continue;
        }
        copies.emplace_back(index, b);
        floats += size_t(b.width) * s * b.height * s;
    }
    if (copies.empty())
        return;
    const auto fail = [&](const char *what) {
        std::fprintf(stderr, "[native-pipeline] depth reads off: %s: %s\n", what, SDL_GetError());
        m_depthFailed = true;
        m_depthCounts.unserved += copies.size();
    };
    const uint32_t bytes = uint32_t(floats * sizeof(float));
    if (m_depthDownloadBytes < bytes)
    {
        if (m_depthDownload)
            SDL_ReleaseGPUTransferBuffer(device, m_depthDownload);
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = bytes;
        m_depthDownload = SDL_CreateGPUTransferBuffer(device, &info);
        m_depthDownloadBytes = m_depthDownload ? bytes : 0u;
        if (!m_depthDownload)
            return fail("SDL_CreateGPUTransferBuffer");
    }
    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
    if (!commands)
        return fail("SDL_AcquireGPUCommandBuffer");
    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(commands);
    if (!pass)
    {
        SDL_CancelGPUCommandBuffer(commands);
        return fail("SDL_BeginGPUCopyPass");
    }
    uint32_t offset = 0u;
    for (const auto &[index, b] : copies)
    {
        SDL_GPUTextureRegion region{};
        region.texture = m_gpuTargets[b.target].texture;
        region.x = uint32_t(b.x) * s;
        region.y = uint32_t(b.y) * s;
        region.w = b.width * s;
        region.h = b.height * s;
        region.d = 1u;
        SDL_GPUTextureTransferInfo destination{};
        destination.transfer_buffer = m_depthDownload;
        destination.offset = offset;
        destination.pixels_per_row = region.w;
        destination.rows_per_layer = region.h;
        SDL_DownloadFromGPUTexture(pass, &region, &destination);
        offset += region.w * region.h * uint32_t(sizeof(float));
    }
    SDL_EndGPUCopyPass(pass);
    m_depthFence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    if (!m_depthFence)
        return fail("SDL_SubmitGPUCommandBufferAndAcquireFence");
    m_depthCopied = std::move(copies);
    m_depthCopyScale = s;
    m_depthCounts.copied += m_depthCopied.size();
}

void Renderer::dumpTargets(SDL_GPUDevice *device)
{
    m_dumpTargets = false;
    std::vector<std::tuple<Id, SDL_GPUTexture *, uint32_t, uint32_t>> all;
    all.emplace_back(m_sceneTarget, m_scene, m_sceneWidth, m_sceneHeight);
    for (const auto &[id, t] : m_gpuTargets)
    {
        const auto declared = m_targets.find(id);
        if (!t.texture || declared == m_targets.end() || declared->second.format == PixelFormat::Depth32 ||
            id == m_sceneTarget)
            continue;
        all.emplace_back(id, t.texture, t.width, t.height);
    }
    std::error_code ec;
    std::filesystem::create_directories("build/run/targets", ec);
    for (const auto &[id, texture, w, h] : all)
    {
        if (!texture || w == 0u || h == 0u)
            continue;
        const uint32_t bytes = w * h * 4u;
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = bytes;
        SDL_GPUTransferBuffer *download = SDL_CreateGPUTransferBuffer(device, &info);
        SDL_GPUCommandBuffer *commands = download ? SDL_AcquireGPUCommandBuffer(device) : nullptr;
        SDL_GPUCopyPass *pass = commands ? SDL_BeginGPUCopyPass(commands) : nullptr;
        SDL_GPUFence *fence = nullptr;
        if (pass)
        {
            SDL_GPUTextureRegion region{};
            region.texture = texture;
            region.w = w;
            region.h = h;
            region.d = 1u;
            SDL_GPUTextureTransferInfo destination{};
            destination.transfer_buffer = download;
            destination.pixels_per_row = w;
            destination.rows_per_layer = h;
            SDL_DownloadFromGPUTexture(pass, &region, &destination);
            SDL_EndGPUCopyPass(pass);
            fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
        }
        else if (commands)
            SDL_CancelGPUCommandBuffer(commands);
        if (fence)
        {
            SDL_WaitForGPUFences(device, true, &fence, 1u);
            SDL_ReleaseGPUFence(device, fence);
            if (const auto *p = static_cast<const uint8_t *>(SDL_MapGPUTransferBuffer(device, download, false)))
            {
                char name[96];
                std::snprintf(name, sizeof(name), "build/run/targets/target%u_%ux%u.ppm", id, w, h);
                if (FILE *f = std::fopen(name, "wb"))
                {
                    std::fprintf(f, "P6\n%u %u\n255\n", w * 2u, h);
                    std::vector<uint8_t> row(size_t(w) * 6u);
                    for (uint32_t y = 0; y < h; ++y)
                    {
                        for (uint32_t x = 0; x < w; ++x)
                        {
                            const uint8_t *px = p + (size_t(y) * w + x) * 4u;
                            std::memcpy(row.data() + x * 3u, px, 3u);
                            row[(w + x) * 3u] = row[(w + x) * 3u + 1u] = row[(w + x) * 3u + 2u] = px[3];
                        }
                        std::fwrite(row.data(), 1, row.size(), f);
                    }
                    std::fclose(f);
                    std::fprintf(stderr, "[target-dump] %s\n", name);
                }
                SDL_UnmapGPUTransferBuffer(device, download);
            }
        }
        if (download)
            SDL_ReleaseGPUTransferBuffer(device, download);
    }
}

void Renderer::serveColorReads(SDL_GPUDevice *device)
{
    if (m_dumpTargets)
        dumpTargets(device);
    std::vector<std::pair<uint32_t, ColorRead>> waiting;
    ColorMailbox &m = colorMailbox();
    {
        std::lock_guard lock(m.mutex);
        for (const auto &[id, e] : m.entries)
            if (!e.finished)
                waiting.emplace_back(id, e.read);
    }
    if (waiting.empty())
        return;
    const auto finish = [&](uint32_t id, std::vector<uint8_t> &&rgba, bool ok, uint32_t answerW = 0u,
                            uint32_t answerH = 0u) {
        ++(ok ? m_colorReads : m_colorReadsFailed);
        std::lock_guard lock(m.mutex);
        const auto e = m.entries.find(id);
        if (e == m.entries.end())
            return;
        e->second.finished = true;
        e->second.ok = ok;
        e->second.rgba = std::move(rgba);
        e->second.width = answerW;
        e->second.height = answerH;
    };
    const uint32_t frameScale = std::max<uint32_t>(m_frameScale, 1u);
    for (const auto &[id, read] : waiting)
    {
        // The scene, or an offscreen target, as the last frame left it.
        SDL_GPUTexture *texture = nullptr;
        uint32_t texW = 0, texH = 0;
        uint32_t s = frameScale;
        if (read.target == m_sceneTarget || read.target == 0u)
        {
            texture = m_scene;
            texW = m_sceneWidth;
            texH = m_sceneHeight;
        }
        else if (const auto t = m_gpuTargets.find(read.target); t != m_gpuTargets.end())
        {
            texture = t->second.texture;
            texW = t->second.width;
            texH = t->second.height;
            if (t->second.scale != 0u)
                s = t->second.scale;
        }
        Rect r = read.rect;
        if (read.whole)
            r = {0, 0, int32_t(texW / s), int32_t(texH / s)};
        const uint32_t w = uint32_t(std::max(r.x1 - r.x0, 0)), h = uint32_t(std::max(r.y1 - r.y0, 0));
        if (!texture || m_frameScale == 0u || w == 0u || h == 0u || r.x0 < 0 || r.y0 < 0 ||
            uint32_t(r.x1) * s > texW || uint32_t(r.y1) * s > texH)
        {
            finish(id, {}, false);
            continue;
        }
        // Halved on the GPU while the scale is even (a bilinear blit at
        // half size is the mean of each 2 x 2), so a read at 4x downloads
        // the game's size, not 16 times it; an odd factor left (3x, 6x's 3)
        // is averaged below. The EE waits on this read.
        SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
        std::vector<SDL_GPUTexture *> halves;
        SDL_GPUTexture *from = texture;
        uint32_t fromX = uint32_t(r.x0) * s, fromY = uint32_t(r.y0) * s;
        uint32_t left = s;
        while (commands && left % 2u == 0u)
        {
            const uint32_t halfW = w * left / 2u, halfH = h * left / 2u;
            SDL_GPUTextureCreateInfo halfInfo{};
            halfInfo.type = SDL_GPU_TEXTURETYPE_2D;
            halfInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            halfInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            halfInfo.width = halfW;
            halfInfo.height = halfH;
            halfInfo.layer_count_or_depth = 1;
            halfInfo.num_levels = 1;
            SDL_GPUTexture *half = SDL_CreateGPUTexture(device, &halfInfo);
            if (!half)
                break;
            halves.push_back(half);
            SDL_GPUBlitInfo blit{};
            blit.source.texture = from;
            blit.source.x = fromX;
            blit.source.y = fromY;
            blit.source.w = w * left;
            blit.source.h = h * left;
            blit.destination.texture = half;
            blit.destination.w = halfW;
            blit.destination.h = halfH;
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(commands, &blit);
            from = half;
            fromX = fromY = 0u;
            left /= 2u;
        }
        const uint32_t hostW = w * left, hostH = h * left, bytes = hostW * hostH * 4u;
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = bytes;
        SDL_GPUTransferBuffer *download = commands ? SDL_CreateGPUTransferBuffer(device, &info) : nullptr;
        SDL_GPUCopyPass *pass = download ? SDL_BeginGPUCopyPass(commands) : nullptr;
        SDL_GPUFence *fence = nullptr;
        if (pass)
        {
            SDL_GPUTextureRegion region{};
            region.texture = from;
            region.x = fromX;
            region.y = fromY;
            region.w = hostW;
            region.h = hostH;
            region.d = 1u;
            SDL_GPUTextureTransferInfo destination{};
            destination.transfer_buffer = download;
            destination.pixels_per_row = hostW;
            destination.rows_per_layer = hostH;
            SDL_DownloadFromGPUTexture(pass, &region, &destination);
            SDL_EndGPUCopyPass(pass);
            fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
        }
        else if (commands)
            SDL_CancelGPUCommandBuffer(commands);
        std::vector<uint8_t> rgba;
        bool ok = false;
        if (fence)
        {
            SDL_WaitForGPUFences(device, true, &fence, 1u);
            SDL_ReleaseGPUFence(device, fence);
            if (const auto *p = static_cast<const uint8_t *>(SDL_MapGPUTransferBuffer(device, download, false)))
            {
                // Each game pixel the mean of the left x left it covers;
                // alpha from the stored A / 0x80 back to the game's.
                rgba.resize(size_t(w) * h * 4u);
                for (uint32_t y = 0; y < h; ++y)
                    for (uint32_t x = 0; x < w; ++x)
                        for (uint32_t c = 0; c < 4u; ++c)
                        {
                            uint32_t sum = 0;
                            for (uint32_t dy = 0; dy < left; ++dy)
                                for (uint32_t dx = 0; dx < left; ++dx)
                                    sum += p[(size_t(y * left + dy) * hostW + x * left + dx) * 4u + c];
                            float v = float(sum) / float(left * left);
                            if (c == 3u)
                                v = v * 128.0f / 255.0f;
                            rgba[(size_t(y) * w + x) * 4u + c] = uint8_t(std::min(v + 0.5f, 255.0f));
                        }
                SDL_UnmapGPUTransferBuffer(device, download);
                ok = true;
            }
        }
        for (SDL_GPUTexture *half : halves)
            SDL_ReleaseGPUTexture(device, half);
        if (download)
            SDL_ReleaseGPUTransferBuffer(device, download);
        if (!ok)
            std::fprintf(stderr, "[native-pipeline] colour read %u failed: %s\n", id, SDL_GetError());
        finish(id, std::move(rgba), ok, w, h);
    }
    m.done.notify_all();
}

void Renderer::releaseDepth(SDL_GPUDevice *device)
{
    if (m_depthFence)
    {
        SDL_WaitForGPUFences(device, true, &m_depthFence, 1u);
        SDL_ReleaseGPUFence(device, m_depthFence);
    }
    if (m_depthDownload)
        SDL_ReleaseGPUTransferBuffer(device, m_depthDownload);
    m_depthFence = nullptr;
    m_depthDownload = nullptr;
    m_depthDownloadBytes = 0u;
    m_depthCopied.clear();
}

void Renderer::release(SDL_GPUDevice *device)
{
    if (!device)
        return;
    releaseMeshes(device);
    releaseDepth(device);
    m_meshes.clear();
    m_meshVerts.clear();
    m_meshIndices.clear();
    m_frameMeshes.clear();
    m_frameVerts.clear();
    m_frameIndices.clear();
    m_drainedVerts.clear();
    m_drainedIndices.clear();
    if (m_scene)
        SDL_ReleaseGPUTexture(device, m_scene);
    if (m_fxaaTarget)
        SDL_ReleaseGPUTexture(device, m_fxaaTarget);
    if (m_casTarget)
        SDL_ReleaseGPUTexture(device, m_casTarget);
    if (m_ssaoRaw)
        SDL_ReleaseGPUTexture(device, m_ssaoRaw);
    if (m_ssaoBlur)
        SDL_ReleaseGPUTexture(device, m_ssaoBlur);
    for (auto &[id, image] : m_images)
        SDL_ReleaseGPUTexture(device, image.texture);
    for (auto &[id, image] : m_palettes)
        SDL_ReleaseGPUTexture(device, image.texture);
    for (auto &[key, image] : m_resolved)
        SDL_ReleaseGPUTexture(device, image.texture);
    if (m_viewPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, m_viewPipeline);
    if (m_viewSampler)
        SDL_ReleaseGPUSampler(device, m_viewSampler);
    if (m_fxaaPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, m_fxaaPipeline);
    if (m_casPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, m_casPipeline);
    m_aiCache.shutdown();
    if (m_xbrPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, m_xbrPipeline);
    if (m_xbr2dPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, m_xbr2dPipeline);
    for (SDL_GPUGraphicsPipeline *&p : m_cnnConvPipelines)
    {
        if (p)
            SDL_ReleaseGPUGraphicsPipeline(device, p);
        p = nullptr;
    }
    if (m_cnnLastPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, m_cnnLastPipeline);
    if (m_cnnD2sPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, m_cnnD2sPipeline);
    for (SDL_GPUTexture *&t : m_cnnScratch)
    {
        if (t)
            SDL_ReleaseGPUTexture(device, t);
        t = nullptr;
    }
    m_cnnScratchW = m_cnnScratchH = 0u;
    if (m_ssaoPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, m_ssaoPipeline);
    if (m_ssaoBlurPipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, m_ssaoBlurPipeline);
    if (m_ssaoCompositePipeline)
        SDL_ReleaseGPUGraphicsPipeline(device, m_ssaoCompositePipeline);
    if (m_fxaaSampler)
        SDL_ReleaseGPUSampler(device, m_fxaaSampler);
    if (m_casSampler)
        SDL_ReleaseGPUSampler(device, m_casSampler);
    m_images.clear();
    m_palettes.clear();
    m_resolved.clear();
    m_resolvedOrder.clear();
    m_imageOrder.clear();
    m_recent.clear();
    m_imageBytes = 0u;
    m_resolvedBytes = 0u;
    m_scene = nullptr;
    m_fxaaTarget = nullptr;
    m_casTarget = nullptr;
    m_ssaoRaw = nullptr;
    m_ssaoBlur = nullptr;
    m_viewPipeline = nullptr;
    m_viewSampler = nullptr;
    m_fxaaPipeline = nullptr;
    m_casPipeline = nullptr;
    m_xbrPipeline = nullptr;
    m_xbr2dPipeline = nullptr;
    m_cnnLastPipeline = nullptr;
    m_cnnD2sPipeline = nullptr;
    m_ssaoPipeline = nullptr;
    m_ssaoBlurPipeline = nullptr;
    m_ssaoCompositePipeline = nullptr;
    m_fxaaSampler = nullptr;
    m_casSampler = nullptr;
    m_casFailed = false;
    m_xbrFailed = false;
    m_cnnFailed = false;
    m_cnnSupported = false;
    m_bc7Checked = false;
    m_bc7Supported = false;
    m_ssaoFailed = false;
    m_resolved2dCount = 0u;
    m_sceneWidth = m_sceneHeight = 0;
    m_casWidth = m_casHeight = 0;
    m_ssaoWidth = m_ssaoHeight = 0;
}
} // namespace dq8::gfx::native
