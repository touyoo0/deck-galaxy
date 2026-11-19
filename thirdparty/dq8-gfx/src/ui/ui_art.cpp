#include "ui/ui_art.h"

#include <imgui_internal.h> // RegisterUserTexture

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

// Static: raylib, linked in the same executable, has its own copy of stb_image.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
// The save states' thumbnails are BMPs (Steam Deck fork: added).
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "ui/stb_image.h"

// Generated from installer/art/home.jpg and logo-home.png; empty when absent.
extern const unsigned char kHomeArtData[];
extern const unsigned int kHomeArtData_size;
extern const unsigned char kHomeLogoData[];
extern const unsigned int kHomeLogoData_size;
extern const unsigned char kHomeChangesData[];
extern const unsigned int kHomeChangesData_size;
extern const unsigned char kHomeChangesFr[], kHomeChangesDe[], kHomeChangesEs[], kHomeChangesIt[];
extern const unsigned int kHomeChangesFr_size, kHomeChangesDe_size, kHomeChangesEs_size, kHomeChangesIt_size;

namespace dq8::ui {

namespace {
struct Picture {
    ImTextureData *data = nullptr;
    bool tried = false;
};

void load(Picture &picture, const unsigned char *bytes, unsigned int size, const char *name) {
    picture.tried = true;
    if (size == 0u)
        return;
    int width = 0, height = 0, channels = 0;
    unsigned char *pixels = stbi_load_from_memory(bytes, static_cast<int>(size), &width, &height, &channels, 4);
    if (pixels && width > 0 && height > 0) {
        // Kept for the life of the process: the backend frees its copy of
        // the texture when the context goes.
        picture.data = IM_NEW(ImTextureData)();
        picture.data->Create(ImTextureFormat_RGBA32, width, height);
        std::memcpy(picture.data->GetPixels(), pixels, static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
        ImGui::RegisterUserTexture(picture.data);
        std::fprintf(stderr, "[ui] home %s %dx%d\n", name, width, height);
    } else {
        std::fprintf(stderr, "[ui] home %s could not be decoded\n", name);
    }
    if (pixels)
        stbi_image_free(pixels);
}
} // namespace

bool fileTexture(const char *path, ImTextureRef &texture, ImVec2 &size) {
    // A file that is there but cannot be decoded stays failed until the
    // process restarts; one not there yet is asked for again.
    static std::map<std::string, Picture> pictures;
    Picture &picture = pictures[path];
    if (!picture.tried) {
        std::ifstream in(path, std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        // Nothing on disk yet (a download under way): asked again next time.
        if (bytes.empty())
            return false;
        picture.tried = true;
        int width = 0, height = 0, channels = 0;
        unsigned char *pixels = bytes.empty() ? nullptr
                                              : stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                                                      &width, &height, &channels, 4);
        if (pixels && width > 0 && height > 0) {
            picture.data = IM_NEW(ImTextureData)();
            picture.data->Create(ImTextureFormat_RGBA32, width, height);
            std::memcpy(picture.data->GetPixels(), pixels,
                        static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
            ImGui::RegisterUserTexture(picture.data);
        }
        if (pixels)
            stbi_image_free(pixels);
    }
    if (!picture.data)
        return false;
    texture = picture.data->GetTexRef();
    size = ImVec2(static_cast<float>(picture.data->Width), static_cast<float>(picture.data->Height));
    return true;
}

const char *homeChangelog(unsigned int &size) {
    size = kHomeChangesData_size;
    return reinterpret_cast<const char *>(kHomeChangesData);
}

const char *homeChangelogIn(int language, unsigned int &size) {
    switch (language) {
    case 1: size = kHomeChangesFr_size; return reinterpret_cast<const char *>(kHomeChangesFr);
    case 2: size = kHomeChangesDe_size; return reinterpret_cast<const char *>(kHomeChangesDe);
    case 3: size = kHomeChangesEs_size; return reinterpret_cast<const char *>(kHomeChangesEs);
    case 4: size = kHomeChangesIt_size; return reinterpret_cast<const char *>(kHomeChangesIt);
    default: return homeChangelog(size);
    }
}

bool homeArt(HomePicture which, ImTextureRef &texture, ImVec2 &size) {
    static Picture background, logo;
    Picture &picture = which == HomePicture::Logo ? logo : background;
    if (!picture.tried) {
        if (which == HomePicture::Logo)
            load(picture, kHomeLogoData, kHomeLogoData_size, "logo");
        else
            load(picture, kHomeArtData, kHomeArtData_size, "artwork");
    }
    if (!picture.data)
        return false;
    texture = picture.data->GetTexRef();
    size = ImVec2(static_cast<float>(picture.data->Width), static_cast<float>(picture.data->Height));
    return true;
}

} // namespace dq8::ui
