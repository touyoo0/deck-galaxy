#include "gfx/native/native_ai_cache.h"

#include "gfx/native/bc7enc.h"

#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "ui/stb_image.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace dq8::gfx::native
{
namespace
{
namespace fs = std::filesystem;

constexpr uint32_t kDdsMagic = 0x20534444u; // "DDS "
constexpr uint32_t kDdpfFourCC = 0x4u;
constexpr uint32_t kFourCcDx10 = 0x30315844u; // "DX10"
constexpr uint32_t kDxgiFormatBc7Unorm = 98u;
constexpr uint32_t kDdsdCaps = 0x1u;
constexpr uint32_t kDdsdHeight = 0x2u;
constexpr uint32_t kDdsdWidth = 0x4u;
constexpr uint32_t kDdsdPixelFormat = 0x1000u;
constexpr uint32_t kDdsdMipMapCount = 0x20000u;
constexpr uint32_t kDdsdLinearSize = 0x80000u;
constexpr uint32_t kDdscapsComplex = 0x8u;
constexpr uint32_t kDdscapsTexture = 0x1000u;
constexpr uint32_t kDdscapsMipMap = 0x400000u;

struct DdsPixelFormat
{
    uint32_t size = 32;
    uint32_t flags = 0;
    uint32_t fourCC = 0;
    uint32_t rgbBitCount = 0;
    uint32_t rBitMask = 0;
    uint32_t gBitMask = 0;
    uint32_t bBitMask = 0;
    uint32_t aBitMask = 0;
};

struct DdsHeader
{
    uint32_t size = 124;
    uint32_t flags = 0;
    uint32_t height = 0;
    uint32_t width = 0;
    uint32_t pitchOrLinearSize = 0;
    uint32_t depth = 1;
    uint32_t mipMapCount = 1;
    uint32_t reserved1[11] = {};
    DdsPixelFormat ddspf{};
    uint32_t caps = 0;
    uint32_t caps2 = 0;
    uint32_t caps3 = 0;
    uint32_t caps4 = 0;
    uint32_t reserved2 = 0;
};

struct DdsHeaderDxt10
{
    uint32_t dxgiFormat = kDxgiFormatBc7Unorm;
    uint32_t resourceDimension = 3; // D3D10_RESOURCE_DIMENSION_TEXTURE2D
    uint32_t miscFlag = 0;
    uint32_t arraySize = 1;
    uint32_t miscFlags2 = 0;
};

static_assert(sizeof(DdsHeader) == 124u, "DDS_HEADER must be 124 bytes");
static_assert(sizeof(DdsHeaderDxt10) == 20u, "DDS_HEADER_DXT10 must be 20 bytes");

size_t bc7LevelBytes(uint32_t w, uint32_t h)
{
    const size_t bx = std::max<size_t>(1u, (w + 3u) / 4u);
    const size_t by = std::max<size_t>(1u, (h + 3u) / 4u);
    return bx * by * 16u;
}

// Minimal self-contained uncompressed RGBA8 PNG writer (stored DEFLATE blocks)
// so we have zero dependency on libpng or zlib.
uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t len)
{
    static uint32_t crcTable[256];
    static std::once_flag once;
    std::call_once(once, [] {
        for (uint32_t n = 0; n < 256; ++n)
        {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1u)) : (c >> 1u);
            crcTable[n] = c;
        }
    });
    uint32_t c = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        c = crcTable[(c ^ data[i]) & 0xFFu] ^ (c >> 8u);
    return c ^ 0xFFFFFFFFu;
}

void writeBe32(std::vector<uint8_t> &buf, uint32_t v)
{
    buf.push_back(uint8_t((v >> 24u) & 0xFFu));
    buf.push_back(uint8_t((v >> 16u) & 0xFFu));
    buf.push_back(uint8_t((v >> 8u) & 0xFFu));
    buf.push_back(uint8_t(v & 0xFFu));
}

void writePngChunk(std::vector<uint8_t> &out, const char type[4], const uint8_t *data, size_t len)
{
    writeBe32(out, static_cast<uint32_t>(len));
    const size_t typePos = out.size();
    out.insert(out.end(), type, type + 4);
    if (len > 0)
        out.insert(out.end(), data, data + len);
    const uint32_t crc = crc32Update(0u, out.data() + typePos, 4u + len);
    writeBe32(out, crc);
}

bool writeUncompressedPng(const std::string &path, uint32_t w, uint32_t h, const uint8_t *rgba)
{
    std::vector<uint8_t> raw;
    const size_t rowBytes = size_t(w) * 4u;
    raw.reserve((rowBytes + 1u) * h);
    for (uint32_t y = 0; y < h; ++y)
    {
        raw.push_back(0u); // filter type 0 (None)
        const uint8_t *row = rgba + size_t(y) * rowBytes;
        raw.insert(raw.end(), row, row + rowBytes);
    }

    // Wrap raw scanlines in a valid zlib stream using uncompressed DEFLATE blocks.
    std::vector<uint8_t> zstream;
    zstream.reserve(raw.size() + (raw.size() / 65535u + 2u) * 5u + 6u);
    zstream.push_back(0x78u); // CMF: deflate, 32K window
    zstream.push_back(0x01u); // FLG: no dict, check bits

    uint32_t s1 = 1u, s2 = 0u;
    for (uint8_t b : raw)
    {
        s1 = (s1 + b) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }

    size_t pos = 0;
    while (pos < raw.size())
    {
        const size_t chunk = std::min<size_t>(65535u, raw.size() - pos);
        const bool last = (pos + chunk) >= raw.size();
        zstream.push_back(last ? 0x01u : 0x00u);
        const uint16_t len16 = static_cast<uint16_t>(chunk);
        const uint16_t nlen16 = static_cast<uint16_t>(~len16);
        zstream.push_back(uint8_t(len16 & 0xFFu));
        zstream.push_back(uint8_t((len16 >> 8u) & 0xFFu));
        zstream.push_back(uint8_t(nlen16 & 0xFFu));
        zstream.push_back(uint8_t((nlen16 >> 8u) & 0xFFu));
        zstream.insert(zstream.end(), raw.data() + pos, raw.data() + pos + chunk);
        pos += chunk;
    }
    writeBe32(zstream, (s2 << 16u) | s1);

    std::vector<uint8_t> png;
    static const uint8_t kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), kSig, kSig + 8);

    std::vector<uint8_t> ihdr;
    writeBe32(ihdr, w);
    writeBe32(ihdr, h);
    ihdr.push_back(8u); // bit depth
    ihdr.push_back(6u); // RGBA
    ihdr.push_back(0u); // compression
    ihdr.push_back(0u); // filter
    ihdr.push_back(0u); // interlace
    writePngChunk(png, "IHDR", ihdr.data(), ihdr.size());
    writePngChunk(png, "IDAT", zstream.data(), zstream.size());
    writePngChunk(png, "IEND", nullptr, 0u);

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        return false;
    f.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    return f.good();
}

// Premultiplied-alpha 2x2 box downsample for clean mipmap generation.
void downsampleMip(const uint8_t *src, uint32_t sw, uint32_t sh, std::vector<uint8_t> &dst, uint32_t &dw,
                   uint32_t &dh)
{
    dw = std::max(1u, sw >> 1u);
    dh = std::max(1u, sh >> 1u);
    dst.resize(size_t(dw) * dh * 4u);
    for (uint32_t y = 0; y < dh; ++y)
    {
        const uint32_t sy0 = std::min(y * 2u, sh - 1u);
        const uint32_t sy1 = std::min(y * 2u + 1u, sh - 1u);
        for (uint32_t x = 0; x < dw; ++x)
        {
            const uint32_t sx0 = std::min(x * 2u, sw - 1u);
            const uint32_t sx1 = std::min(x * 2u + 1u, sw - 1u);
            const uint8_t *p[4] = {
                src + (size_t(sy0) * sw + sx0) * 4u,
                src + (size_t(sy0) * sw + sx1) * 4u,
                src + (size_t(sy1) * sw + sx0) * 4u,
                src + (size_t(sy1) * sw + sx1) * 4u,
            };
            float r = 0.0f, g = 0.0f, b = 0.0f, aSum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                const float a = float(p[k][3]) / 255.0f;
                r += float(p[k][0]) * a;
                g += float(p[k][1]) * a;
                b += float(p[k][2]) * a;
                aSum += a;
            }
            uint8_t *o = dst.data() + (size_t(y) * dw + x) * 4u;
            if (aSum > 1e-5f)
            {
                o[0] = uint8_t(std::clamp(r / aSum + 0.5f, 0.0f, 255.0f));
                o[1] = uint8_t(std::clamp(g / aSum + 0.5f, 0.0f, 255.0f));
                o[2] = uint8_t(std::clamp(b / aSum + 0.5f, 0.0f, 255.0f));
            }
            else
            {
                o[0] = o[1] = o[2] = 0u;
            }
            o[3] = uint8_t(std::clamp((aSum * 0.25f) * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
}

void encodeBc7Level(const uint8_t *rgba, uint32_t w, uint32_t h, std::vector<uint8_t> &out)
{
    static std::once_flag initOnce;
    std::call_once(initOnce, [] { bc7enc_compress_block_init(); });

    bc7enc_compress_block_params params{};
    bc7enc_compress_block_params_init(&params);
    params.m_max_partitions = 16; // good balance of speed and quality on Steam Deck CPU
    params.m_uber_level = 0;

    const uint32_t bxCount = std::max(1u, (w + 3u) / 4u);
    const uint32_t byCount = std::max(1u, (h + 3u) / 4u);
    out.resize(size_t(bxCount) * byCount * 16u);

    color_rgba block[16];
    for (uint32_t by = 0; by < byCount; ++by)
    {
        for (uint32_t bx = 0; bx < bxCount; ++bx)
        {
            for (uint32_t py = 0; py < 4u; ++py)
            {
                const uint32_t sy = std::min(by * 4u + py, h - 1u);
                for (uint32_t px = 0; px < 4u; ++px)
                {
                    const uint32_t sx = std::min(bx * 4u + px, w - 1u);
                    const uint8_t *p = rgba + (size_t(sy) * w + sx) * 4u;
                    block[py * 4u + px].m_c[0] = p[0];
                    block[py * 4u + px].m_c[1] = p[1];
                    block[py * 4u + px].m_c[2] = p[2];
                    block[py * 4u + px].m_c[3] = p[3];
                }
            }
            uint8_t *dst = out.data() + (size_t(by) * bxCount + bx) * 16u;
            bc7enc_compress_block(dst, block, &params);
        }
    }
}

bool writeDdsFile(const std::string &path, uint32_t w, uint32_t h, const std::vector<AiTextureCache::MipLevel> &mips)
{
    if (mips.empty())
        return false;
    DdsHeader hdr{};
    hdr.flags = kDdsdCaps | kDdsdHeight | kDdsdWidth | kDdsdPixelFormat | kDdsdMipMapCount | kDdsdLinearSize;
    hdr.width = w;
    hdr.height = h;
    hdr.pitchOrLinearSize = static_cast<uint32_t>(mips[0].bc7Data.size());
    hdr.mipMapCount = static_cast<uint32_t>(mips.size());
    hdr.ddspf.flags = kDdpfFourCC;
    hdr.ddspf.fourCC = kFourCcDx10;
    hdr.caps = kDdscapsTexture | (mips.size() > 1u ? (kDdscapsComplex | kDdscapsMipMap) : 0u);

    DdsHeaderDxt10 dx10{};
    dx10.dxgiFormat = kDxgiFormatBc7Unorm;

    const std::string tmpPath = path + ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f)
            return false;
        f.write(reinterpret_cast<const char *>(&kDdsMagic), sizeof(kDdsMagic));
        f.write(reinterpret_cast<const char *>(&hdr), sizeof(hdr));
        f.write(reinterpret_cast<const char *>(&dx10), sizeof(dx10));
        for (const auto &mip : mips)
            f.write(reinterpret_cast<const char *>(mip.bc7Data.data()), static_cast<std::streamsize>(mip.bc7Data.size()));
        if (!f.good())
            return false;
    }
    std::error_code ec;
    fs::rename(tmpPath, path, ec);
    if (ec)
    {
        fs::remove(tmpPath, ec);
        return false;
    }
    return true;
}

bool isExecutableFile(const fs::path &p)
{
    std::error_code ec;
    if (!fs::is_regular_file(p, ec))
        return false;
#if defined(__unix__) || defined(__APPLE__)
    return ::access(p.c_str(), X_OK) == 0;
#else
    return true;
#endif
}

std::string findInPath(const char *name)
{
#if defined(__unix__) || defined(__APPLE__)
    const char *pathEnv = std::getenv("PATH");
    if (!pathEnv)
        return {};
    std::string s(pathEnv);
    size_t start = 0;
    while (start <= s.size())
    {
        size_t end = s.find(':', start);
        std::string dir = s.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!dir.empty())
        {
            fs::path candidate = fs::path(dir) / name;
            if (isExecutableFile(candidate))
                return candidate.string();
        }
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
#else
    (void)name;
#endif
    return {};
}

bool runProcess(const std::vector<std::string> &args, bool quiet)
{
#if defined(__unix__) || defined(__APPLE__)
    if (args.empty())
        return false;
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (const std::string &a : args)
        argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = ::fork();
    if (pid < 0)
        return false;
    if (pid == 0)
    {
        // Lower CPU priority so background upscaling never contends with gameplay.
        (void)::nice(10);
        if (quiet)
        {
            int devnull = ::open("/dev/null", O_WRONLY);
            if (devnull >= 0)
            {
                ::dup2(devnull, STDOUT_FILENO);
                ::dup2(devnull, STDERR_FILENO);
                ::close(devnull);
            }
        }
        ::execv(argv[0], argv.data());
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0)
        return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#else
    (void)args;
    (void)quiet;
    return false;
#endif
}

} // namespace

AiTextureCache::~AiTextureCache()
{
    shutdown();
}

uint64_t AiTextureCache::hashRgba(uint32_t width, uint32_t height, const uint8_t *rgba, size_t byteSize)
{
    uint64_t h = 14695981039346656037ull;
    auto mixByte = [&](uint8_t b) {
        h ^= uint64_t(b);
        h *= 1099511628211ull;
    };
    const uint32_t dims[2] = {width, height};
    const uint8_t *dimBytes = reinterpret_cast<const uint8_t *>(dims);
    for (size_t i = 0; i < sizeof(dims); ++i)
        mixByte(dimBytes[i]);
    for (size_t i = 0; i < byteSize; ++i)
        mixByte(rgba[i]);
    // Murmur3 64-bit finalizer for uniform hex distribution.
    h ^= h >> 33u;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33u;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33u;
    return h;
}

void AiTextureCache::init(bool bc7Supported)
{
    if (m_initialized)
        return;
    m_initialized = true;

    if (!bc7Supported)
        return;
    if (const char *v = std::getenv("RG_AI_CACHE"); v && *v == '0')
        return;
    if (const char *v = std::getenv("RG_AI_DEBUG"); v && *v && *v != '0')
        m_debug = true;
    if (const char *v = std::getenv("RG_AI_CACHE_MAX_MB"); v && *v)
    {
        const uint64_t mb = std::strtoull(v, nullptr, 10);
        if (mb >= 64u)
            m_maxDiskBytes = mb * 1024ull * 1024ull;
    }

    // Resolve cache directory: $RG_AI_CACHE_DIR, else $XDG_CACHE_HOME/RogueGalaxy/ai-textures-bc7,
    // else $HOME/.cache/RogueGalaxy/ai-textures-bc7.
    if (const char *v = std::getenv("RG_AI_CACHE_DIR"); v && *v)
    {
        m_cacheDir = v;
    }
    else if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
    {
        m_cacheDir = (fs::path(xdg) / "RogueGalaxy" / "ai-textures-bc7").string();
    }
    else if (const char *home = std::getenv("HOME"); home && *home)
    {
        m_cacheDir = (fs::path(home) / ".cache" / "RogueGalaxy" / "ai-textures-bc7").string();
    }
    else
    {
        return;
    }

    m_tmpDir = (fs::path(m_cacheDir) / ".tmp").string();
    std::error_code ec;
    fs::create_directories(m_cacheDir, ec);
    if (ec)
        return;

    // Scan existing .dds cache files into m_diskHashes for O(1) lookup.
    for (const auto &entry : fs::directory_iterator(m_cacheDir, ec))
    {
        if (!entry.is_regular_file(ec))
            continue;
        const fs::path p = entry.path();
        if (p.extension() != ".dds")
            continue;
        const std::string stem = p.stem().string();
        if (stem.size() != 16u)
            continue;
        char *end = nullptr;
        const uint64_t hash = std::strtoull(stem.c_str(), &end, 16);
        if (end && *end == '\0')
        {
            m_diskHashes.insert(hash);
            m_diskBytes += entry.file_size(ec);
        }
    }

    // Auto-detect realesrgan-ncnn-vulkan for background 4x AI generation.
    std::vector<fs::path> candidates;
    if (const char *v = std::getenv("RG_AI_UPSCALER_BIN"); v && *v)
        candidates.emplace_back(v);
    candidates.push_back(fs::path(m_cacheDir) / "bin" / "realesrgan-ncnn-vulkan");
    candidates.push_back(fs::path("tools") / "realesrgan-ncnn-vulkan");
    if (std::string inPath = findInPath("realesrgan-ncnn-vulkan"); !inPath.empty())
        candidates.emplace_back(inPath);

    for (const fs::path &c : candidates)
    {
        if (isExecutableFile(c))
        {
            m_upscalerBin = c.string();
            break;
        }
    }

    if (!m_upscalerBin.empty())
    {
        if (const char *v = std::getenv("RG_AI_MODEL_DIR"); v && *v)
            m_modelDir = v;
        else
            m_modelDir = (fs::path(m_upscalerBin).parent_path() / "models").string();

        if (const char *v = std::getenv("RG_AI_MODEL"); v && *v)
        {
            m_modelName = v;
        }
        else
        {
            static const char *kPreferredModels[] = {
                "realesr-animevideov3-x4",
                "realesr-animevideov3",
                "realesrgan-x4plus-anime",
                "4x-AnimeSharp",
            };
            for (const char *m : kPreferredModels)
            {
                if (fs::is_regular_file(fs::path(m_modelDir) / (std::string(m) + ".param"), ec))
                {
                    m_modelName = m;
                    break;
                }
            }
            if (m_modelName.empty())
                m_modelName = "realesr-animevideov3";
        }
        fs::create_directories(m_tmpDir, ec);
        m_upscalerAvailable = true;
    }

    m_enabled = true;
    m_stop = false;
    m_worker = std::thread(&AiTextureCache::workerLoop, this);

    std::fprintf(stderr,
                 "[native-pipeline] AI BC7 cache: %s (%zu cached .dds, %.1f MiB, ncnn-vulkan: %s%s%s)\n",
                 m_cacheDir.c_str(), m_diskHashes.size(), double(m_diskBytes) / (1024.0 * 1024.0),
                 m_upscalerAvailable ? "ready" : "off",
                 m_upscalerAvailable ? " / model=" : "",
                 m_upscalerAvailable ? m_modelName.c_str() : "");
}

void AiTextureCache::shutdown()
{
    if (!m_initialized)
        return;
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
        m_loadQueue.clear();
        m_genQueue.clear();
    }
    m_cv.notify_all();
    if (m_worker.joinable())
        m_worker.join();
    {
        std::lock_guard lock(m_readyMutex);
        m_ready.clear();
    }
    m_inflight.clear();
    m_diskHashes.clear();
    m_enabled = false;
    m_upscalerAvailable = false;
    m_initialized = false;
}

std::string AiTextureCache::ddsPath(uint64_t contentHash) const
{
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.dds", static_cast<unsigned long long>(contentHash));
    return (fs::path(m_cacheDir) / name).string();
}

void AiTextureCache::request(uint64_t resolvedKey, uint64_t contentHash, uint32_t width, uint32_t height, Bytes rgba,
                             bool allowGenerate)
{
    if (!m_enabled)
        return;
    std::lock_guard lock(m_mutex);
    if (m_inflight.find(contentHash) != m_inflight.end())
        return;

    const bool onDisk = m_diskHashes.find(contentHash) != m_diskHashes.end();
    if (onDisk)
    {
        m_inflight.insert(contentHash);
        m_loadQueue.push_back(Job{resolvedKey, contentHash, width, height, nullptr, true});
        m_pendingJobs.fetch_add(1u, std::memory_order_relaxed);
        m_cv.notify_one();
    }
    else if (allowGenerate && m_upscalerAvailable && rgba && m_diskBytes < m_maxDiskBytes && m_genQueue.size() < 48u)
    {
        m_inflight.insert(contentHash);
        m_genQueue.push_back(Job{resolvedKey, contentHash, width, height, std::move(rgba), false});
        m_pendingJobs.fetch_add(1u, std::memory_order_relaxed);
        m_cv.notify_one();
    }
}

void AiTextureCache::takeReady(std::vector<ReadyTexture> &out)
{
    std::lock_guard lock(m_readyMutex);
    if (m_ready.empty())
        return;
    out.insert(out.end(), std::make_move_iterator(m_ready.begin()), std::make_move_iterator(m_ready.end()));
    m_ready.clear();
}

bool AiTextureCache::loadDds(const std::string &path, ReadyTexture &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    uint32_t magic = 0;
    DdsHeader hdr{};
    DdsHeaderDxt10 dx10{};
    f.read(reinterpret_cast<char *>(&magic), sizeof(magic));
    f.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));
    if (!f || magic != kDdsMagic || hdr.size != 124u || (hdr.ddspf.flags & kDdpfFourCC) == 0u ||
        hdr.ddspf.fourCC != kFourCcDx10)
        return false;
    f.read(reinterpret_cast<char *>(&dx10), sizeof(dx10));
    if (!f || dx10.dxgiFormat != kDxgiFormatBc7Unorm || hdr.width == 0u || hdr.height == 0u || hdr.width > 4096u ||
        hdr.height > 4096u)
        return false;

    const uint32_t mipCount = std::max(1u, hdr.mipMapCount);
    out.width = hdr.width;
    out.height = hdr.height;
    out.mips.clear();
    out.mips.reserve(mipCount);

    uint32_t mw = hdr.width, mh = hdr.height;
    for (uint32_t m = 0; m < mipCount; ++m)
    {
        const size_t bytes = bc7LevelBytes(mw, mh);
        MipLevel lvl{mw, mh, std::vector<uint8_t>(bytes)};
        f.read(reinterpret_cast<char *>(lvl.bc7Data.data()), static_cast<std::streamsize>(bytes));
        if (!f)
            return false;
        out.mips.push_back(std::move(lvl));
        mw = std::max(1u, mw >> 1u);
        mh = std::max(1u, mh >> 1u);
    }
    return true;
}

bool AiTextureCache::generateAndEncode(const Job &job, ReadyTexture &out)
{
    if (!job.rgba || job.rgba->size() < size_t(job.width) * job.height * 4u)
        return false;

    char prefix[32];
    std::snprintf(prefix, sizeof(prefix), "%016llx", static_cast<unsigned long long>(job.contentHash));
    const std::string inPng = (fs::path(m_tmpDir) / (std::string(prefix) + "_in.png")).string();
    const std::string outPng = (fs::path(m_tmpDir) / (std::string(prefix) + "_out.png")).string();

    if (!writeUncompressedPng(inPng, job.width, job.height, job.rgba->data()))
        return false;

    std::vector<std::string> args = {
        m_upscalerBin, "-i", inPng, "-o", outPng, "-s", "4", "-n", m_modelName, "-f", "png",
    };
    if (!m_modelDir.empty())
    {
        args.push_back("-m");
        args.push_back(m_modelDir);
    }

    const bool ok = runProcess(args, !m_debug);
    std::error_code ec;
    fs::remove(inPng, ec);
    if (!ok)
    {
        fs::remove(outPng, ec);
        return false;
    }

    int upW = 0, upH = 0, comp = 0;
    uint8_t *pixels = stbi_load(outPng.c_str(), &upW, &upH, &comp, 4);
    fs::remove(outPng, ec);
    if (!pixels || upW <= 0 || upH <= 0 || upW > 4096 || upH > 4096)
    {
        if (pixels)
            stbi_image_free(pixels);
        return false;
    }

    // Build premultiplied-alpha mipmap chain and encode every level to BC7.
    out.width = static_cast<uint32_t>(upW);
    out.height = static_cast<uint32_t>(upH);
    out.mips.clear();

    std::vector<uint8_t> cur(pixels, pixels + size_t(upW) * upH * 4u);
    stbi_image_free(pixels);

    uint32_t mw = out.width, mh = out.height;
    while (true)
    {
        MipLevel lvl{mw, mh, {}};
        encodeBc7Level(cur.data(), mw, mh, lvl.bc7Data);
        out.mips.push_back(std::move(lvl));
        if (mw == 1u && mh == 1u)
            break;
        std::vector<uint8_t> next;
        uint32_t nw = 0, nh = 0;
        downsampleMip(cur.data(), mw, mh, next, nw, nh);
        cur.swap(next);
        mw = nw;
        mh = nh;
    }

    const std::string outDds = ddsPath(job.contentHash);
    if (!writeDdsFile(outDds, out.width, out.height, out.mips))
        return false;

    const uint64_t fsize = fs::file_size(outDds, ec);
    {
        std::lock_guard lock(m_mutex);
        m_diskHashes.insert(job.contentHash);
        m_diskBytes += fsize;
    }
    return true;
}

void AiTextureCache::workerLoop()
{
    while (true)
    {
        Job job{};
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [&] { return m_stop || !m_loadQueue.empty() || !m_genQueue.empty(); });
            if (m_stop)
                return;
            // Prioritize fast disk .dds loads over background AI generation.
            if (!m_loadQueue.empty())
            {
                job = std::move(m_loadQueue.front());
                m_loadQueue.pop_front();
            }
            else
            {
                job = std::move(m_genQueue.front());
                m_genQueue.pop_front();
            }
        }

        ReadyTexture ready{};
        ready.resolvedKey = job.resolvedKey;
        ready.contentHash = job.contentHash;
        bool ok = false;
        if (job.loadOnly)
        {
            ok = loadDds(ddsPath(job.contentHash), ready);
            if (ok)
                m_diskHits.fetch_add(1u, std::memory_order_relaxed);
        }
        else
        {
            ok = generateAndEncode(job, ready);
            if (ok)
                m_generated.fetch_add(1u, std::memory_order_relaxed);
        }

        if (ok)
        {
            std::lock_guard lock(m_readyMutex);
            m_ready.push_back(std::move(ready));
        }

        {
            std::lock_guard lock(m_mutex);
            m_inflight.erase(job.contentHash);
        }
        m_pendingJobs.fetch_sub(1u, std::memory_order_relaxed);
    }
}

} // namespace dq8::gfx::native
