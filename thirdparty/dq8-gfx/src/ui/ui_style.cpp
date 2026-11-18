#include "ui/ui_style.h"

#include <algorithm>
#include <cmath>

// Roboto Medium, from Dear ImGui's misc/fonts, embedded at build time.
extern const unsigned char kUiFontData[];
extern const unsigned int kUiFontData_size;

namespace dq8::ui {
namespace {
constexpr float kBaseFontSize = 17.0f;

ImVec4 color(ImU32 packed) { return ImGui::ColorConvertU32ToFloat4(packed); }
ImVec4 rgba(int r, int g, int b, float a) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }
} // namespace

ImFont *loadFonts() {
    ImFontConfig config;
    // The data is static; the atlas must not try to free it.
    config.FontDataOwnedByAtlas = false;
    ImGuiIO &io = ImGui::GetIO();
    return io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(kUiFontData),
                                          static_cast<int>(kUiFontData_size), kBaseFontSize, &config);
}

void applyStyle(float scale) {
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.WindowPadding = ImVec2(18.0f, 16.0f);
    style.FramePadding = ImVec2(12.0f, 7.0f);
    style.CellPadding = ImVec2(8.0f, 10.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 6.0f;
    style.GrabMinSize = 18.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.WindowRounding = 18.0f;
    style.ChildRounding = 12.0f;
    style.FrameRounding = 10.0f;
    style.PopupRounding = 14.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 9.0f;
    style.TabRounding = 10.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    style.DisabledAlpha = 0.40f;
    style.SeparatorTextBorderSize = 1.0f;

    ImVec4 *c = style.Colors;
    c[ImGuiCol_Text] = color(palette::kText);
    c[ImGuiCol_TextDisabled] = color(palette::kTextMuted);
    c[ImGuiCol_WindowBg] = color(palette::kWindow);
    c[ImGuiCol_ChildBg] = rgba(0, 0, 0, 0.0f);
    c[ImGuiCol_PopupBg] = color(palette::kWindowSolid);
    c[ImGuiCol_Border] = color(palette::kBorder);
    c[ImGuiCol_BorderShadow] = rgba(0, 0, 0, 0.0f);
    c[ImGuiCol_FrameBg] = rgba(255, 255, 255, 0.06f);
    c[ImGuiCol_FrameBgHovered] = rgba(255, 255, 255, 0.10f);
    c[ImGuiCol_FrameBgActive] = rgba(255, 255, 255, 0.14f);
    c[ImGuiCol_TitleBg] = color(palette::kWindowSolid);
    c[ImGuiCol_TitleBgActive] = color(palette::kWindowSolid);
    c[ImGuiCol_TitleBgCollapsed] = color(palette::kWindow);
    c[ImGuiCol_MenuBarBg] = rgba(14, 16, 22, 0.94f);
    c[ImGuiCol_ScrollbarBg] = rgba(0, 0, 0, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = rgba(255, 255, 255, 0.16f);
    c[ImGuiCol_ScrollbarGrabHovered] = rgba(255, 255, 255, 0.28f);
    c[ImGuiCol_ScrollbarGrabActive] = rgba(255, 255, 255, 0.42f);
    c[ImGuiCol_CheckMark] = color(palette::kGold);
    c[ImGuiCol_SliderGrab] = color(palette::kText);
    c[ImGuiCol_SliderGrabActive] = color(palette::kText);
    c[ImGuiCol_Button] = rgba(255, 255, 255, 0.08f);
    c[ImGuiCol_ButtonHovered] = rgba(255, 255, 255, 0.15f);
    c[ImGuiCol_ButtonActive] = rgba(255, 255, 255, 0.22f);
    c[ImGuiCol_Header] = rgba(255, 255, 255, 0.10f);
    c[ImGuiCol_HeaderHovered] = rgba(255, 255, 255, 0.13f);
    c[ImGuiCol_HeaderActive] = rgba(255, 255, 255, 0.18f);
    c[ImGuiCol_Separator] = rgba(255, 255, 255, 0.08f);
    c[ImGuiCol_SeparatorHovered] = rgba(255, 255, 255, 0.30f);
    c[ImGuiCol_SeparatorActive] = rgba(255, 255, 255, 0.50f);
    c[ImGuiCol_ResizeGrip] = rgba(255, 255, 255, 0.0f);
    c[ImGuiCol_ResizeGripHovered] = rgba(255, 255, 255, 0.20f);
    c[ImGuiCol_ResizeGripActive] = rgba(255, 255, 255, 0.35f);
    c[ImGuiCol_InputTextCursor] = color(palette::kText);
    c[ImGuiCol_Tab] = rgba(255, 255, 255, 0.05f);
    c[ImGuiCol_TabHovered] = rgba(255, 255, 255, 0.14f);
    c[ImGuiCol_TabSelected] = rgba(255, 255, 255, 0.16f);
    c[ImGuiCol_TabSelectedOverline] = color(palette::kGold);
    c[ImGuiCol_TabDimmed] = rgba(255, 255, 255, 0.04f);
    c[ImGuiCol_TabDimmedSelected] = rgba(255, 255, 255, 0.10f);
    c[ImGuiCol_PlotLines] = color(palette::kGold);
    c[ImGuiCol_PlotHistogram] = color(palette::kGold);
    c[ImGuiCol_TableHeaderBg] = rgba(255, 255, 255, 0.04f);
    c[ImGuiCol_TableBorderStrong] = rgba(255, 255, 255, 0.08f);
    c[ImGuiCol_TableBorderLight] = rgba(255, 255, 255, 0.06f);
    c[ImGuiCol_TableRowBg] = rgba(0, 0, 0, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = rgba(0, 0, 0, 0.0f);
    c[ImGuiCol_TextLink] = color(palette::kSky);
    c[ImGuiCol_TextSelectedBg] = rgba(255, 189, 89, 0.30f);
    // A soft white ring: the cursor's place for keyboard and controller.
    c[ImGuiCol_NavCursor] = rgba(255, 255, 255, 0.45f);
    c[ImGuiCol_ModalWindowDimBg] = rgba(0, 0, 0, 0.55f);

    style.FontSizeBase = kBaseFontSize;
    style.FontScaleMain = scale;
    style.ScaleAllSizes(scale);
    ImGui::GetStyle() = style;
}

void drawShadow(ImDrawList *list, ImVec2 min, ImVec2 max, float rounding, float alpha) {
    constexpr int kRings = 10;
    for (int ring = 1; ring <= kRings; ++ring) {
        const float spread = static_cast<float>(ring) * 2.4f;
        const float fade = 1.0f - static_cast<float>(ring) / (kRings + 1);
        list->AddRect(ImVec2(min.x - spread, min.y - spread + 6.0f),
                      ImVec2(max.x + spread, max.y + spread + 6.0f),
                      IM_COL32(0, 0, 0, static_cast<int>(46.0f * fade * fade * alpha)),
                      rounding + spread, 2.6f);
    }
}

void drawFrame(ImDrawList *list, ImVec2 min, ImVec2 max, float rounding, float alpha) {
    list->AddRect(min, max, withAlpha(palette::kBorder, alpha), rounding, 1.0f);
}

void decorateWindow(float alpha) {
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    ImDrawList *list = ImGui::GetWindowDrawList();
    // Past the window's own clip rect: the shadow sits outside it.
    list->PushClipRect(ImVec2(min.x - 40.0f, min.y - 40.0f), ImVec2(max.x + 40.0f, max.y + 40.0f), false);
    drawShadow(list, min, max, ImGui::GetStyle().WindowRounding, alpha);
    list->PopClipRect();
}

void drawCursor(ImDrawList *list, ImVec2 tip, float size, float alpha) {
    const float width = std::max(2.0f, std::floor(size * 0.26f));
    const float half = size * 0.5f;
    list->AddRectFilled(ImVec2(tip.x - width, tip.y - half), ImVec2(tip.x, tip.y + half),
                        withAlpha(palette::kGold, alpha), width * 0.5f);
}

} // namespace dq8::ui
