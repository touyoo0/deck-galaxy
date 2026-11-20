#include "native_renderer.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace rg::nativerenderer
{

namespace
{

std::atomic<bool> g_preferred{true};

// Texture packs are installed on the GS path's texture cache
// (texture_pack.cpp); the native pipeline has its own textures and does not
// draw them yet. They are looked for where texture_pack.cpp looks.
bool texturePacksPresent()
{
    std::error_code error;
    if (const char *one = std::getenv("RG_TEXTURE_PACK"); one && *one)
        return true;
    if (std::filesystem::is_directory("textures/hd", error))
        return true;
    std::filesystem::directory_iterator it("textures/packs", error);
    return !error && it != std::filesystem::directory_iterator();
}

} // namespace

void setPreferred(bool on)
{
    g_preferred.store(on, std::memory_order_relaxed);
}

bool enabled()
{
    static const bool on = [] {
        const char *value = std::getenv("RG_NATIVE_RENDERER");
        if (value && *value)
            return std::strcmp(value, "0") != 0;
        const bool preferred = g_preferred.load(std::memory_order_relaxed);
        if (preferred && texturePacksPresent())
        {
            std::fprintf(stderr, "[rg] native renderer off: texture packs found, which it does not draw yet "
                                 "(RG_NATIVE_RENDERER=1 forces it)\n");
            return false;
        }
        return preferred;
    }();
    return on;
}

} // namespace rg::nativerenderer
