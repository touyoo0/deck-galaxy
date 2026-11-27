#include "ui/ui_widgets.h"

#include "ui/ui_style.h"
#include "ui/ui_text.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <string>

namespace dq8::ui {
namespace {
ImU32 mix(ImU32 a, ImU32 b, float t) {
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t,
                                                 x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

// Eases a per-item value toward its target, for small animations.
float animate(ImGuiID id, float target, float speed = 14.0f) {
    float &value = *ImGui::GetStateStorage()->GetFloatRef(id, target);
    value += (target - value) * std::min(1.0f, ImGui::GetIO().DeltaTime * speed);
    if (std::fabs(target - value) < 0.002f)
        value = target;
    return value;
}

void centeredText(ImDrawList *list, ImVec2 min, ImVec2 max, ImU32 color, const char *text) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    list->AddText(ImVec2(std::floor((min.x + max.x - size.x) * 0.5f), std::floor((min.y + max.y - size.y) * 0.5f)),
                  color, text);
}
} // namespace

bool beginWindow(const char *name, bool *open, ImGuiWindowFlags flags) {
    const bool visible = ImGui::Begin(name, open, flags);
    if (visible)
        decorateWindow();
    return visible;
}

bool toggle(const char *id, bool *value) {
    const float frame = ImGui::GetFrameHeight();
    const float height = std::floor(frame * 0.78f);
    const float width = std::floor(height * 1.75f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    // InvisibleButton leaves keyboard and gamepad navigation out unless asked.
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, frame), ImGuiButtonFlags_EnableNav);
    if (pressed)
        *value = !*value;
    const float t = animate(ImGui::GetItemID(), *value ? 1.0f : 0.0f);
    const bool hovered = ImGui::IsItemHovered();

    // Faded with the style's alpha, as ImGui's own items are under
    // BeginDisabled() (Rogue Galaxy: added).
    ImDrawList *list = ImGui::GetWindowDrawList();
    const ImVec2 min(pos.x, pos.y + (frame - height) * 0.5f);
    const ImVec2 max(min.x + width, min.y + height);
    const ImU32 off = IM_COL32(255, 255, 255, hovered ? 40 : 28);
    list->AddRectFilled(min, max, ImGui::GetColorU32(mix(off, palette::kGold, t)), height * 0.5f);
    const float radius = height * 0.5f - 3.0f;
    const ImVec2 knob(min.x + height * 0.5f + (width - height) * t, min.y + height * 0.5f);
    list->AddCircleFilled(ImVec2(knob.x, knob.y + 1.0f), radius + 0.5f, ImGui::GetColorU32(IM_COL32(0, 0, 0, 60)));
    list->AddCircleFilled(knob, radius, ImGui::GetColorU32(mix(IM_COL32(214, 218, 230, 255), IM_COL32(255, 255, 255, 255), t)));
    return pressed;
}

// A slider as a thin track with a knob; the value is written to its right.
// Built on ImGui's own slider, drawn invisible, so typing, the d-pad and the
// sticks behave as they always did (Steam Deck fork: added).
static bool slider(const char *id, ImGuiDataType type, void *value, const void *minimum, const void *maximum,
                   const char *format, ImGuiSliderFlags flags) {
    const float frame = ImGui::GetFrameHeight();
    char text[64];
    ImGui::DataTypeFormatString(text, sizeof(text), type, value, format ? format : "%d");
    const float valueWidth = std::max(ImGui::CalcTextSize("100%").x, ImGui::CalcTextSize(text).x) + em(0.9f);
    const float full = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(0, 0, 0, 0));
    ImGui::SetNextItemWidth(std::max(frame * 3.0f, full - valueWidth));
    const bool changed = ImGui::SliderScalar(id, type, value, minimum, maximum, "", flags);
    ImGui::PopStyleColor(5);
    const ImGuiID item = ImGui::GetItemID();
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    const bool active = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused() && ImGui::GetCurrentContext()->NavCursorVisible;
    const bool hovered = ImGui::IsItemHovered();

    double fraction = 0.0;
    {
        const double lo = type == ImGuiDataType_Float ? *static_cast<const float *>(minimum) : *static_cast<const int *>(minimum);
        const double hi = type == ImGuiDataType_Float ? *static_cast<const float *>(maximum) : *static_cast<const int *>(maximum);
        const double now = type == ImGuiDataType_Float ? *static_cast<const float *>(value) : *static_cast<const int *>(value);
        fraction = hi > lo ? std::clamp((now - lo) / (hi - lo), 0.0, 1.0) : 0.0;
    }
    ImDrawList *list = ImGui::GetWindowDrawList();
    const float knobRadius = frame * 0.30f + (active ? 1.5f : 0.0f);
    const float left = min.x + knobRadius, right = max.x - knobRadius;
    const float y = std::floor((min.y + max.y) * 0.5f);
    const float thick = std::max(3.0f, std::floor(frame * 0.14f));
    const float x = left + static_cast<float>(fraction) * (right - left);
    if (ImGui::TempInputIsActive(item)) {
        list->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
    } else {
        list->AddRectFilled(ImVec2(left, y - thick * 0.5f), ImVec2(right, y + thick * 0.5f),
                            ImGui::GetColorU32(IM_COL32(255, 255, 255, 30)), thick * 0.5f);
        list->AddRectFilled(ImVec2(left, y - thick * 0.5f), ImVec2(x, y + thick * 0.5f), ImGui::GetColorU32(palette::kGold),
                            thick * 0.5f);
        if (focused || hovered || active)
            list->AddCircleFilled(ImVec2(x, y), knobRadius + em(0.35f), ImGui::GetColorU32(withAlpha(palette::kGold, 0.20f)));
        list->AddCircleFilled(ImVec2(x, y + 1.0f), knobRadius + 0.5f, ImGui::GetColorU32(IM_COL32(0, 0, 0, 70)));
        list->AddCircleFilled(ImVec2(x, y), knobRadius, ImGui::GetColorU32(palette::kText));
    }
    // The value, right-aligned in the room kept for it.
    const ImVec2 size = ImGui::CalcTextSize(text);
    list->AddText(ImVec2(std::floor(max.x + valueWidth - em(0.1f) - size.x), std::floor(y - size.y * 0.5f)),
                  ImGui::GetColorU32(palette::kText), text);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::Dummy(ImVec2(valueWidth, frame));
    return changed;
}

bool sliderInt(const char *id, int *value, int minimum, int maximum, const char *format, ImGuiSliderFlags flags) {
    return slider(id, ImGuiDataType_S32, value, &minimum, &maximum, format, flags);
}

bool sliderFloat(const char *id, float *value, float minimum, float maximum, const char *format, ImGuiSliderFlags flags) {
    return slider(id, ImGuiDataType_Float, value, &minimum, &maximum, format, flags);
}

bool segmented(const char *id, int *value, const char *const *labels, int count, const bool *enabled) {
    ImGui::PushID(id);
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rounding = height * 0.5f;
    ImDrawList *list = ImGui::GetWindowDrawList();
    // Its colors faded with the style's alpha, under BeginDisabled() too.
    list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(IM_COL32(255, 255, 255, 14)),
                        rounding);

    const float segment = width / static_cast<float>(count);
    // The white plate glides to the choice.
    const float slid = animate(ImGui::GetID("##plate"), static_cast<float>(*value), 16.0f);
    const ImVec2 plateMin(pos.x + 3.0f + segment * slid, pos.y + 3.0f);
    list->AddRectFilled(plateMin, ImVec2(plateMin.x + segment - 6.0f, pos.y + height - 3.0f),
                        ImGui::GetColorU32(palette::kPlate), rounding - 3.0f);
    bool changed = false;
    for (int index = 0; index < count; ++index) {
        const bool chosen = *value == index;
        const bool usable = !enabled || enabled[index];
        const ImVec2 at(pos.x + segment * index, pos.y);
        ImGui::SetCursorScreenPos(at);
        ImGui::PushID(index);
        ImGui::BeginDisabled(!usable);
        if (ImGui::InvisibleButton("##choice", ImVec2(segment, height), ImGuiButtonFlags_EnableNav) && !chosen) {
            *value = index;
            changed = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::EndDisabled();
        ImGui::PopID();

        const ImVec2 min(at.x + 3.0f, at.y + 3.0f), max(at.x + segment - 3.0f, at.y + height - 3.0f);
        if (!chosen && hovered && usable)
            list->AddRectFilled(min, max, palette::kHover, rounding - 3.0f);
        const ImU32 color = chosen ? palette::kInk : usable ? palette::kText : withAlpha(palette::kTextMuted, 0.6f);
        list->PushClipRect(min, max, true);
        centeredText(list, min, max, ImGui::GetColorU32(color), L(labels[index]));
        list->PopClipRect();
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(width, height));
    ImGui::PopID();
    return changed;
}

void sectionHeader(const char *title) {
    ImGui::Dummy(ImVec2(0.0f, em(0.5f)));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.86f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(palette::kTextMuted));
    ImGui::TextUnformatted(L(title));
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, em(0.1f)));
}

bool beginSettings(const char *id) {
    // Rows told apart by a hairline, not by boxes.
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_BordersInnerH))
        return false;
    const float available = ImGui::GetContentRegionAvail().x;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed,
                            std::clamp(available * 0.42f, em(8.0f), em(17.0f)));
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void settingRow(const char *label, const char *help, const char *chipText, const char *note, ImU32 noteColor) {
    ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetFrameHeight());
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(L(label));
    if (chipText) {
        ImGui::SameLine(0.0f, em(0.5f));
        chip(L(chipText), palette::kWarn);
    }
    if (help || note) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.84f);
        // Across the whole table, under the control too, rather than in the
        // name's column alone (Steam Deck fork: changed): the clip is opened
        // to the table's width, and the text wraps at its right edge.
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        const ImGuiTable *table = ImGui::GetCurrentTable();
        const float right = table->OuterRect.Max.x - em(0.6f);
        const ImVec2 clipMin = window->DrawList->GetClipRectMin(), clipMax = window->DrawList->GetClipRectMax();
        ImGui::PushClipRect(ImVec2(std::min(clipMin.x, table->OuterRect.Min.x), clipMin.y),
                            ImVec2(std::max(clipMax.x, table->OuterRect.Max.x), clipMax.y), false);
        ImGui::PushTextWrapPos(right - window->Pos.x + window->Scroll.x);
        if (help) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(palette::kTextMuted));
            ImGui::TextUnformatted(L(help));
            ImGui::PopStyleColor();
        }
        if (note) {
            // Whatever the row's greyed-out state, the note stays readable.
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(noteColor ? noteColor : palette::kWarn));
            ImGui::TextUnformatted(L(note));
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
        ImGui::PopTextWrapPos();
        ImGui::PopClipRect();
        ImGui::PopFont();
    }
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void endSettings() { ImGui::EndTable(); }

bool navGroup(const char *label, Icon kind, bool open, bool active) {
    const float height = em(2.35f);
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const bool pressed = ImGui::InvisibleButton("##group", ImVec2(width, height), ImGuiButtonFlags_EnableNav);
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered();
    const float y = pos.y + height * 0.5f;

    ImDrawList *list = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + width, pos.y + height);
    const float rounding = em(0.65f);
    if (hovered)
        list->AddRectFilled(pos, max, IM_COL32(255, 255, 255, 16), rounding);
    else if (open)
        list->AddRectFilled(pos, max, IM_COL32(255, 255, 255, 9), rounding);
    const ImU32 color = (open || active) ? palette::kText : hovered ? IM_COL32(214, 219, 232, 255) : palette::kTextMuted;
    drawIcon(list, kind, ImVec2(pos.x + em(1.2f), y), em(1.05f), active ? palette::kGold : color);
    const char *shown = L(label);
    const ImVec2 text = ImGui::CalcTextSize(shown);
    list->AddText(ImVec2(pos.x + em(2.35f), std::floor(y - text.y * 0.5f)), color, shown);

    // Chevron on the right: down when open, right when collapsed.
    const float cx = pos.x + width - em(1.0f);
    const float e = em(0.24f);
    if (open) {
        list->PathLineTo(ImVec2(cx - e * 1.3f, y - e * 0.5f));
        list->PathLineTo(ImVec2(cx, y + e * 0.7f));
        list->PathLineTo(ImVec2(cx + e * 1.3f, y - e * 0.5f));
    } else {
        list->PathLineTo(ImVec2(cx - e * 0.5f, y - e * 1.3f));
        list->PathLineTo(ImVec2(cx + e * 0.7f, y));
        list->PathLineTo(ImVec2(cx - e * 0.5f, y + e * 1.3f));
    }
    list->PathStroke(open ? palette::kText : palette::kTextMuted, 1.6f);
    return pressed;
}

bool navItem(const char *label, Icon kind, bool selected, bool sub) {
    const float indent = sub ? em(0.95f) : 0.0f;
    const float height = sub ? em(2.15f) : em(2.5f);
    const float avail = ImGui::GetContentRegionAvail().x;
    const ImVec2 rowPos = ImGui::GetCursorScreenPos();
    if (sub)
        ImGui::SetCursorScreenPos(ImVec2(rowPos.x + indent, rowPos.y));
    const float width = std::max(em(4.0f), avail - indent);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const bool pressed = ImGui::InvisibleButton("##page", ImVec2(width, height), ImGuiButtonFlags_EnableNav);
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered();
    const float y = pos.y + height * 0.5f;

    ImDrawList *list = ImGui::GetWindowDrawList();
    if (sub) {
        const float lineX = std::floor(rowPos.x + em(0.55f)) + 0.5f;
        list->AddLine(ImVec2(lineX, rowPos.y), ImVec2(lineX, rowPos.y + height + ImGui::GetStyle().ItemSpacing.y),
                      IM_COL32(255, 255, 255, 22), 1.0f);
    }
    const ImVec2 max(pos.x + width, pos.y + height);
    const float rounding = em(0.6f);
    if (selected)
        list->AddRectFilled(pos, max, IM_COL32(255, 255, 255, 24), rounding);
    else if (hovered)
        list->AddRectFilled(pos, max, IM_COL32(255, 255, 255, 12), rounding);
    const ImU32 color = selected ? palette::kText : hovered ? IM_COL32(214, 219, 232, 255) : palette::kTextMuted;
    drawIcon(list, kind, ImVec2(pos.x + em(1.1f), y), em(0.98f), selected ? palette::kGold : color);
    const char *shown = L(label);
    const ImVec2 text = ImGui::CalcTextSize(shown);
    list->AddText(ImVec2(pos.x + em(2.15f), std::floor(y - text.y * 0.5f)), color, shown);
    return pressed;
}

namespace {
// The cursor beside the item just submitted, in the menu window `list` draws to.
// Menu entries reach half the item spacing left of their text; the cursor
// goes by the text, clear of the window's edge.
void menuCursor(ImDrawList *list, ImVec2 windowMin, ImVec2 windowSize) {
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    const float text = min.x + std::floor(ImGui::GetStyle().ItemSpacing.x * 0.5f);
    list->PushClipRect(windowMin, ImVec2(windowMin.x + windowSize.x, windowMin.y + windowSize.y), false);
    drawCursor(list, ImVec2(text - em(0.4f), (min.y + max.y) * 0.5f), em(0.55f));
    list->PopClipRect();
}
} // namespace

bool menuItem(const char *label, const char *shortcut, bool selected, bool enabled) {
    const bool pressed = ImGui::MenuItem(L(label), shortcut, selected, enabled);
    if (enabled && (ImGui::IsItemHovered() || ImGui::IsItemFocused()))
        menuCursor(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(), ImGui::GetWindowSize());
    return pressed;
}

bool beginSubmenu(const char *label) {
    // Taken first: once the submenu is open, it is the current window.
    ImDrawList *list = ImGui::GetWindowDrawList();
    const ImVec2 windowMin = ImGui::GetWindowPos(), windowSize = ImGui::GetWindowSize();
    const bool open = ImGui::BeginMenu(L(label));
    if (open || ImGui::IsItemHovered() || ImGui::IsItemFocused())
        menuCursor(list, windowMin, windowSize);
    return open;
}

bool beginCombo(const char *id, const char *preview) {
    ImDrawList *list = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::CalcItemWidth(), height = ImGui::GetFrameHeight();
    const bool open = ImGui::BeginCombo(id, preview, ImGuiComboFlags_NoArrowButton);
    const ImVec2 centre(pos.x + width - height * 0.6f, pos.y + height * 0.5f);
    const float e = height * 0.13f;
    list->PathLineTo(ImVec2(centre.x - e * 1.4f, centre.y - e * 0.7f + (open ? e : 0.0f)));
    list->PathLineTo(ImVec2(centre.x, centre.y + e * 0.7f - (open ? e : 0.0f)));
    list->PathLineTo(ImVec2(centre.x + e * 1.4f, centre.y - e * 0.7f + (open ? e : 0.0f)));
    list->PathStroke(ImGui::GetColorU32(palette::kTextMuted), 1.8f);
    return open;
}

bool iconButton(const char *label, Icon kind, bool primary) {
    const float iconSize = em(0.95f);
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    const char *shown = L(label);
    const ImVec2 text = ImGui::CalcTextSize(shown);
    const ImVec2 size(padding.x * 2.0f + iconSize + em(0.45f) + text.x, ImGui::GetFrameHeight());
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, size.y * 0.5f);
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(palette::kPlate));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.82f, 0.84f, 0.88f, 1.0f));
    }
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const bool pressed = ImGui::Button("##button", size);
    ImGui::PopID();
    if (primary)
        ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    const ImU32 color = primary ? palette::kInk : palette::kText;
    ImDrawList *list = ImGui::GetWindowDrawList();
    drawIcon(list, kind, ImVec2(pos.x + padding.x + iconSize * 0.5f, pos.y + size.y * 0.5f), iconSize, color);
    list->AddText(ImVec2(pos.x + padding.x + iconSize + em(0.45f), std::floor(pos.y + (size.y - text.y) * 0.5f)),
                  color, shown);
    return pressed;
}

void keycap(const char *text) {
    // Laid out one frame tall, so it lines up with framed text beside it.
    const float frame = ImGui::GetFrameHeight();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.8f);
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float height = std::floor(frame * 0.78f);
    const float width = std::max(height, textSize.x + em(0.8f));
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, frame));
    const ImVec2 min(at.x, std::floor(at.y + (frame - height) * 0.5f));
    const ImVec2 max(min.x + width, min.y + height);
    ImDrawList *list = ImGui::GetWindowDrawList();
    const float rounding = height * 0.24f;
    list->AddRectFilled(min, max, IM_COL32(255, 255, 255, 26), rounding);
    list->AddRect(min, max, IM_COL32(255, 255, 255, 40), rounding, 1.0f);
    centeredText(list, min, max, palette::kText, text);
    ImGui::PopFont();
}

void chip(const char *text, ImU32 color) {
    // One frame tall, as keycap is, to line up with framed text beside it.
    const float frame = ImGui::GetFrameHeight();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.72f);
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float height = std::floor(textSize.y + em(0.5f));
    const float width = std::floor(textSize.x + em(1.2f));
    if (ImGui::GetContentRegionAvail().x < width)
        ImGui::NewLine();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, frame));
    const ImVec2 min(at.x, std::floor(at.y + (frame - height) * 0.5f));
    const ImVec2 max(min.x + width, min.y + height);
    ImDrawList *list = ImGui::GetWindowDrawList();
    list->AddRectFilled(min, max, ImGui::GetColorU32(withAlpha(color, 0.16f)), height * 0.5f);
    centeredText(list, min, max, ImGui::GetColorU32(color), text);
    ImGui::PopFont();
}

void mutedText(const char *format, ...) {
    va_list args;
    va_start(args, format);
    ImGui::TextDisabledV(L(format), args);
    va_end(args);
}

} // namespace dq8::ui
