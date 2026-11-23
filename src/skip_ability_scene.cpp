#include "skip_ability_scene.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ps2_runtime.h"
#include "return_hook.h"
#include "scene_timing.h"

namespace rg::skipabilityscene
{
namespace
{

constexpr uint32_t kPadIsTriggered = 0x001839F0u;
// Where AbilityModeRun's skip check returns to (the jal at 0x279698).
constexpr uint32_t kSkipCheckReturn = 0x002796A0u;

// The scene's file: AbilityModeRun asks the loader for it in phase 0 and, in
// phase 1, reads LoaderGetStatus (0x1CD4F0, from 0x279474, which returns to
// 0x27947C). -1 (no such file) is the game's way for an ability with no
// scene: it releases the file, asks CharaAbilityResReloadMaybe (0x2F3F10, from
// 0x27949C, which returns to 0x2794A4) to reload the ability's resources and
// goes to phase 100, the end of the ability, without the scene's camera or
// overlay. With the option, a file that has loaded answers -1 too. The
// reload is left out then: the scene it would follow never played, and its
// queued loads kept phase 100 waiting for good.
constexpr uint32_t kLoaderGetStatus = 0x001CD4F0u;
constexpr uint32_t kStatusCheckReturn = 0x0027947Cu;
constexpr uint32_t kAbilityResReload = 0x002F3F10u;
constexpr uint32_t kAbilityResReloadReturn = 0x002794A4u;
// The ability's caster (an index below 12 is a party member, the only ones
// the game lets skip) and the equip slot it uses; the composite id of an
// ability whose scene the game never lets be skipped.
constexpr uint32_t kCaster = 0x004D7C70u;
constexpr uint32_t kEquipSlot = 0x004D7C78u;
constexpr uint32_t kEquipSlotGetAbilityCompositeId = 0x003A0730u;
constexpr int32_t kUnskippableAbility = 0x1776;

PS2Runtime::RecompiledFunction g_original = nullptr;
PS2Runtime::RecompiledFunction g_originalStatus = nullptr;
PS2Runtime::RecompiledFunction g_originalReload = nullptr;
std::atomic<bool> g_sceneLeftOut{false};
PS2Runtime *g_runtime = nullptr;
std::atomic<bool> g_enabled{false};
// When the scene was left out (steady clock, nanoseconds; 0 for never).
std::atomic<int64_t> g_leftOutAt{0};

int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

template <typename T>
T readGuest(const uint8_t *rdram, uint32_t address)
{
    T value{};
    std::memcpy(&value, rdram + address, sizeof(value));
    return value;
}

// Whether the game would let the player skip this ability's scene.
bool skippable(uint8_t *rdram, const R5900Context *caller)
{
    const int32_t caster = readGuest<int32_t>(rdram, kCaster);
    if (caster < 0 || caster >= 12 || !g_runtime)
    {
        return false;
    }
    PS2Runtime::RecompiledFunction fn = g_runtime->lookupFunction(kEquipSlotGetAbilityCompositeId);
    if (!fn)
    {
        return false;
    }
    R5900Context local = *caller;
    local.r[4] = _mm_set_epi64x(0, static_cast<int64_t>(readGuest<int32_t>(rdram, kEquipSlot)));
    if (!rg::hooks::runToEnd(fn, kEquipSlotGetAbilityCompositeId, rdram, local, g_runtime))
    {
        return false;
    }
    return static_cast<int32_t>(getRegU32(&local, 2)) != kUnskippableAbility;
}

void loaderGetStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const bool phaseOneCheck = g_enabled.load(std::memory_order_relaxed) && getRegU32(ctx, 31) == kStatusCheckReturn;
    const uint32_t ra = getRegU32(ctx, 31);
    g_originalStatus(rdram, ctx, runtime);
    if (phaseOneCheck && static_cast<int32_t>(getRegU32(ctx, 2)) == 1 && skippable(rdram, ctx))
    {
        g_sceneLeftOut.store(true, std::memory_order_relaxed);
        g_leftOutAt.store(nowNs(), std::memory_order_relaxed);
        setReturnS32(ctx, -1);
    }
    ctx->pc = ra;
}

void abilityResReload(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (getRegU32(ctx, 31) == kAbilityResReloadReturn && g_sceneLeftOut.exchange(false, std::memory_order_relaxed))
    {
        ctx->pc = getRegU32(ctx, 31);
        return;
    }
    g_originalReload(rdram, ctx, runtime);
}

void padIsTriggered(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (!g_enabled.load(std::memory_order_relaxed) || getRegU32(ctx, 31) != kSkipCheckReturn)
    {
        g_original(rdram, ctx, runtime);
        return;
    }
    setReturnS32(ctx, 1);
    // A host function stands in for the guest's `jr ra` too (see
    // libc_rand.cpp).
    ctx->pc = getRegU32(ctx, 31);
}

} // namespace

void install(PS2Runtime &runtime)
{
    const char *enabled = std::getenv("RG_SKIP_ABILITY_SCENE");
    if (enabled && *enabled && *enabled != '0')
    {
        setEnabled(true);
    }
    g_runtime = &runtime;
    if (!rg::timing::hookFunction(runtime, kLoaderGetStatus, &loaderGetStatus, g_originalStatus))
    {
        std::fprintf(stderr, "[rg] the ability scenes are still shown\n");
    }
    if (!rg::timing::hookFunction(runtime, kAbilityResReload, &abilityResReload, g_originalReload))
    {
        std::fprintf(stderr, "[rg] the ability scenes are still shown\n");
    }
    if (!rg::timing::hookFunction(runtime, kPadIsTriggered, &padIsTriggered, g_original))
    {
        std::fprintf(stderr, "[rg] the ability scenes stay as they are\n");
    }
}

float burstProgress()
{
    constexpr double kSeconds = 0.55;
    const int64_t at = g_leftOutAt.load(std::memory_order_relaxed);
    if (at == 0)
    {
        return -1.0f;
    }
    const double t = static_cast<double>(nowNs() - at) * 1e-9 / kSeconds;
    return t < 0.0 || t >= 1.0 ? -1.0f : static_cast<float>(t);
}

void setEnabled(bool on)
{
    g_enabled.store(on, std::memory_order_relaxed);
}

} // namespace rg::skipabilityscene
