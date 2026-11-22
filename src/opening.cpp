#include "opening.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "overlay/overlay_manager.h"
#include "ps2_runtime.h"
#include "scene_timing.h"

namespace rg::opening
{
namespace
{

constexpr uint32_t kPadIsTriggered = 0x001839F0u;
// TitleOpeningMovieTick's five checks, PadIsTriggered(0x4B2570, button), in
// the title overlay, in this order (the returns are the addresses after
// each call): Triangle (0x10), Cross (0x40), Circle (0x80), Square (0x20)
// and Start (0x800). A press goes on to the skip only once the opening has
// been seen (the halfword at 0x481B90).
constexpr uint32_t kOpeningFirstCheckReturn = 0x004EBB48u;
constexpr uint32_t kOpeningCheckReturns[] = {0x004EBB48u, 0x004EBB60u, 0x004EBB78u, 0x004EBB90u, 0x004EBBA8u};
constexpr uint32_t kPad = 0x004B2570u;
constexpr uint32_t kOpeningButton = 0x10u;
constexpr uint32_t kOpeningSeen = 0x00481B90u;

PS2Runtime::RecompiledFunction g_padIsTriggered = nullptr;
std::atomic<bool> g_skip{false};

bool isOpeningCheck(uint32_t returnAddress)
{
    for (const uint32_t address : kOpeningCheckReturns)
    {
        if (address == returnAddress)
        {
            return true;
        }
    }
    return false;
}

void markSeen(uint8_t *rdram)
{
    const uint16_t seen = 1u;
    std::memcpy(rdram + kOpeningSeen, &seen, sizeof(seen));
}

void padIsTriggered(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    g_padIsTriggered(rdram, ctx, runtime);
    const uint32_t returnAddress = getRegU32(ctx, 31);
    if (!isOpeningCheck(returnAddress) || getRegU32(ctx, 4) != kPad ||
        std::strcmp(rg::overlay::residentName(), "title") != 0)
    {
        return;
    }
    if (getRegU32(ctx, 2) != 0u)
    {
        // A button the game takes as a skip: it ignores it until the opening
        // has been seen, so a first boot could not be skipped.
        markSeen(rdram);
    }
    else if (returnAddress == kOpeningFirstCheckReturn && getRegU32(ctx, 5) == kOpeningButton &&
             g_skip.load(std::memory_order_relaxed))
    {
        markSeen(rdram);
        setReturnS32(ctx, 1);
    }
}

} // namespace

void install(PS2Runtime &runtime)
{
    const char *skip = std::getenv("RG_SKIP_OPENING");
    if (skip && *skip && *skip != '0')
    {
        setSkip(true);
    }
    if (!rg::timing::hookFunction(runtime, kPadIsTriggered, &padIsTriggered, g_padIsTriggered))
    {
        std::fprintf(stderr, "[rg] the opening cannot be skipped\n");
    }
}

void setSkip(bool on)
{
    g_skip.store(on, std::memory_order_relaxed);
}

} // namespace rg::opening
