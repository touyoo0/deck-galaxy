#pragma once

#include "gfx/native/native_records.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace dq8::gfx::native
{

// Content-addressed local disk cache of 4x AI-upscaled BC7 (.dds) textures
// with an asynchronous background worker that:
//   1. Loads existing <hash>.dds BC7 files off the render thread (0 ms stall).
//   2. On cache miss for 3D textures, optionally invokes realesrgan-ncnn-vulkan
//      in the background, builds a premultiplied-alpha mipmap chain, encodes
//      it to BC7 (DXGI_FORMAT_BC7_UNORM), saves <hash>.dds atomically, and
//      hands the compressed mipchain back to the renderer for hot-swap into
//      m_resolved (0% GPU cost per frame, 4x VRAM savings vs RGBA8).
class AiTextureCache
{
public:
    struct MipLevel
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> bc7Data;
    };

    struct ReadyTexture
    {
        uint64_t resolvedKey = 0;
        uint64_t contentHash = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<MipLevel> mips;
    };

    AiTextureCache() = default;
    ~AiTextureCache();

    AiTextureCache(const AiTextureCache &) = delete;
    AiTextureCache &operator=(const AiTextureCache &) = delete;

    // Initializes cache directory, scans existing .dds files, detects
    // realesrgan-ncnn-vulkan, and starts the background worker thread.
    void init(bool bc7Supported);

    // Stops the background worker and clears queues.
    void shutdown();

    bool enabled() const { return m_enabled; }
    bool upscalerAvailable() const { return m_upscalerAvailable; }

    // Fast 64-bit content hash for RGBA8 pixels + dimensions.
    static uint64_t hashRgba(uint32_t width, uint32_t height, const uint8_t *rgba, size_t byteSize);

    // Non-blocking request from the render thread. Schedules an async .dds load
    // on cache hit, or an async 4x ncnn-vulkan upscale + BC7 encode on miss.
    void request(uint64_t resolvedKey, uint64_t contentHash, uint32_t width, uint32_t height, Bytes rgba,
                 bool allowGenerate);

    // Drains all BC7 textures ready for GPU upload (non-blocking).
    void takeReady(std::vector<ReadyTexture> &out);

    // Stats for logging.
    uint64_t diskHits() const { return m_diskHits.load(std::memory_order_relaxed); }
    uint64_t generated() const { return m_generated.load(std::memory_order_relaxed); }
    uint64_t pendingJobs() const { return m_pendingJobs.load(std::memory_order_relaxed); }

private:
    struct Job
    {
        uint64_t resolvedKey = 0;
        uint64_t contentHash = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        Bytes rgba;
        bool loadOnly = false;
    };

    void workerLoop();
    bool loadDds(const std::string &path, ReadyTexture &out);
    bool generateAndEncode(const Job &job, ReadyTexture &out);
    std::string ddsPath(uint64_t contentHash) const;

    bool m_initialized = false;
    bool m_enabled = false;
    bool m_upscalerAvailable = false;
    bool m_debug = false;
    std::string m_cacheDir;
    std::string m_tmpDir;
    std::string m_upscalerBin;
    std::string m_modelDir;
    std::string m_modelName;
    uint64_t m_maxDiskBytes = 2048ull * 1024ull * 1024ull;
    uint64_t m_diskBytes = 0;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false;
    std::unordered_set<uint64_t> m_diskHashes;
    std::unordered_set<uint64_t> m_inflight;
    std::deque<Job> m_loadQueue;
    std::deque<Job> m_genQueue;

    std::mutex m_readyMutex;
    std::vector<ReadyTexture> m_ready;

    std::thread m_worker;
    std::atomic<uint64_t> m_diskHits{0};
    std::atomic<uint64_t> m_generated{0};
    std::atomic<uint64_t> m_pendingJobs{0};
};

} // namespace dq8::gfx::native
