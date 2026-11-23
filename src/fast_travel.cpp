#include "fast_travel.h"

#include <smmintrin.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include "ps2_runtime.h"
#include "return_hook.h"
#include "runtime/ps2_memory.h"
#include "scene_timing.h"

namespace rg::fasttravel
{
namespace
{

// PadIsTriggered(pad, mask) and the calls of FieldExploreModeUpdate's
// phases around the full map.
constexpr uint32_t kPadIsTriggered = 0x001839F0u;
constexpr uint32_t kFullMapCloseReturn = 0x00283DE4u; // the jal at 0x283DDC: Select closes the map
constexpr uint32_t kFieldSelectReturn = 0x0028377Cu;  // the jal at 0x283774: Select opens it

// Guest functions called from the host (they only read, or queue, as the
// game's own code does at these points).
constexpr uint32_t kGetGameData = 0x001FEDE0u;
constexpr uint32_t kGetMapMgr = 0x00201A90u;
constexpr uint32_t kGetCurrentScene = 0x001F5BE0u;
constexpr uint32_t kFuncPointGetPos = 0x0025AE60u;
constexpr uint32_t kFuncPointGetActiveId = 0x00201A00u;
constexpr uint32_t kGetActionMgr = 0x001FDB50u;
constexpr uint32_t kActionMgrFindById = 0x00233B50u;
constexpr uint32_t kCharaGetPos = 0x002388A0u;
constexpr uint32_t kScreenFadeIn = 0x001CF740u;
constexpr uint32_t kMapSlotsFindBlockById = 0x001E49B0u;
constexpr uint32_t kGroupViewCtor = 0x00425440u;
constexpr uint32_t kGroupGetRecordById = 0x00425590u;
// The active transporter's id (FuncPointGetActiveId reads it), -1 when none.
constexpr uint32_t kActiveFuncPoint = 0x00481B2Cu;
constexpr uint32_t kViewIterBegin = 0x00370170u;
constexpr uint32_t kViewIterNext = 0x00370040u;
constexpr uint32_t kGameReqQueuePushWarp = 0x001F5AF0u;
constexpr uint32_t kGameReqQueuePushBack = 0x001F5B50u;

// Pad 1: its buttons held this frame at +4 and last frame's at +0x5C, off
// when +0x24C is not zero (PadIsTriggered).
constexpr uint32_t kPad1 = 0x004B2570u;
constexpr uint32_t kButtonCross = 0x0040u;
constexpr uint32_t kButtonUp = 0x1000u;
constexpr uint32_t kButtonRight = 0x2000u;
constexpr uint32_t kButtonDown = 0x4000u;
constexpr uint32_t kButtonLeft = 0x8000u;

// The warp record the transporter menu fills, and the float it adds to the
// destination's angle (gp-0x7F74).
constexpr uint32_t kWarpRecord = 0x004D7E00u;
constexpr uint32_t kAngleBase = 0x0047D1FCu;

// A transporter record (the game's data, read in place): id at +0, its
// block at +0x18, its position and angle at +0x20..+0x2C, bit 0 of the
// word at +0x40 once activated.
constexpr uint32_t kRecId = 0x00;
constexpr uint32_t kRecBlock = 0x18;
constexpr uint32_t kRecPos = 0x20;
constexpr uint32_t kRecAngle = 0x2C;
constexpr uint32_t kRecState = 0x40;

constexpr uint32_t kScratchSize = 0x100u;
constexpr uint32_t kIterOffset = 0x80u;
constexpr size_t kMaxPoints = 64;

PS2Runtime::RecompiledFunction g_padIsTriggered = nullptr;
PS2Runtime::RecompiledFunction g_funcPointGetPos = nullptr;
PS2Runtime *g_runtime = nullptr;
std::atomic<bool> g_enabled{false};

struct Point
{
    uint32_t record = 0;
    int32_t id = 0;
    int32_t block = 0;
    std::string name;
};
// Built when the full map opens; empty while it is closed.
std::vector<Point> g_points;
bool g_open = false;
int g_selected = 0;
uint32_t g_scratch = 0;
// A warp has been queued: the field's first frame after the warp ends (the
// field's Select test is not asked while the warp's modes run) gets the
// fade in the Teleport page's phase 0x12D asks for, which the warp's own
// modes do not.
bool g_arrivalPending = false;
// The departure script (0x320) puts the light at the active transporter's
// position (FuncPointGetPos): for the next call, the leader's own.
bool g_departOverride = false;
std::chrono::steady_clock::time_point g_lastFieldFrame;

std::mutex g_listMutex;
List g_list;

std::mutex g_toastMutex;
std::vector<Toast> g_toasts;

void toast(int index, int count)
{
    std::lock_guard<std::mutex> lock(g_toastMutex);
    g_toasts.push_back(Toast{index, count});
}

uint32_t read32(const uint8_t *rdram, uint32_t address)
{
    uint32_t value = 0;
    if (const uint8_t *p = getConstMemPtr(rdram, address))
    {
        std::memcpy(&value, p, sizeof(value));
    }
    return value;
}

int32_t read16s(const uint8_t *rdram, uint32_t address)
{
    int16_t value = 0;
    if (const uint8_t *p = getConstMemPtr(rdram, address))
    {
        std::memcpy(&value, p, sizeof(value));
    }
    return value;
}

float readFloat(const uint8_t *rdram, uint32_t address)
{
    float value = 0.0f;
    if (const uint8_t *p = getConstMemPtr(rdram, address))
    {
        std::memcpy(&value, p, sizeof(value));
    }
    return value;
}

void write16(uint8_t *rdram, uint32_t address, int16_t value)
{
    if (uint8_t *p = getMemPtr(rdram, address))
    {
        std::memcpy(p, &value, sizeof(value));
    }
}

void write32(uint8_t *rdram, uint32_t address, uint32_t value)
{
    if (uint8_t *p = getMemPtr(rdram, address))
    {
        std::memcpy(p, &value, sizeof(value));
    }
}

void writeFloat(uint8_t *rdram, uint32_t address, float value)
{
    if (uint8_t *p = getMemPtr(rdram, address))
    {
        std::memcpy(p, &value, sizeof(value));
    }
}

// Calls a guest function from the host with the caller's registers (its
// stack included), as text_language.cpp does, to its end. Arguments are signed words.
uint32_t callGuest(uint8_t *rdram, const R5900Context *caller, uint32_t address, int32_t a0 = 0, int32_t a1 = 0,
                   int32_t a2 = 0, int32_t a3 = 0)
{
    PS2Runtime::RecompiledFunction fn = g_runtime ? g_runtime->lookupFunction(address) : nullptr;
    if (!fn)
    {
        return 0;
    }
    R5900Context local = *caller;
    local.r[4] = _mm_set_epi64x(0, static_cast<int64_t>(a0));
    local.r[5] = _mm_set_epi64x(0, static_cast<int64_t>(a1));
    local.r[6] = _mm_set_epi64x(0, static_cast<int64_t>(a2));
    local.r[7] = _mm_set_epi64x(0, static_cast<int64_t>(a3));
    // Through the dispatcher: the game's indirect jumps (a virtual call that
    // tail-jumps) only complete there.
    if (!rg::hooks::runToEnd(fn, address, rdram, local, g_runtime))
    {
        return 0;
    }
    return getRegU32(&local, 2);
}


// A letter with the accent the European text writes as a tag, "<'e>": the
// accent's mark, then the letter.
const char *accented(char mark, char letter)
{
    struct Entry
    {
        char mark;
        char letter;
        const char *utf8;
    };
    static constexpr Entry kTable[] = {
        {'\'', 'a', "\u00e1"}, {'\'', 'e', "\u00e9"}, {'\'', 'i', "\u00ed"}, {'\'', 'o', "\u00f3"},
        {'\'', 'u', "\u00fa"}, {'\'', 'A', "\u00c1"}, {'\'', 'E', "\u00c9"}, {'\'', 'I', "\u00cd"},
        {'\'', 'O', "\u00d3"}, {'\'', 'U', "\u00da"}, {'`', 'a', "\u00e0"},  {'`', 'e', "\u00e8"},
        {'`', 'i', "\u00ec"},  {'`', 'o', "\u00f2"},  {'`', 'u', "\u00f9"},  {'`', 'A', "\u00c0"},
        {'`', 'E', "\u00c8"},  {'^', 'a', "\u00e2"},  {'^', 'e', "\u00ea"},  {'^', 'i', "\u00ee"},
        {'^', 'o', "\u00f4"},  {'^', 'u', "\u00fb"},  {'^', 'A', "\u00c2"},  {'^', 'E', "\u00ca"},
        {':', 'a', "\u00e4"},  {':', 'e', "\u00eb"},  {':', 'i', "\u00ef"},  {':', 'o', "\u00f6"},
        {':', 'u', "\u00fc"},  {':', 'A', "\u00c4"},  {':', 'O', "\u00d6"},  {':', 'U', "\u00dc"},
        {',', 'c', "\u00e7"},  {',', 'C', "\u00c7"},  {'~', 'n', "\u00f1"},  {'~', 'N', "\u00d1"},
    };
    for (const Entry &entry : kTable)
    {
        if (entry.mark == mark && entry.letter == letter)
        {
            return entry.utf8;
        }
    }
    return nullptr;
}

// The name a transporter's record points at (+8): ASCII with the accent tags
// of the European text, as UTF-8; "Transporter N" when it reads as nothing.
std::string recordName(const uint8_t *rdram, uint32_t record, int32_t id)
{
    std::string raw;
    const uint32_t pointer = read32(rdram, record + 8u);
    if (pointer >= 0x00100000u && pointer < 0x02000000u)
    {
        for (uint32_t i = 0; i < 48; ++i)
        {
            const uint8_t *p = getConstMemPtr(rdram, pointer + i);
            if (!p || *p == 0u)
            {
                break;
            }
            if (*p < 0x20u || *p > 0x7Eu)
            {
                raw.clear();
                break;
            }
            raw.push_back(static_cast<char>(*p));
        }
    }
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i)
    {
        if (raw[i] == '<' && i + 3 < raw.size() && raw[i + 3] == '>')
        {
            if (const char *letter = accented(raw[i + 1], raw[i + 2]))
            {
                out += letter;
                i += 3;
                continue;
            }
        }
        out.push_back(raw[i]);
    }
    return out.empty() ? "Transporter " + std::to_string(id) : out;
}

// What the menu draws: copied under the lock whenever the choice changes.
void publish(bool open)
{
    std::lock_guard<std::mutex> lock(g_listMutex);
    g_list.open = open;
    g_list.selected = g_selected;
    g_list.names.clear();
    if (open)
    {
        for (const Point &point : g_points)
        {
            g_list.names.push_back(point.name);
        }
    }
}

// The transporters the Teleport page lists: its own view over the group of
// records (TransWarpListBuild, 0x51B120), the ones activated.
void buildPoints(uint8_t *rdram, const R5900Context *caller)
{
    g_points.clear();
    g_selected = 0;
    if (!g_scratch)
    {
        g_scratch = g_runtime->guestMalloc(kScratchSize, 16u);
    }
    if (!g_scratch)
    {
        std::fprintf(stderr, "[fast-travel] no scratch memory\n");
        return;
    }
    std::memset(getMemPtr(rdram, g_scratch), 0, kScratchSize);
    const uint32_t gameData = callGuest(rdram, caller, kGetGameData);
    if (!gameData)
    {
        return;
    }
    const uint32_t here = read32(rdram, gameData + 0x18u);
    const int32_t area = read16s(rdram, here + 4u);
    const uint32_t group = read32(rdram, read32(rdram, gameData + 8u) + 0x74u);
    const uint32_t view = g_scratch;
    const uint32_t iter = g_scratch + kIterOffset;
    callGuest(rdram, caller, kGroupViewCtor, static_cast<int32_t>(view), static_cast<int32_t>(gameData), area, 1);
    callGuest(rdram, caller, kViewIterBegin, static_cast<int32_t>(iter), static_cast<int32_t>(view));
    for (int guard = 0; guard < 4096; ++guard)
    {
        uint32_t record = callGuest(rdram, caller, kViewIterNext, static_cast<int32_t>(iter));
        if (!record)
        {
            break;
        }
        // The first run listed 1 to 9, not addresses: the iterator gives the
        // record's key, the record is the group's entry for it.
        if (record < 0x00100000u)
        {
            const uint32_t key = record;
            record = callGuest(rdram, caller, kGroupGetRecordById, static_cast<int32_t>(group), static_cast<int32_t>(key));
            std::fprintf(stderr, "[fast-travel] key %u -> record %08x\n", key, record);
            if (!record)
            {
                continue;
            }
        }
        const uint32_t state = read32(rdram, record + kRecState);
        std::fprintf(stderr, "[fast-travel] record %08x id %d block %d state %08x pos %.1f %.1f %.1f\n", record,
                     read16s(rdram, record + kRecId), read16s(rdram, record + kRecBlock), state,
                     readFloat(rdram, record + kRecPos), readFloat(rdram, record + kRecPos + 4u),
                     readFloat(rdram, record + kRecPos + 8u));
        if ((state & 1u) == 0u || g_points.size() >= kMaxPoints)
        {
            continue;
        }
        Point p;
        p.record = record;
        p.id = read16s(rdram, record + kRecId);
        p.block = read16s(rdram, record + kRecBlock);
        p.name = recordName(rdram, record, p.id);
        std::fprintf(stderr, "[fast-travel] name \"%s\" (pointer %08x)\n", p.name.c_str(), read32(rdram, record + 8u));
        g_points.push_back(p);
    }
    std::fprintf(stderr, "[fast-travel] area %d: %zu activated\n", area, g_points.size());
}

void announce()
{
    if (g_points.empty())
    {
        toast(0, 0);
    }
    publish(true);
}

// The warp record the transporter menu fills for a Teleport choice
// (TransporterMenuModeUpdate 0x27CEBC to 0x27D040), then the requests of
// its phase 0x12C (0x27D054 to 0x27D098).
void travel(uint8_t *rdram, const R5900Context *caller, const Point &p)
{
    const uint32_t gameData = callGuest(rdram, caller, kGetGameData);
    if (!gameData)
    {
        return;
    }
    const uint32_t here = read32(rdram, gameData + 0x18u);
    const uint32_t mapMgr = callGuest(rdram, caller, kGetMapMgr);
    callGuest(rdram, caller, kMapSlotsFindBlockById, static_cast<int32_t>(mapMgr), p.block, 0);

    std::memset(getMemPtr(rdram, kWarpRecord), 0, 0x44);
    getMemPtr(rdram, kWarpRecord)[0] = 1;
    write16(rdram, kWarpRecord + 0x02, static_cast<int16_t>(read16s(rdram, here + 4u)));
    write16(rdram, kWarpRecord + 0x04, static_cast<int16_t>(read16s(rdram, here + 6u)));
    write16(rdram, kWarpRecord + 0x06, static_cast<int16_t>(p.block));
    write16(rdram, kWarpRecord + 0x08, static_cast<int16_t>(read16s(rdram, here + 0xCu)));
    write16(rdram, kWarpRecord + 0x0A, static_cast<int16_t>(read32(rdram, here + 0x10u)));
    writeFloat(rdram, kWarpRecord + 0x0C, readFloat(rdram, p.record + kRecPos));
    writeFloat(rdram, kWarpRecord + 0x10, readFloat(rdram, p.record + kRecPos + 4u));
    writeFloat(rdram, kWarpRecord + 0x14, readFloat(rdram, p.record + kRecPos + 8u));
    constexpr float kPi = 3.14159265358979f;
    float angle = readFloat(rdram, kAngleBase) + readFloat(rdram, p.record + kRecAngle);
    while (angle > kPi)
    {
        angle -= 2.0f * kPi;
    }
    while (angle < -kPi)
    {
        angle += 2.0f * kPi;
    }
    writeFloat(rdram, kWarpRecord + 0x18, angle);
    writeFloat(rdram, kWarpRecord + 0x1C, 28.0f);
    writeFloat(rdram, kWarpRecord + 0x20, -28.0f);
    writeFloat(rdram, kWarpRecord + 0x24, -28.0f);
    writeFloat(rdram, kWarpRecord + 0x28, -28.0f);

    const int32_t site = read16s(rdram, here + 0xAu);
    callGuest(rdram, caller, kGameReqQueuePushWarp, site, 0x320, 0, -1);
    callGuest(rdram, caller, kGameReqQueuePushBack, 5, -1);
    callGuest(rdram, caller, kGameReqQueuePushWarp, site, 0x321, 0, -1);
    publish(false);
    g_arrivalPending = true;
    g_departOverride = true;
    g_lastFieldFrame = std::chrono::steady_clock::now();
    std::fprintf(stderr, "[fast-travel] warp to transporter %d (block %d)\n", p.id, p.block);
}

// Once per frame while the full map is open, with the result the game
// would give to "Select pressed?"; returns what to give instead.
bool onFullMapFrame(uint8_t *rdram, R5900Context *ctx, bool selectPressed)
{
    if (!g_open)
    {
        g_open = true;
        buildPoints(rdram, ctx);
        announce();
    }
    const uint32_t held = read32(rdram, kPad1 + 4u);
    const uint32_t before = read32(rdram, kPad1 + 0x5Cu);
    const bool padOff = read32(rdram, kPad1 + 0x24Cu) != 0u;
    const uint32_t pressed = padOff ? 0u : (held & ~before);
    if (g_points.empty())
    {
        return selectPressed;
    }
    const int count = static_cast<int>(g_points.size());
    if (pressed & (kButtonRight | kButtonDown))
    {
        g_selected = (g_selected + 1) % count;
        announce();
    }
    else if (pressed & (kButtonLeft | kButtonUp))
    {
        g_selected = (g_selected + count - 1) % count;
        announce();
    }
    else if (pressed & kButtonCross)
    {
        travel(rdram, ctx, g_points[static_cast<size_t>(g_selected)]);
        return true; // the game closes the map as it does for Select
    }
    return selectPressed;
}

// EvCmdFuncPointGetPosMaybe reads the position of the active transporter's
// record into the script's variables. For the fast travel's departure the
// record, for that one call, holds the leader's position.
void funcPointGetPos(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (!g_departOverride)
    {
        g_funcPointGetPos(rdram, ctx, runtime);
        return;
    }
    g_departOverride = false;
    uint32_t record = 0;
    uint32_t savedActive = 0xFFFFFFFFu;
    float saved[3] = {};
    const uint32_t gameData = callGuest(rdram, ctx, kGetGameData);
    if (gameData && g_scratch)
    {
        uint32_t activeId = callGuest(rdram, ctx, kFuncPointGetActiveId);
        savedActive = activeId;
        if (activeId == 0xFFFFFFFFu && !g_points.empty())
        {
            // No transporter is active (the player is not on one): any record
            // of the list gives the script a place to read, which is the
            // leader's below.
            activeId = static_cast<uint32_t>(g_points.front().id);
            write32(rdram, kActiveFuncPoint, activeId);
        }
        const uint32_t group = read32(rdram, read32(rdram, gameData + 8u) + 0x74u);
        record = callGuest(rdram, ctx, kGroupGetRecordById, static_cast<int32_t>(group), static_cast<int32_t>(activeId));
        const uint32_t actionMgr = callGuest(rdram, ctx, kGetActionMgr);
        const uint32_t leaderId = static_cast<uint32_t>(read16s(rdram, read32(rdram, gameData + 0x18u) + 0x18Eu));
        const uint32_t leader = callGuest(rdram, ctx, kActionMgrFindById, static_cast<int32_t>(actionMgr),
                                          static_cast<int32_t>(leaderId));
        const uint32_t vec = g_scratch + 0xC0u;
        if (record && leader)
        {
            callGuest(rdram, ctx, kCharaGetPos, static_cast<int32_t>(leader), static_cast<int32_t>(vec));
            for (int i = 0; i < 3; ++i)
            {
                saved[i] = readFloat(rdram, record + kRecPos + 4u * static_cast<uint32_t>(i));
                writeFloat(rdram, record + kRecPos + 4u * static_cast<uint32_t>(i),
                           readFloat(rdram, vec + 4u * static_cast<uint32_t>(i)));
            }
        }
        std::fprintf(stderr, "[fast-travel] departure light: active %u record %08x leader %08x\n", activeId, record,
                     leader);
    }
    g_funcPointGetPos(rdram, ctx, runtime);
    write32(rdram, kActiveFuncPoint, savedActive);
    if (record)
    {
        for (int i = 0; i < 3; ++i)
        {
            writeFloat(rdram, record + kRecPos + 4u * static_cast<uint32_t>(i), saved[i]);
        }
    }
}

void padIsTriggered(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t returnPc = getRegU32(ctx, 31);
    g_padIsTriggered(rdram, ctx, runtime);
    if (!g_enabled.load(std::memory_order_relaxed))
    {
        return;
    }
    if (returnPc == kFieldSelectReturn)
    {
        if (g_open)
        {
            publish(false);
        }
        g_open = false; // the field is running: the map is closed
        const auto now = std::chrono::steady_clock::now();
        if (g_arrivalPending && now - g_lastFieldFrame > std::chrono::milliseconds(300))
        {
            g_arrivalPending = false;
            const uint32_t scene = callGuest(rdram, ctx, kGetCurrentScene);
            callGuest(rdram, ctx, kScreenFadeIn, static_cast<int32_t>(scene), 15);
            std::fprintf(stderr, "[fast-travel] arrived: fade in\n");
        }
        g_lastFieldFrame = now;
    }
    else if (returnPc == kFullMapCloseReturn && getRegU32(ctx, 4) == kPad1)
    {
        const bool answer = onFullMapFrame(rdram, ctx, getRegU32(ctx, 2) != 0u);
        setReturnS32(ctx, answer ? 1 : 0);
    }
}

} // namespace

void install(PS2Runtime &runtime)
{
    g_runtime = &runtime;
    if (const char *value = std::getenv("RG_FAST_TRAVEL"))
    {
        setEnabled(*value && *value != '0');
    }
    rg::timing::hookFunction(runtime, kFuncPointGetPos, &funcPointGetPos, g_funcPointGetPos);
    if (!rg::timing::hookFunction(runtime, kPadIsTriggered, &padIsTriggered, g_padIsTriggered))
    {
        std::fprintf(stderr, "[rg] fast travel from the map is unavailable\n");
    }
}

void setEnabled(bool on)
{
    g_enabled.store(on, std::memory_order_relaxed);
}

bool enabled()
{
    return g_enabled.load(std::memory_order_relaxed);
}

List list()
{
    std::lock_guard<std::mutex> lock(g_listMutex);
    return g_list;
}

bool takeToast(Toast &toast)
{
    std::lock_guard<std::mutex> lock(g_toastMutex);
    if (g_toasts.empty())
    {
        return false;
    }
    toast = g_toasts.front();
    g_toasts.erase(g_toasts.begin());
    return true;
}

} // namespace rg::fasttravel
