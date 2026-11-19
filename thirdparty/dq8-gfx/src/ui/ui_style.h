// The menu's look (Rogue Galaxy Steam Deck fork: redesigned): flat, dark,
// quiet. Near-black panels with a hairline edge and generous rounding, white
// for what is selected, and one warm accent (kGold, named for what it was)
// for what is switched on.
#pragma once

#include <imgui.h>

namespace dq8::ui {

namespace palette {
inline constexpr ImU32 kWindow = IM_COL32(14, 16, 22, 246);
inline constexpr ImU32 kWindowSolid = IM_COL32(17, 19, 27, 250);
inline constexpr ImU32 kBorder = IM_COL32(255, 255, 255, 26);
inline constexpr ImU32 kBorderInner = IM_COL32(255, 255, 255, 0);
inline constexpr ImU32 kText = IM_COL32(244, 246, 250, 255);
inline constexpr ImU32 kTextMuted = IM_COL32(139, 147, 166, 255);
inline constexpr ImU32 kGold = IM_COL32(255, 189, 89, 255);
inline constexpr ImU32 kGoldSoft = IM_COL32(255, 189, 89, 48);
inline constexpr ImU32 kSky = IM_COL32(124, 168, 255, 255);
inline constexpr ImU32 kHover = IM_COL32(255, 255, 255, 16);
inline constexpr ImU32 kFill = IM_COL32(255, 255, 255, 14);
inline constexpr ImU32 kFillStrong = IM_COL32(255, 255, 255, 28);
inline constexpr ImU32 kInk = IM_COL32(16, 18, 26, 255); // text on a white plate
inline constexpr ImU32 kPlate = IM_COL32(244, 246, 250, 255); // the white selection plate
inline constexpr ImU32 kGood = IM_COL32(104, 214, 142, 255);
inline constexpr ImU32 kWarn = IM_COL32(255, 189, 89, 255);
inline constexpr ImU32 kBad = IM_COL32(244, 106, 106, 255);
} // namespace palette

inline ImU32 withAlpha(ImU32 color, float alpha) {
    const auto a = static_cast<ImU32>(((color >> IM_COL32_A_SHIFT) & 0xFFu) * alpha);
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

// Loads the embedded font into the current context's atlas.
ImFont *loadFonts();
// Rebuilds the style at a scale: the display's own times the player's choice.
void applyStyle(float scale);

// The soft shadow and hairline of a menu window, drawn over a window's own
// background. Call right after Begin().
void decorateWindow(float alpha = 1.0f);
void drawFrame(ImDrawList *list, ImVec2 min, ImVec2 max, float rounding, float alpha = 1.0f);
// A soft shadow under a floating window, so it reads over a busy picture.
void drawShadow(ImDrawList *list, ImVec2 min, ImVec2 max, float rounding, float alpha = 1.0f);
// The marker beside an entry: a small accent bar ending at `tip`.
void drawCursor(ImDrawList *list, ImVec2 tip, float size, float alpha = 1.0f);

// Heights scale with the font: one "unit" is the current font size.
inline float em(float units = 1.0f) { return ImGui::GetFontSize() * units; }

} // namespace dq8::ui
