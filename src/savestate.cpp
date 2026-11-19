#include "savestate.h"

#include "overlay/overlay_manager.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "vif1_thread.h"

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

#include <ctime>
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

std::atomic<int> g_slot{1};
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
    return stateDir() / ("slot" + std::to_string(slot) + ".rgs");
}

bool validSlot(int slot)
{
    return slot >= 1 && slot <= kSlots;
}

// The file starts with the magic, the version, the size of the RAM and the
// name of the overlay that was resident: the function table the hooks patch
// follows the overlay, so a state is only loaded under the one it was saved in.
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

// Returns an empty string when the header fits this process, else why not.
std::string checkHeader(Reader &in)
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
    const char *overlay = rg::overlay::residentName();
    if (name != (overlay ? overlay : ""))
        return "saved in another area of the game (" + name + "), load it from there";
    return {};
}

struct Pending
{
    std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
    std::string lastReason;
};

void doSave(PS2Runtime &runtime, int slot, std::shared_ptr<Pending> pending, bool &done)
{
    // The lists the game started are still running on the VIF1 thread; wait
    // for them so the memory and the registers belong to one moment.
    rg::vif1::drainForSnapshot();

    Writer out;
    writeHeader(out);
    std::string why;
    if (!runtime.eeScheduler().saveMachine(out, why))
    {
        pending->lastReason = why;
        if (std::chrono::steady_clock::now() - pending->since < kGiveUpAfter)
        {
            done = false;
            return;
        }
        say("Could not save: " + why);
        done = true;
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(stateDir(), ec);
    const std::filesystem::path path = slotPath(slot);
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(out.buffer().data()),
                   static_cast<std::streamsize>(out.buffer().size()));
        if (!file)
        {
            say("Could not write the save state");
            done = true;
            return;
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec)
    {
        say("Could not write the save state: " + ec.message());
        done = true;
        return;
    }
    say("Saved to slot " + std::to_string(slot));
    std::fprintf(stderr, "[savestate] slot %d: %zu bytes, after %.2f s\n", slot, out.buffer().size(),
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - pending->since).count());
    done = true;
}

void doLoad(PS2Runtime &runtime, int slot, bool &done)
{
    done = true;
    std::ifstream file(slotPath(slot), std::ios::binary | std::ios::ate);
    if (!file)
    {
        say("Slot " + std::to_string(slot) + " is empty");
        return;
    }
    const std::streamsize size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char *>(bytes.data()), size))
    {
        say("Could not read the save state");
        return;
    }

    rg::vif1::drainForSnapshot();
    try
    {
        Reader in(bytes.data(), bytes.size());
        const std::string problem = checkHeader(in);
        if (!problem.empty())
        {
            say("Could not load: " + problem);
            return;
        }
        runtime.eeScheduler().loadMachine(in);
    }
    catch (const SaveStateError &error)
    {
        // The scheduler's part is read before anything changes, the memory's
        // is not: a failure here may have left the machine half loaded.
        say(std::string("Load failed: ") + error.what());
        return;
    }
    say("Loaded slot " + std::to_string(slot));
}

} // namespace

void requestSave(PS2Runtime &runtime, int slot)
{
    if (!validSlot(slot))
        return;
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

bool takeMessage(std::string &text)
{
    std::lock_guard lock(g_messageMutex);
    if (g_messages.empty())
        return false;
    text = std::move(g_messages.front());
    g_messages.pop_front();
    return true;
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
    }
    return result;
}

int currentSlot()
{
    return g_slot.load(std::memory_order_relaxed);
}

void setCurrentSlot(int slot)
{
    if (validSlot(slot))
        g_slot.store(slot, std::memory_order_relaxed);
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
                else
                    requestLoad(runtime, step.slot);
            }
        })
        .detach();
}

} // namespace rg::savestate
