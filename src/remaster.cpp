#include "remaster.h"

#if defined(RG_HAS_SDLGPU)
#include "gfx/native/native_pipeline.h"
#endif

#include <atomic>
#include <cstdlib>

namespace rg::remaster
{
namespace
{

std::atomic<bool> g_enabled{true};

} // namespace

void install()
{
    if (const char *value = std::getenv("RG_REMASTER"); value && *value)
    {
        setEnabled(*value != '0');
        return;
    }
#if defined(RG_HAS_SDLGPU)
    dq8::gfx::native::setRemaster(g_enabled.load(std::memory_order_relaxed));
#endif
}

void setEnabled(bool on)
{
    g_enabled.store(on, std::memory_order_relaxed);
#if defined(RG_HAS_SDLGPU)
    dq8::gfx::native::setRemaster(on);
#endif
}

bool enabled()
{
    return g_enabled.load(std::memory_order_relaxed);
}

} // namespace rg::remaster
