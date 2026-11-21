#include "savestate.h"

#include "overlay/overlay_manager.h"
#include "ps2_runtime.h"
#include "Stubs/CD.h"
#include "draw_state_mirror.h"
#include "native_producer.h"
#include "scene_timing.h"
#include "Stubs/Pad.h"
#include "runtime/ee_scheduler.h"
#include "vif1_thread.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include <condition_variable>
#include <ctime>
#include <dlfcn.h>
#include <sys/stat.h>

namespace rg::savestate
{
namespace
{
using ps2_savestate::Reader;
using ps2_savestate::SaveStateError;
using ps2_savestate::Writer;

constexpr uint32_t kMagic = ps2_savestate::makeTag('R', 'G', 'S', 'S');
// Bumped with any change to what the files hold.
constexpr uint32_t kVersion = 2u;
constexpr auto kGiveUpAfter = std::chrono::seconds(5);

std::mutex g_messageMutex;
std::deque<std::string> g_messages;

void say(const std::string &text, bool player = true)
{
    std::fprintf(stderr, "[savestate] %s\n", text.c_str());
    if (player)
    {
        std::lock_guard lock(g_messageMutex);
        g_messages.push_back(text);
    }
}

// The settings folder: the one RG_SETTINGS_FILE is in, else the usual one.
std::filesystem::path stateDir()
{
    std::filesystem::path base;
    if (const char *file = std::getenv("RG_SETTINGS_FILE"); file && *file)
    {
        base = std::filesystem::path(file).parent_path();
    }
    else
    {
        const char *home = std::getenv("HOME");
        base = std::filesystem::path(home && *home ? home : ".") / ".local" / "share" / "RogueGalaxy";
    }
    return base / "states";
}

std::filesystem::path slotPath(int slot)
{
    if (slot == kAutoSlot)
        return stateDir() / "auto.rgs";
    return stateDir() / ("slot" + std::to_string(slot) + ".rgs");
}

// The automatic state is only good for the build that took it: a state holds the
// machine under that build's code, and a newer one (an update) would run it
// under other code. Its build is kept beside it, `auto.rev`; a state without it,
// or from another build, is not offered and is removed. The player's own slots
// are left to him.
std::filesystem::path autoRevisionPath()
{
    return stateDir() / "auto.rev";
}

void writeAutoRevision()
{
    std::ofstream file(autoRevisionPath(), std::ios::trunc);
    file << RG_REVISION << '\n';
}

bool autoStateFits()
{
    std::ifstream file(autoRevisionPath());
    std::string revision;
    std::getline(file, revision);
    if (file && revision == RG_REVISION)
        return true;
    std::error_code ec;
    std::filesystem::remove(slotPath(kAutoSlot), ec);
    std::filesystem::remove(autoRevisionPath(), ec);
    return false;
}

bool validSlot(int slot)
{
    return slot >= 1 && slot <= kSlots;
}

// The file starts with the magic, the version, the size of the RAM and the
// name of the overlay that was resident: the function table the hooks patch
// follows the overlay, so loading one makes the overlay it was saved under the
// resident one first (checkHeader tells which).
void writeHeader(Writer &out)
{
    out.put<uint32_t>(kMagic);
    out.put<uint32_t>(kVersion);
    out.put<uint32_t>(PS2_RAM_SIZE);
    const char *overlay = rg::overlay::residentName();
    const std::string name = overlay ? overlay : "";
    out.put<uint32_t>(static_cast<uint32_t>(name.size()));
    out.bytes(name.data(), name.size());
}

// Returns an empty string when the header fits this process, else why not;
// `overlay` is the one the state was saved under.
std::string checkHeader(Reader &in, std::string &overlay)
{
    if (in.get<uint32_t>() != kMagic)
        return "not a save state";
    if (in.get<uint32_t>() != kVersion)
        return "a save state of another version";
    if (in.get<uint32_t>() != PS2_RAM_SIZE)
        return "a save state of another memory size";
    const uint32_t length = in.get<uint32_t>();
    if (length > 256u)
        return "a damaged save state";
    std::string name(length, '\0');
    in.bytes(name.data(), length);
    overlay = name;
    return {};
}

// ---- the pad ports: which are open, in which mode. The runtime keeps that on the
// host side, outside the machine a state holds, so a state loaded by a process
// whose game has not opened a port yet (Continue, from the home screen) would
// have the game read "controller not connected". The ports' settings follow the
// machine as a trailer: a file without one (an older state) loads as before.
constexpr uint32_t kPadTag = 0x31444150u; // "PAD1"

void writePadTrailer(Writer &out)
{
    const ps2_stubs::PadDebugSnapshot snapshot = ps2_stubs::getPadDebugSnapshot();
    out.put<uint32_t>(kPadTag);
    for (size_t port = 0; port < ps2_stubs::kPadDebugPortCount; ++port)
    {
        const ps2_stubs::PadDebugPortSnapshot &p = snapshot.ports[port][0];
        out.put<uint8_t>(p.open ? 1u : 0u);
        out.put<uint8_t>(p.analogMode ? 1u : 0u);
        out.put<uint8_t>(p.pressureEnabled ? 1u : 0u);
        out.put<uint16_t>(p.buttonMask);
        out.put<uint32_t>(p.dmaAddr);
    }
}

// Opens the ports as the state's game had them, through the same calls the game
// makes: the buffer scePadPortOpen clears is put back as it was.
void readPadTrailer(Reader &in, PS2Runtime &runtime)
{
    if (in.remaining() < sizeof(uint32_t) || in.get<uint32_t>() != kPadTag)
        return;
    uint8_t *rdram = runtime.memory().getRDRAM();
    for (size_t port = 0; port < ps2_stubs::kPadDebugPortCount; ++port)
    {
        const bool open = in.get<uint8_t>() != 0u;
        const bool analog = in.get<uint8_t>() != 0u;
        const bool pressure = in.get<uint8_t>() != 0u;
        const uint16_t mask = in.get<uint16_t>();
        const uint32_t dma = in.get<uint32_t>();
        if (!open || dma == 0u || dma + 32u > PS2_RAM_SIZE)
            continue;
        R5900Context ctx{};
        const auto set = [&ctx](int reg, uint32_t value) { ctx.r[reg] = _mm_set_epi64x(0, value); };
        uint8_t buffer[32];
        std::memcpy(buffer, rdram + dma, sizeof(buffer));
        set(4, static_cast<uint32_t>(port));
        set(5, 0u);
        set(6, dma);
        ps2_stubs::scePadPortOpen(rdram, &ctx, &runtime);
        std::memcpy(rdram + dma, buffer, sizeof(buffer));
        if (analog)
        {
            set(6, 1u);
            ps2_stubs::scePadSetMainMode(rdram, &ctx, &runtime);
        }
        if (pressure)
            ps2_stubs::scePadEnterPressMode(rdram, &ctx, &runtime);
        set(6, mask);
        ps2_stubs::scePadSetButtonInfo(rdram, &ctx, &runtime);
    }
}

// ---- thumbnails: a small picture of the game at the moment of a save, beside
// the slot as "slotN.<time>.thumb.bmp". The picture is the presenter's, the
// state the executor's, and either may come first: whichever arrives second
// writes the file. The name changes with each picture: the menu's texture
// cache keeps a file's pixels for as long as the process runs.
constexpr uint32_t kThumbnailWidth = 192;

struct Thumbnail
{
    std::vector<uint8_t> rgb; // top row first
    uint32_t width = 0, height = 0;
};
std::mutex g_thumbMutex;
Thumbnail g_thumbPending[kSlots + 1];
bool g_thumbAwaiting[kSlots + 1] = {};

std::string thumbnailPrefix(int slot)
{
    return "slot" + std::to_string(slot) + ".";
}

bool isThumbnailOf(const std::filesystem::path &path, int slot)
{
    const std::string name = path.filename().string();
    return name.rfind(thumbnailPrefix(slot), 0) == 0 && name.size() > 10 &&
           name.compare(name.size() - 10, 10, ".thumb.bmp") == 0;
}

void removeThumbnails(int slot)
{
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(stateDir(), ec))
        if (isThumbnailOf(entry.path(), slot))
            std::filesystem::remove(entry.path(), ec);
}

// A 24-bit BMP, which the menu's picture loader reads.
void writeBmp(const std::filesystem::path &path, const Thumbnail &picture)
{
    const uint32_t rowBytes = (picture.width * 3u + 3u) & ~3u;
    const uint32_t imageBytes = rowBytes * picture.height;
    std::vector<uint8_t> file(54u + imageBytes, 0u);
    const auto put32 = [&](size_t at, uint32_t value) { std::memcpy(&file[at], &value, 4); };
    file[0] = 'B';
    file[1] = 'M';
    put32(2, static_cast<uint32_t>(file.size()));
    put32(10, 54u);
    put32(14, 40u);
    put32(18, picture.width);
    put32(22, picture.height); // positive: rows bottom first
    const uint16_t planes = 1, bits = 24;
    std::memcpy(&file[26], &planes, 2);
    std::memcpy(&file[28], &bits, 2);
    put32(34, imageBytes);
    for (uint32_t y = 0; y < picture.height; ++y)
    {
        uint8_t *row = &file[54u + (picture.height - 1u - y) * rowBytes];
        const uint8_t *source = &picture.rgb[static_cast<size_t>(y) * picture.width * 3u];
        for (uint32_t x = 0; x < picture.width; ++x)
        {
            row[x * 3u + 0] = source[x * 3u + 2];
            row[x * 3u + 1] = source[x * 3u + 1];
            row[x * 3u + 2] = source[x * 3u + 0];
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
}

// With g_thumbMutex held.
void writeThumbnailLocked(int slot)
{
    std::error_code ec;
    std::filesystem::create_directories(stateDir(), ec);
    removeThumbnails(slot);
    writeBmp(stateDir() / (thumbnailPrefix(slot) + std::to_string(static_cast<long long>(std::time(nullptr))) + "." +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 100000) +
                           ".thumb.bmp"),
             g_thumbPending[slot]);
    g_thumbPending[slot] = {};
    g_thumbAwaiting[slot] = false;
}

// The state of `slot` is on disk: its picture goes with it, now or when it
// arrives. One that never comes leaves no picture rather than the old state's.
void thumbnailSaved(int slot)
{
    std::lock_guard lock(g_thumbMutex);
    if (!g_thumbPending[slot].rgb.empty())
    {
        writeThumbnailLocked(slot);
        return;
    }
    removeThumbnails(slot);
    g_thumbAwaiting[slot] = true;
}

// ---- the file: a small header, then the stream, zstd-compressed when the
// library is there. libzstd is loaded when first needed rather than linked: the
// build container has no development files for it, and the stream is the same
// without it, only four times larger (a slot is 42 MB raw, 11 MB compressed,
// in 0.05 s and 0.04 s).
constexpr uint32_t kFileMagic = ps2_savestate::makeTag('R', 'G', 'S', 'F');
enum Codec : uint32_t
{
    kRaw = 0,
    kZstd = 1,
};
struct FileHeader
{
    uint32_t magic = kFileMagic;
    uint32_t codec = kRaw;
    uint64_t rawSize = 0;
};
constexpr int kZstdLevel = 3;

struct Zstd
{
    size_t (*compressBound)(size_t) = nullptr;
    size_t (*compress)(void *, size_t, const void *, size_t, int) = nullptr;
    size_t (*decompress)(void *, size_t, const void *, size_t) = nullptr;
    unsigned (*isError)(size_t) = nullptr;
};

const Zstd *zstd()
{
    static const Zstd *library = []() -> const Zstd *
    {
        void *handle = dlopen("libzstd.so.1", RTLD_NOW);
        if (!handle)
            return nullptr;
        static Zstd functions;
        functions.compressBound = reinterpret_cast<decltype(functions.compressBound)>(dlsym(handle, "ZSTD_compressBound"));
        functions.compress = reinterpret_cast<decltype(functions.compress)>(dlsym(handle, "ZSTD_compress"));
        functions.decompress = reinterpret_cast<decltype(functions.decompress)>(dlsym(handle, "ZSTD_decompress"));
        functions.isError = reinterpret_cast<decltype(functions.isError)>(dlsym(handle, "ZSTD_isError"));
        if (!functions.compressBound || !functions.compress || !functions.decompress || !functions.isError)
            return nullptr;
        return &functions;
    }();
    return library;
}

// The file's bytes for a stream: header and payload.
std::vector<uint8_t> encodeFile(const std::vector<uint8_t> &raw)
{
    FileHeader header;
    header.rawSize = raw.size();
    std::vector<uint8_t> file;
    if (const Zstd *z = zstd())
    {
        const size_t bound = z->compressBound(raw.size());
        file.resize(sizeof(FileHeader) + bound);
        const size_t packed = z->compress(file.data() + sizeof(FileHeader), bound, raw.data(), raw.size(), kZstdLevel);
        if (!z->isError(packed))
        {
            header.codec = kZstd;
            file.resize(sizeof(FileHeader) + packed);
            std::memcpy(file.data(), &header, sizeof(header));
            return file;
        }
    }
    file.resize(sizeof(FileHeader) + raw.size());
    std::memcpy(file.data(), &header, sizeof(header));
    std::memcpy(file.data() + sizeof(FileHeader), raw.data(), raw.size());
    return file;
}

// The stream in a file's bytes; empty, with the reason, when it cannot be had.
// A file without the header is a stream as the first build wrote it.
std::vector<uint8_t> decodeFile(std::vector<uint8_t> file, std::string &why)
{
    uint32_t magic = 0;
    if (file.size() >= sizeof(magic))
        std::memcpy(&magic, file.data(), sizeof(magic));
    if (magic != kFileMagic)
        return file;
    FileHeader header;
    if (file.size() < sizeof(header))
    {
        why = "a damaged save state";
        return {};
    }
    std::memcpy(&header, file.data(), sizeof(header));
    const uint8_t *payload = file.data() + sizeof(header);
    const size_t payloadSize = file.size() - sizeof(header);
    if (header.rawSize > (256ull << 20))
    {
        why = "a damaged save state";
        return {};
    }
    if (header.codec == kRaw)
    {
        if (payloadSize != header.rawSize)
        {
            why = "a damaged save state";
            return {};
        }
        return std::vector<uint8_t>(payload, payload + payloadSize);
    }
    const Zstd *z = header.codec == kZstd ? zstd() : nullptr;
    if (!z)
    {
        why = "this save state needs libzstd, which is not on this system";
        return {};
    }
    std::vector<uint8_t> raw(static_cast<size_t>(header.rawSize));
    const size_t got = z->decompress(raw.data(), raw.size(), payload, payloadSize);
    if (z->isError(got) || got != raw.size())
    {
        why = "a damaged save state";
        return {};
    }
    return raw;
}

// A file being written, per slot: a load waits for it. And a count, so the
// game does not end with one half done (flush()).
std::mutex g_slotLock[kSlots + 1];
std::mutex g_writerMutex;
std::condition_variable g_writerDone;
int g_writers = 0;

struct Pending
{
    std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
    std::string lastReason;
    // Set once the request is over, the file's writing started or the try
    // given up: what autoSaveBlocking waits for.
    std::atomic<bool> finished{false};
    std::atomic<bool> saved{false};
};

void doSave(PS2Runtime &runtime, int slot, std::shared_ptr<Pending> pending, bool &done)
{
    // The lists the game started are still running on the VIF1 thread; wait
    // for them so the memory and the registers belong to one moment.
    rg::vif1::drainForSnapshot();

    Writer out;
    writeHeader(out);
    std::string why;
    const bool taken = runtime.eeScheduler().saveMachine(out, why);
    if (taken)
    {
        writePadTrailer(out);
        ps2_stubs::cdSaveState(out, &runtime);
        rg::nativeproducer::saveState(out);
        rg::drawstate::saveState(out);
        rg::timing::saveState(out);
        rg::nativeproducer::saveFills(out);
    }
    if (!taken)
    {
        pending->lastReason = why;
        if (std::chrono::steady_clock::now() - pending->since < kGiveUpAfter)
        {
            done = false;
            return;
        }
        if (slot == kAutoSlot)
        {
            // An old state would take the player back to where he no longer is.
            std::error_code ec;
            std::filesystem::remove(slotPath(slot), ec);
            std::fprintf(stderr, "[savestate] no automatic state: %s\n", why.c_str());
        }
        else
        {
            say("Could not save: " + why);
        }
        done = true;
        return;
    }

    // Compressing and writing take longer than a frame: off the game's thread,
    // the stream handed over. The slot's old file stays until the new one is
    // whole.
    {
        std::lock_guard lock(g_writerMutex);
        ++g_writers;
    }
    std::thread(
        [slot, since = pending->since, raw = std::move(out.buffer())]
        {
            std::lock_guard slotLock(g_slotLock[slot]);
            const std::vector<uint8_t> file = encodeFile(raw);
            std::error_code ec;
            std::filesystem::create_directories(stateDir(), ec);
            const std::filesystem::path path = slotPath(slot);
            const std::filesystem::path temp = path.string() + ".tmp";
            bool ok = false;
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
                ok = static_cast<bool>(out);
            }
            if (ok)
            {
                std::filesystem::rename(temp, path, ec);
                ok = !ec;
            }
            if (ok)
            {
                std::fprintf(stderr, "[savestate] slot %d: %zu bytes (%zu on disk), after %.2f s\n", slot, raw.size(),
                             file.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count());
                if (slot == kAutoSlot)
                {
                    writeAutoRevision();
                }
                else
                {
                    say("Saved to slot " + std::to_string(slot));
                    thumbnailSaved(slot);
                }
            }
            else
            {
                say("Could not write the save state");
            }
            std::lock_guard lock(g_writerMutex);
            --g_writers;
            g_writerDone.notify_all();
        })
        .detach();
    pending->saved = true;
    done = true;
}

// Continue's load waits for the boot (requestAutoLoad) up to this long, and
// the vblanks the guest runs first (1.5 s).
constexpr auto kBootWait = std::chrono::seconds(20);
constexpr uint64_t kBootTicks = 90;
// Until when Continue's load may be asked again.
std::chrono::steady_clock::time_point g_autoLoadDeadline{};

void doLoad(PS2Runtime &runtime, int slot, bool &done)
{
    done = true;
    std::vector<uint8_t> bytes;
    {
        // A save still being written to this slot is waited for.
        std::lock_guard slotLock(g_slotLock[slot]);
        if (slot == kAutoSlot && !autoStateFits())
            return;
        std::ifstream file(slotPath(slot), std::ios::binary | std::ios::ate);
        if (!file)
        {
            if (slot != kAutoSlot)
                say("Slot " + std::to_string(slot) + " is empty");
            return;
        }
        const std::streamsize size = file.tellg();
        file.seekg(0);
        std::vector<uint8_t> packed(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char *>(packed.data()), size))
        {
            say("Could not read the save state");
            return;
        }
        std::string why;
        bytes = decodeFile(std::move(packed), why);
        if (bytes.empty())
        {
            say("Could not load: " + why);
            return;
        }
    }

    rg::vif1::drainForSnapshot();
    try
    {
        Reader in(bytes.data(), bytes.size());
        std::string overlay;
        const std::string problem = checkHeader(in, overlay);
        if (!problem.empty())
        {
            say("Could not load: " + problem);
            return;
        }
        const std::string before = rg::overlay::residentName();
        if (overlay != before && !rg::overlay::makeResident(overlay.c_str()))
        {
            say("Could not load: saved in an area this build does not have (" + overlay + ")");
            return;
        }
        try
        {
            runtime.eeScheduler().loadMachine(in);
            readPadTrailer(in, runtime);
            // The disc's files and stream (a state from before has none).
            if (in.remaining() >= sizeof(uint32_t))
            {
                ps2_stubs::cdLoadState(in, &runtime);
                // The renderer's side of the picture (a state from before has none).
                if (in.remaining() >= sizeof(uint32_t))
                {
                    rg::nativeproducer::loadState(in);
                    rg::drawstate::loadState(in);
                    if (in.remaining() >= sizeof(uint32_t))
                    {
                        rg::timing::loadState(in);
                        if (in.remaining() >= sizeof(uint32_t))
                            rg::nativeproducer::loadFills(in);
                    }
                }
            }
        }
        catch (...)
        {
            // The machine is as it was: so is the code that goes with it.
            rg::overlay::makeResident(before.c_str());
            throw;
        }
    }
    catch (const SaveStateError &error)
    {
        // The scheduler's part is read before anything changes, the memory's
        // is not: a failure here may have left the machine half loaded.
        // Continue loads while the game boots, which plays its logos as movies:
        // the load asks again until the movie ends (it refused before it
        // touched anything).
        if (slot == kAutoSlot && std::strstr(error.what(), "movie") &&
            std::chrono::steady_clock::now() < g_autoLoadDeadline)
        {
            done = false;
            return;
        }
        say(std::string("Load failed: ") + error.what());
        return;
    }
    if (slot != kAutoSlot)
        say("Loaded slot " + std::to_string(slot));
}

} // namespace

void requestSave(PS2Runtime &runtime, int slot)
{
    if (!validSlot(slot))
        return;
    {
        // A picture left from an earlier request is not this save's.
        std::lock_guard lock(g_thumbMutex);
        g_thumbPending[slot] = {};
        g_thumbAwaiting[slot] = false;
    }
    auto pending = std::make_shared<Pending>();
    runtime.eeScheduler().requestExecutorTask(
        [&runtime, slot, pending]
        {
            bool done = true;
            doSave(runtime, slot, pending, done);
            return done;
        });
}

void requestLoad(PS2Runtime &runtime, int slot)
{
    if (!validSlot(slot))
        return;
    runtime.eeScheduler().requestExecutorTask(
        [&runtime, slot]
        {
            bool done = true;
            doLoad(runtime, slot, done);
            return done;
        });
}

namespace
{
std::shared_ptr<Pending> queueAutoSave(PS2Runtime &runtime)
{
    auto pending = std::make_shared<Pending>();
    runtime.eeScheduler().requestExecutorTask(
        [&runtime, pending]
        {
            bool done = true;
            doSave(runtime, kAutoSlot, pending, done);
            if (done)
                pending->finished = true;
            return done;
        });
    return pending;
}
} // namespace

void requestAutoSave(PS2Runtime &runtime)
{
    queueAutoSave(runtime);
}

bool autoSaveBlocking(PS2Runtime &runtime, int timeoutMs, const std::function<void()> &whileWaiting)
{
    const auto pending = queueAutoSave(runtime);
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!pending->finished && std::chrono::steady_clock::now() < until)
    {
        if (whileWaiting)
            whileWaiting();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!pending->finished)
    {
        // Not taken in time, and maybe never: the old state would be a lie.
        std::error_code ec;
        std::filesystem::remove(slotPath(kAutoSlot), ec);
        std::fprintf(stderr, "[savestate] no automatic state: not taken in %d ms\n", timeoutMs);
        return false;
    }
    flush();
    return pending->saved;
}

void requestAutoLoad(PS2Runtime &runtime)
{
    // Continue is pressed on the home screen, with the guest not yet run: the
    // host's side of the boot (the sound driver on the IOP and its audio
    // stream, the RPC tables, the game's pad ports) does not exist, and a
    // state cannot bring it (it is not in it). The game boots first, as for
    // Play, and the state loads once it has opened its pad: no sound and
    // broken textures otherwise.
    const auto started = std::chrono::steady_clock::now();
    g_autoLoadDeadline = started + kBootWait;
    const uint64_t firstTick = runtime.eeScheduler().currentVSyncTick();
    auto lastTry = std::make_shared<std::chrono::steady_clock::time_point>();
    runtime.eeScheduler().requestExecutorTask(
        [&runtime, started, firstTick, lastTry]
        {
            const auto now = std::chrono::steady_clock::now();
            const bool booted = runtime.eeScheduler().currentVSyncTick() >= firstTick + kBootTicks &&
                                ps2_stubs::getPadDebugSnapshot().ports[0][0].open;
            if (!booted && now - started < kBootWait)
                return false;
            // A refused load asks again; not every pass.
            if (*lastTry != std::chrono::steady_clock::time_point{} && now - *lastTry < std::chrono::milliseconds(200))
                return false;
            *lastTry = now;
            bool done = true;
            doLoad(runtime, kAutoSlot, done);
            return done;
        });
}

SlotInfo autoSlot()
{
    SlotInfo entry;
    struct stat info{};
    if (::stat(slotPath(kAutoSlot).c_str(), &info) != 0 || info.st_size <= 0)
        return entry;
    if (!autoStateFits())
        return entry;
    entry.used = true;
    std::tm local{};
    const time_t when = info.st_mtime;
    localtime_r(&when, &local);
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
    entry.when = text;
    return entry;
}

bool takeMessage(std::string &text)
{
    std::lock_guard lock(g_messageMutex);
    if (g_messages.empty())
        return false;
    text = std::move(g_messages.front());
    g_messages.pop_front();
    return true;
}

void offerThumbnail(int slot, const uint8_t *rgba, uint32_t width, uint32_t height)
{
    if (!validSlot(slot) || !rgba || width == 0u || height == 0u)
        return;
    // A box filter down to kThumbnailWidth, the picture's own shape.
    Thumbnail small;
    small.width = std::min(kThumbnailWidth, width);
    small.height = std::max(1u, static_cast<uint32_t>(static_cast<uint64_t>(small.width) * height / width));
    small.rgb.resize(static_cast<size_t>(small.width) * small.height * 3u);
    for (uint32_t y = 0; y < small.height; ++y)
    {
        const uint32_t y0 = static_cast<uint32_t>(static_cast<uint64_t>(y) * height / small.height);
        const uint32_t y1 = std::max(y0 + 1u, static_cast<uint32_t>(static_cast<uint64_t>(y + 1u) * height / small.height));
        for (uint32_t x = 0; x < small.width; ++x)
        {
            const uint32_t x0 = static_cast<uint32_t>(static_cast<uint64_t>(x) * width / small.width);
            const uint32_t x1 = std::max(x0 + 1u, static_cast<uint32_t>(static_cast<uint64_t>(x + 1u) * width / small.width));
            uint32_t sum[3] = {0, 0, 0}, count = 0;
            for (uint32_t sy = y0; sy < y1; ++sy)
                for (uint32_t sx = x0; sx < x1; ++sx)
                {
                    const uint8_t *pixel = rgba + (static_cast<size_t>(sy) * width + sx) * 4u;
                    sum[0] += pixel[0];
                    sum[1] += pixel[1];
                    sum[2] += pixel[2];
                    ++count;
                }
            uint8_t *out = &small.rgb[(static_cast<size_t>(y) * small.width + x) * 3u];
            for (int c = 0; c < 3; ++c)
                out[c] = static_cast<uint8_t>(sum[c] / count);
        }
    }
    std::lock_guard lock(g_thumbMutex);
    g_thumbPending[slot] = std::move(small);
    if (g_thumbAwaiting[slot])
        writeThumbnailLocked(slot);
}

void flush()
{
    std::unique_lock lock(g_writerMutex);
    g_writerDone.wait_for(lock, std::chrono::seconds(10), [] { return g_writers == 0; });
}

std::vector<SlotInfo> slots()
{
    std::vector<SlotInfo> result(kSlots);
    for (int slot = 1; slot <= kSlots; ++slot)
    {
        struct stat info{};
        if (::stat(slotPath(slot).c_str(), &info) != 0 || info.st_size <= 0)
            continue;
        SlotInfo &entry = result[static_cast<size_t>(slot - 1)];
        entry.used = true;
        std::tm local{};
        const time_t when = info.st_mtime;
        localtime_r(&when, &local);
        char text[32];
        std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
        entry.when = text;
        std::error_code ec;
        for (const auto &file : std::filesystem::directory_iterator(stateDir(), ec))
            if (isThumbnailOf(file.path(), slot))
                entry.picture = file.path().string();
    }
    return result;
}

void install(PS2Runtime &runtime)
{
    const char *script = std::getenv("RG_STATE_SCRIPT");
    if (!script || !*script)
        return;
    struct Step
    {
        bool save;
        int slot;
        double at;
    };
    std::vector<Step> steps;
    // "save 1@30": a command word, then slot@seconds.
    std::istringstream again(script);
    std::string command;
    for (std::string word; again >> word;)
    {
        if (word.find('@') == std::string::npos)
        {
            command = word;
            continue;
        }
        const size_t at = word.find('@');
        steps.push_back({command == "save", std::atoi(word.substr(0, at).c_str()),
                         std::atof(word.substr(at + 1).c_str())});
    }
    std::fprintf(stderr, "[savestate] script: %zu step(s)\n", steps.size());
    std::thread(
        [&runtime, steps]
        {
            const auto start = std::chrono::steady_clock::now();
            for (const Step &step : steps)
            {
                std::this_thread::sleep_until(start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                          std::chrono::duration<double>(step.at)));
                std::fprintf(stderr, "[savestate] script: %s slot %d\n", step.save ? "save" : "load", step.slot);
                if (step.save)
                    requestSave(runtime, step.slot);
                else if (step.slot == kAutoSlot)
                    requestAutoLoad(runtime); // Continue's state, as the home screen loads it
                else
                    requestLoad(runtime, step.slot);
            }
        })
        .detach();
}

} // namespace rg::savestate
