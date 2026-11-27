// The settings window's pages and the menu's other windows.
#include "ui/ui_overlay.h"

#include "ui/ui_art.h"

#include "gfx/backends/sdlgpu/sdlgpu_input.h"
#include "ui/ui_style.h"
#include "ui/ui_text.h"
#include "ui/ui_widgets.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

namespace dq8::ui {
namespace {
using gfx::PadInput;

constexpr const char *kTimes = "\xC3\x97"; // U+00D7, spelled out for compilers without /utf-8
constexpr const char *kPageNames[] = {"Save states", "Actions", "Bestiary", "Display", "Sound", "Controls",
                                      "Game", "Language", "Achievements", "Interface", "Help"};
constexpr Icon kPageIcons[] = {Icon::Disc, Icon::Play, Icon::Folder, Icon::Display, Icon::Sound, Icon::Controls,
                               Icon::Disc, Icon::Info, Icon::Check,  Icon::Interface, Icon::Link};
static_assert(sizeof(kPageNames) / sizeof(kPageNames[0]) == static_cast<size_t>(SettingsPage::Count));
static_assert(sizeof(kPageIcons) / sizeof(kPageIcons[0]) == static_cast<size_t>(SettingsPage::Count));
constexpr const char *kProjectUrl = "https://github.com/victor-takai/rogue-galaxy-recomp";
constexpr const char *kIssuesUrl = "https://github.com/victor-takai/rogue-galaxy-recomp/issues";

constexpr ImGuiWindowFlags kDialogFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_AlwaysAutoResize;

ImVec4 color(ImU32 packed) { return ImGui::ColorConvertU32ToFloat4(packed); }

// A setting that does nothing now (a reason given, null while it works): what
// is drawn while this lives is greyed out (Rogue Galaxy: added). The line that
// says what would make it work goes in settingRow's note.
class Blocked {
public:
    explicit Blocked(const char *reason) { ImGui::BeginDisabled(reason != nullptr); }
    ~Blocked() { ImGui::EndDisabled(); }
    Blocked(const Blocked &) = delete;
    Blocked &operator=(const Blocked &) = delete;
};

// A round "x" in a window's corner, for the mouse: keyboard and controller
// close with Back, and the cursor would otherwise start on it.
bool closeButton(const char *id) {
    const float size = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    // Where drawIcon puts the cross, so the hover disc stays around it.
    const ImVec2 center = pixelCentre(ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f));
    ImDrawList *list = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered())
        list->AddCircleFilled(center, size * 0.5f, palette::kHover);
    drawIcon(list, Icon::Close, center, size * 0.62f,
             ImGui::IsItemHovered() ? palette::kText : palette::kTextMuted);
    return pressed;
}

// closeButton at the right end of the line that starts at `lineTop`, centred
// on `middle` (its title's): the button is a frame of the smaller font, so at
// the line's top it sat above a bigger title's middle.
bool closeButtonBeside(const char *id, float lineTop, float middle) {
    const float frame = ImGui::GetFrameHeight();
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - frame);
    ImGui::SetCursorScreenPos(
        ImVec2(ImGui::GetCursorScreenPos().x, std::floor(std::max(lineTop, middle - frame * 0.5f))));
    return closeButton(id);
}

// A window's title row: icon, title, and a close button at the right end.
void dialogHeader(Icon kind, const char *title, bool *open) {
    const IconRoom room = iconRoom(em(1.4f));
    ImGui::SameLine(0.0f, em(0.6f));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(L(title));
    ImGui::PopFont();
    drawIconBeside(room, kind, palette::kTextMuted);
    if (open && closeButtonBeside("##close", room.pos.y, lastItemMiddle()))
        *open = false;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, at.y + em(0.15f)),
                                        ImVec2(at.x + ImGui::GetContentRegionAvail().x, at.y + em(0.15f)),
                                        IM_COL32(255, 255, 255, 20), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, em(0.6f)));
}

void centerNextWindow() {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                   viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
}

// `joiner` sits between the keys: "+" for a chord, "/" for either of them.
void shortcutRow(const char *const *keys, int count, const char *what, const char *joiner = "+") {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    for (int key = 0; key < count; ++key) {
        if (key != 0) {
            ImGui::SameLine(0.0f, em(0.3f));
            ImGui::AlignTextToFramePadding();
            mutedText("%s", joiner);
            ImGui::SameLine(0.0f, em(0.3f));
        }
        keycap(keys[key]);
    }
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(L(what));
}

std::string conflictWith(const gfx::PadConfig &config, PadInput input, bool keyboard,
                         const gfx::PadBinding &binding) {
    if (!binding.bound())
        return {};
    for (size_t other = 0; other < gfx::kPadInputCount; ++other) {
        if (static_cast<PadInput>(other) == input)
            continue;
        for (const auto &candidate : config.slots(static_cast<PadInput>(other), keyboard))
            if (candidate == binding)
                return L(gfx::padInputName(static_cast<PadInput>(other)));
    }
    return {};
}
// One save state's row: its picture, slot and time. Returns true when chosen;
// the item is the row's, for the caller to focus (Steam Deck fork: added).
bool slotRow(int slot, const HostServices::SaveSlot &entry, bool enabled, float thumbHeight) {
    const float thumbWidth = thumbHeight * 1.6f, pad = em(0.4f);
    const float rowHeight = thumbHeight + 2.0f * pad;
    ImGui::PushID(slot);
    ImGui::BeginDisabled(!enabled);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::Selectable("##slot", false, 0, ImVec2(0.0f, rowHeight));
    ImDrawList *list = ImGui::GetWindowDrawList();
    const ImVec2 thumbMin(at.x + pad, at.y + pad), thumbMax(thumbMin.x + thumbWidth, thumbMin.y + thumbHeight);
    ImTextureRef picture;
    ImVec2 pictureSize;
    if (entry.used && !entry.picture.empty() && fileTexture(entry.picture.c_str(), picture, pictureSize))
        list->AddImage(picture, thumbMin, thumbMax);
    else
        list->AddRectFilled(thumbMin, thumbMax, withAlpha(palette::kBorderInner, 0.8f), em(0.2f));
    list->AddRect(thumbMin, thumbMax, withAlpha(palette::kBorder, enabled ? 0.4f : 0.15f), em(0.2f));
    const float textX = thumbMax.x + em(1.0f);
    const std::string title = Lf("Slot %d", slot);
    list->AddText(ImVec2(textX, at.y + pad + em(0.3f)), enabled ? palette::kText : palette::kTextMuted, title.c_str());
    const std::string when = entry.used ? entry.when : std::string(L("empty"));
    list->AddText(ImVec2(textX, at.y + pad + em(1.6f)), entry.used ? palette::kGold : palette::kTextMuted,
                  when.c_str());
    ImGui::EndDisabled();
    ImGui::PopID();
    return pressed;
}
} // namespace

void Overlay::drawSettingsWindow() {
    ImGuiContext &g = *ImGui::GetCurrentContext();
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 area = viewport->WorkSize;
    const ImVec2 size(std::min(area.x - em(2.0f), em(54.0f)), std::min(area.y - em(1.0f), em(47.0f)));
    for (int page = 0; !pageShown(m_page) && page < static_cast<int>(SettingsPage::Count); ++page)
        m_page = static_cast<SettingsPage>((static_cast<int>(m_page) + 1) % static_cast<int>(SettingsPage::Count));
    centerNextWindow();
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    if (m_focusSettings) {
        ImGui::SetNextWindowFocus();
        m_focusSettings = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em(1.5f), em(1.2f)));
    // Over the home screen's picture and notes: more opaque, to read.
    const bool opaque = m_homeOpen;
    if (opaque)
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(17, 19, 27, 255));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse;
    if (beginWindow("Settings##dq8", nullptr, flags)) {
        dialogHeader(Icon::Gear, "Settings", &m_settingsOpen);
        // The controller's buttons along the bottom edge while one is in use.
        const float hints = padInUse() ? ImGui::GetFrameHeight() + em(1.2f) : 0.0f;
        const int count = static_cast<int>(SettingsPage::Count);

        // The page list and the page are apart for keyboard and controller:
        // the cursor stays in one until Right or A takes it into the page, and
        // Left at the page's edge or Back brings it out. Moving along the
        // list shows each page.
        const float navWidth = em(12.0f);
        ImGui::BeginChild("##nav", ImVec2(navWidth, -hints), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        m_listPane = ImGui::GetCurrentWindow();
        const bool inList = g.NavWindow == m_listPane;
        const bool follow = inList && g.NavCursorVisible && !m_focusList;
        for (int page = 0; page < count; ++page) {
            const auto which = static_cast<SettingsPage>(page);
            if (!pageShown(which))
                continue;
            if (navItem(kPageNames[page], kPageIcons[page], m_page == which)) {
                m_page = which;
                // Chosen with A or Enter rather than a click: on into it.
                if (g.NavCursorVisible)
                    m_focusPage = true;
            } else if (follow && m_page != which && ImGui::IsItemFocused()) {
                m_page = which;
            }
            if (m_focusList && m_page == which) {
                ImGui::FocusItem();
                if (padInUse())
                    ImGui::SetNavCursorVisible(true);
                m_focusList = false;
            }
        }
        // A way out for a player with only a controller, asked first.
        if (navItem("Quit game", Icon::Close, false))
            m_quitAsk = true;
        if (inList && !m_focusPage &&
            (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, false) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))) {
            // Nothing lies right of the list in it; the request would only
            // find the page's items once the page has the focus.
            ImGui::NavMoveRequestCancel();
            m_focusPage = true;
        }
        const float footer = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        if (ImGui::GetContentRegionAvail().y > footer) {
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - footer);
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.84f);
            mutedText("Deck Galaxy %s", m_host.version.c_str());
            mutedText("Changes save automatically");
            ImGui::PopFont();
        }
        ImGui::EndChild();

        ImGui::SameLine(0.0f, em(0.8f));
        const ImVec2 rule = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(rule.x, rule.y),
                                            ImVec2(rule.x, rule.y + ImGui::GetContentRegionAvail().y - hints),
                                            IM_COL32(255, 255, 255, 18), 1.0f);
        ImGui::SetCursorScreenPos(ImVec2(rule.x + em(1.2f), rule.y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        // One window a page, so each keeps its own scroll and cursor.
        ImGui::PushID(static_cast<int>(m_page));
        ImGui::BeginChild("##page", ImVec2(0.0f, -hints), ImGuiChildFlags_None);
        ImGui::PopStyleVar();
        m_pagePane = ImGui::GetCurrentWindow();
        if (m_focusPage) {
            // Back where it was in this page, or on its first item.
            ImGui::FocusWindow(m_pagePane);
            ImGui::NavInitWindow(m_pagePane, false);
            ImGui::SetNavCursorVisible(true);
            m_focusPage = false;
        }
        // Leaves room for the scrollbar without the page touching it.
        ImGui::PushItemWidth(-em(0.6f));
        ImGui::Indent(em(0.1f));
        switch (m_page) {
        case SettingsPage::Saves: drawSavesPage(); break;
        case SettingsPage::Actions: drawActionsPage(); break;
        case SettingsPage::Bestiary: drawBestiaryPage(); break;
        case SettingsPage::Display: drawDisplayPage(); break;
        case SettingsPage::Sound: drawSoundPage(); break;
        case SettingsPage::Controls: drawControlsPage(); break;
        case SettingsPage::Game: drawGamePage(); break;
        case SettingsPage::Language: drawLanguagePage(); break;
        case SettingsPage::Achievements: drawAchievementsPage(); break;
        case SettingsPage::Interface: drawInterfacePage(); break;
        case SettingsPage::Help: drawHelpPage(); break;
        case SettingsPage::Count: break;
        }
        ImGui::Unindent(em(0.1f));
        ImGui::PopItemWidth();
        ImGui::Dummy(ImVec2(0.0f, em(0.5f)));
        ImGui::EndChild();
        ImGui::PopID(); // the ID stack is the settings window's again
        const bool inPage = g.NavWindow == m_pagePane;
        // Left with nothing further left in the page: back to the list.
        if (inPage && g.NavMoveDir == ImGuiDir_Left && ImGui::NavMoveRequestButNoResultYet()) {
            ImGui::NavMoveRequestCancel();
            m_focusList = true;
        }

        // L1 and R1 turn the pages, as in the game's own menus; not while a
        // slider is held (they speed it up and slow it down) or a list is open.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
            const int turn = ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)   ? count - 1
                             : ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) ? 1
                                                                              : 0;
            if (turn != 0) {
                do {
                    m_page = static_cast<SettingsPage>((static_cast<int>(m_page) + turn) % count);
                } while (!pageShown(m_page));
                // The cursor stays on its side: in the new page, or on its entry.
                if (inPage)
                    m_focusPage = true;
                else
                    m_focusList = true;
            }
        }
        if (hints > 0.0f)
            drawPadHints(hints);
    }
    ImGui::End();
    ImGui::PopStyleVar();
    if (opaque)
        ImGui::PopStyleColor();
}

void Overlay::drawPadHints(float height) {
    const ImGuiContext &g = *ImGui::GetCurrentContext();
    const SDL_GamepadType type = displayedControllerType();
    struct Hint {
        SDL_GamepadButton first;
        SDL_GamepadButton second; // SDL_GAMEPAD_BUTTON_INVALID for none
        const char *text;
    };
    constexpr SDL_GamepadButton kNone = SDL_GAMEPAD_BUTTON_INVALID;
    std::vector<Hint> hints;
    const bool inPage = g.NavWindow && m_pagePane && g.NavWindow->RootWindowForNav == m_pagePane;
    if (ImGui::IsAnyItemActive()) {
        hints = {{SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "Adjust"},
                 {SDL_GAMEPAD_BUTTON_SOUTH, kNone, "Done"}};
    } else if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        hints = {{SDL_GAMEPAD_BUTTON_SOUTH, kNone, "Choose"}, {SDL_GAMEPAD_BUTTON_EAST, kNone, "Close"}};
    } else if (inPage) {
        // "Choose", as in lists: Controls shows the PS2's Select button here.
        hints = {{SDL_GAMEPAD_BUTTON_SOUTH, kNone, "Choose"}, {SDL_GAMEPAD_BUTTON_EAST, kNone, "Back"}};
        if (m_page == SettingsPage::Controls)
            hints.push_back({SDL_GAMEPAD_BUTTON_WEST, kNone, "Clear a binding"});
        hints.push_back({SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "Page"});
    } else {
        hints = {{SDL_GAMEPAD_BUTTON_SOUTH, kNone, "Open"},
                 {SDL_GAMEPAD_BUTTON_EAST, kNone, "Back to the game"},
                 {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "Page"}};
    }

    ImDrawList *list = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float right = at.x + ImGui::GetContentRegionAvail().x;
    list->AddLine(ImVec2(at.x, at.y), ImVec2(right, at.y), IM_COL32(255, 255, 255, 18), 1.0f);
    const float chip = std::floor(ImGui::GetFrameHeight() * 0.8f);
    // A clear gap under the rule; what is left goes below the buttons.
    const float y = std::floor(at.y + em(0.8f));
    float x = at.x;
    for (const Hint &hint : hints) {
        float width = 0.0f;
        for (const SDL_GamepadButton button : {hint.first, hint.second})
            if (button != kNone)
                width += bindingChipWidth(gfx::PadBinding::button(button), type, chip) + em(0.25f);
        const ImVec2 text = ImGui::CalcTextSize(L(hint.text));
        if (x + width + text.x > right)
            break;
        for (const SDL_GamepadButton button : {hint.first, hint.second}) {
            if (button == kNone)
                continue;
            const gfx::PadBinding binding = gfx::PadBinding::button(button);
            drawBindingChip(list, binding, type, ImVec2(x, y), chip);
            x += bindingChipWidth(binding, type, chip) + em(0.25f);
        }
        list->AddText(ImVec2(x + em(0.15f), std::floor(y + (chip - text.y) * 0.5f)), palette::kTextMuted, L(hint.text));
        x += em(0.15f) + text.x + em(1.3f);
    }
    ImGui::Dummy(ImVec2(0.0f, height - ImGui::GetStyle().ItemSpacing.y));
}

// Whether a page is in the list: the actions work on a running game, which
// the home screen has not started (Steam Deck fork: added).
bool Overlay::pageShown(SettingsPage page) const {
    if (page == SettingsPage::Saves)
        return !m_homeOpen && static_cast<bool>(m_host.saveSlots);
    if (page == SettingsPage::Bestiary)
        return !m_homeOpen && static_cast<bool>(m_host.bestiary);
    return page != SettingsPage::Actions || !m_homeOpen;
}

// The save states, managed from the menu: the five slots to save into on the
// left, the five to load from on the right (Steam Deck fork: added). A choice
// closes the menu first, as the slot window's does, so the game runs to do it.
void Overlay::drawSavesPage() {
    const std::vector<HostServices::SaveSlot> slots = m_host.saveSlots ? m_host.saveSlots() : std::vector<HostServices::SaveSlot>{};
    if (slots.empty())
        return;
    const float thumbHeight = em(2.9f);
    int chosenSave = 0, chosenLoad = 0;
    if (ImGui::BeginTable("##slots", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("save", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("load", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        for (int column = 0; column < 2; ++column) {
            ImGui::TableSetColumnIndex(column);
            const bool saving = column == 0;
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.1f);
            ImGui::TextUnformatted(L(saving ? "Save" : "Load"));
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0.0f, em(0.2f)));
        }
        for (int index = 0; index < static_cast<int>(slots.size()); ++index) {
            ImGui::TableNextRow();
            for (int column = 0; column < 2; ++column) {
                ImGui::TableSetColumnIndex(column);
                const bool saving = column == 0;
                ImGui::PushID(column);
                if (slotRow(index + 1, slots[index], saving || slots[index].used, thumbHeight))
                    (saving ? chosenSave : chosenLoad) = index + 1;
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (std::none_of(slots.begin(), slots.end(), [](const HostServices::SaveSlot &slot) { return slot.used; }))
        mutedText("%s", L("No save state yet"));

    if (chosenLoad != 0) {
        const int slot = chosenLoad;
        m_slotLast = slot;
        setMenuOpen(false);
        if (m_host.saveSlotLoad)
            m_host.saveSlotLoad(slot);
    } else if (chosenSave != 0) {
        m_slotLast = chosenSave;
        if (slots[chosenSave - 1].used) {
            m_slotConfirm = chosenSave;
        } else {
            const int slot = chosenSave;
            setMenuOpen(false);
            if (m_host.saveSlotSave)
                m_host.saveSlotSave(slot);
        }
    }

    // The overwrite question, as the slot window asks it.
    constexpr const char *kQuestion = "##overwritepage";
    if (m_slotConfirm != 0 && !ImGui::IsPopupOpen(kQuestion))
        ImGui::OpenPopup(kQuestion);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em(1.6f), em(1.2f)));
    if (ImGui::BeginPopupModal(kQuestion, nullptr,
                               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
        decorateWindow();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
        ImGui::TextUnformatted(Lf("Overwrite slot %d?", m_slotConfirm).c_str());
        ImGui::PopFont();
        mutedText("What is in it is lost.");
        ImGui::Dummy(ImVec2(0.0f, em(0.6f)));
        const ImVec2 buttonSize(em(7.0f), 0.0f);
        // Cancel first, but the cursor starts on Overwrite. Back reaches Cancel
        // as the Quit question's.
        if (ImGui::Button(L("Cancel"), buttonSize) || m_cancelQuit) {
            ImGui::CloseCurrentPopup();
            m_slotConfirm = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button(L("Overwrite"), buttonSize)) {
            const int slot = m_slotConfirm;
            ImGui::CloseCurrentPopup();
            m_slotConfirm = 0;
            setMenuOpen(false);
            if (m_host.saveSlotSave)
                m_host.saveSlotSave(slot);
        }
        ImGui::SetItemDefaultFocus();
        ImGui::EndPopup();
    } else {
        m_slotConfirm = 0;
    }
    ImGui::PopStyleVar();
}

// What the top bar's Game, Save states and View menus held, as buttons and
// switches a controller reaches (Steam Deck fork: added).
void Overlay::drawActionsPage() {
    if (canPause() || canChangeSpeed()) {
        sectionHeader("Pause and speed");
        if (beginSettings("actions")) {
            if (canPause()) {
                settingRow("Pause", "F5");
                bool paused = m_userPaused;
                if (toggle("pause", &paused))
                    setUserPaused(paused);
            }
            if (canChangeSpeed()) {
                settingRow("Fast-forward", "F4");
                bool fast = m_fastForward;
                if (toggle("fastforward", &fast))
                    setFastForward(fast);
                settingRow("Fast-forward speed");
                static constexpr float kSpeeds[] = {2.0f, 3.0f, 4.0f, 0.0f};
                static constexpr const char *kSpeedNames[] = {"2\xC3\x97", "3\xC3\x97", "4\xC3\x97", "Unlimited"};
                int speed = 0;
                for (int index = 0; index < 4; ++index)
                    if (m_settings.fastForwardSpeed == kSpeeds[index])
                        speed = index;
                if (segmented("speed", &speed, kSpeedNames, 4)) {
                    m_settings.fastForwardSpeed = kSpeeds[speed];
                    if (m_fastForward)
                        m_host.setSpeed(m_settings.fastForwardSpeed);
                    markDirty();
                }
            }
            endSettings();
        }
    }
    sectionHeader("Sound");
    if (beginSettings("mute")) {
        settingRow("Mute sound");
        bool muted = m_settings.muted;
        if (toggle("muted", &muted)) {
            m_settings.muted = muted;
            applyVolume();
            markDirty();
        }
        endSettings();
    }
    sectionHeader("Screenshots");
    if (iconButton("Take screenshot", Icon::Camera))
        takeScreenshot(false);
    if (iconButton("Screenshot with the menu", Icon::Camera))
        takeScreenshot(true);
    if (iconButton("Open screenshots folder", Icon::Folder)) {
        std::error_code error;
        std::filesystem::create_directories(screenshotFolder(), error);
        SDL_OpenURL(("file://" + screenshotFolder()).c_str());
    }
    sectionHeader("View");
    if (beginSettings("view")) {
        settingRow("Renderer statistics");
        bool stats = m_statsOpen;
        if (toggle("stats", &stats))
            m_statsOpen = stats;
        endSettings();
    }
}

void Overlay::drawHelpPage() {
    sectionHeader("Help");
    if (iconButton("Shortcuts", Icon::Keyboard))
        m_shortcutsOpen = true;
    if (iconButton("Project page", Icon::Link))
        SDL_OpenURL(kProjectUrl);
    if (iconButton("Report a problem", Icon::Link))
        SDL_OpenURL(kIssuesUrl);
    if (iconButton("About Deck Galaxy", Icon::Info))
        m_aboutOpen = true;
}

void Overlay::drawDisplayPage() {
    sectionHeader("Picture");
    if (beginSettings("picture")) {
        settingRow("Internal resolution",
                   "Draws the game at a multiple of the PS2's own resolution: sharper, and more work for the "
                   "graphics card.",
                   "Experimental");
        const uint32_t active = m_backend.activeResolutionScale();
        const uint32_t shown = m_pendingScale != 0u ? m_pendingScale : active;
        char preview[96];
        const std::string waiting =
            m_pendingScale == 0u ? std::string() : "   " + L(std::string(pausing() ? "when the game resumes" : "applying..."));
        std::snprintf(preview, sizeof(preview), "%u%s   %u %s %u%s", shown, kTimes, 512u * shown, kTimes, 448u * shown,
                      waiting.c_str());
        if (beginCombo("##scale", preview)) {
            for (uint32_t scale = 1u; scale <= 2u; ++scale) {
                char item[64];
                std::snprintf(item, sizeof(item), "%u%s   %u %s %u%s", scale, kTimes, 512u * scale, kTimes,
                              448u * scale, scale == 1u ? "   (PS2)" : "");
                if (ImGui::Selectable(item, scale == shown))
                    requestScale(scale);
            }
            ImGui::EndCombo();
        }
        endSettings();
    }
    // The game's camera and view options (Steam Deck fork: moved here from Game).
    drawGameOptions("display", "", "Camera and view");
}

void Overlay::drawSoundPage() {
    sectionHeader("Volume");
    if (beginSettings("sound")) {
        settingRow("Master volume");
        if (sliderInt("##volume", &m_settings.volume, 0, 100, "%d%%")) {
            applyVolume();
            markDirty();
        }
        endSettings();
    }
    if (!m_host.setVolume)
        mutedText("Sound is not available in this build.");
}

void Overlay::setMenuLanguageChoice(const std::string &setting) {
    m_settings.menuLanguage = setting;
    setMenuLanguage(setting);
    markDirty();
}

void Overlay::drawInterfacePage() {
    sectionHeader("Menu");
    if (beginSettings("menu")) {
        settingRow("Menu size", "Scales this menu and its messages.");
        // Applied on release: the menu resizing under the cursor mid-drag
        // would move the slider away from it.
        if (!ImGui::IsAnyItemActive() || m_uiScaleEdit == 0)
            m_uiScaleEdit = m_settings.uiScale;
        sliderInt("##uiscale", &m_uiScaleEdit, 75, 200, "%d%%");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            m_settings.uiScale = m_uiScaleEdit;
            markDirty();
        }
        settingRow("Notifications", "Short messages about controllers, screenshots and changes.");
        if (toggle("notifications", &m_settings.notifications))
            markDirty();
        endSettings();
    }

    if (canPause() || canChangeSpeed()) {
        sectionHeader("Pause and speed");
        if (beginSettings("pausing")) {
            if (canPause()) {
                settingRow("Pause while the menu is open");
                if (toggle("pausemenu", &m_settings.pauseInMenu)) {
                    applyPause();
                    markDirty();
                }
                settingRow("Pause in the background", "Stops the game while its window is not in front.");
                if (toggle("pausebackground", &m_settings.pauseInBackground)) {
                    applyPause();
                    markDirty();
                }
            }
            if (canChangeSpeed()) {
                settingRow("Fast-forward speed",
                           "F4, or Actions > Fast-forward, switches between it and normal speed. "
                           "Music keeps its tempo.",
                           "Experimental");
                static constexpr float kSpeeds[] = {2.0f, 3.0f, 4.0f, 0.0f};
                static constexpr const char *kSpeedNames[] = {"2\xC3\x97", "3\xC3\x97", "4\xC3\x97", "Unlimited"};
                int speed = 0;
                for (int index = 0; index < 4; ++index)
                    if (m_settings.fastForwardSpeed == kSpeeds[index])
                        speed = index;
                if (segmented("speed", &speed, kSpeedNames, 4)) {
                    m_settings.fastForwardSpeed = kSpeeds[speed];
                    if (m_fastForward)
                        m_host.setSpeed(m_settings.fastForwardSpeed);
                    markDirty();
                }
            }
            endSettings();
        }
    }
    // The version check (Steam Deck fork: moved here from Game).
    drawGameOptions("interface", "", "Updates");
}

// The game's own switches, in the host's sections (Rogue Galaxy: added).
void Overlay::drawGamePage() {
    drawGameOptions(std::string(), "The game has no options of its own.", "Game");
}

void Overlay::drawLanguagePage() {
    // The menu's own language, with the game's (Steam Deck fork: moved here
    // from Interface).
    sectionHeader("Menu");
    if (beginSettings("menulanguage")) {
        settingRow("Menu language", "The language of this menu and of the home screen. It starts as the Steam "
                                    "Deck's own.");
        if (beginCombo("##menulanguage", kMenuLanguageNames[menuLanguageIndex()])) {
            for (int language = 0; language < kMenuLanguageCount; ++language)
                if (ImGui::Selectable(kMenuLanguageNames[language], menuLanguageIndex() == language))
                    setMenuLanguageChoice(kMenuLanguageCodes[language]);
            ImGui::EndCombo();
        }
        endSettings();
    }
    drawGameOptions("language", "There are no language options.", "Game");
}

void Overlay::drawGameOptions(const std::string &page, const char *emptyText, const char *firstTitle) {
    if (std::none_of(m_host.gameOptions.begin(), m_host.gameOptions.end(),
                     [&](const GameOption &option) { return option.page == page; })) {
        // Wrapped at the pane's edge, which a long line would run past.
        ImGui::PushTextWrapPos(0.0f);
        mutedText("%s", L(emptyText));
        ImGui::PopTextWrapPos();
        return;
    }
    bool started = false, table = false;
    for (const GameOption &option : m_host.gameOptions) {
        if (option.page != page)
            continue;
        const auto section = std::find_if(m_host.gameSections.begin(), m_host.gameSections.end(),
                                          [&](const auto &entry) { return entry.first == option.key; });
        if (!started || section != m_host.gameSections.end()) {
            if (table)
                endSettings();
            sectionHeader(section != m_host.gameSections.end() ? section->second.c_str() : firstTitle);
            table = beginSettings(("game." + option.key).c_str());
            started = true;
        }
        if (!table)
            continue;
        // Greyed out, with the line that says what would make it work, while
        // it does nothing (Rogue Galaxy: added).
        const char *reason = m_host.gameOptionBlocked ? m_host.gameOptionBlocked(option.key) : nullptr;
        {
            const Blocked blocked(reason);
            const bool restart = !reason && waitsForRestart(option);
            settingRow(option.label.c_str(), option.help.empty() ? nullptr : option.help.c_str(), nullptr,
                       reason ? reason : restart ? "Restart the game for this to take effect." : nullptr,
                       reason ? palette::kWarn : palette::kBad);
            drawGameControl(option);
        }
    }
    if (table)
        endSettings();
}

void Overlay::drawGameControl(const GameOption &option) {
    if (option.number() && !option.choices.empty()) {
        // Rogue Galaxy: added, a value a button (the text's language).
        const int value = gameNumber(option);
        int index = value - option.minimum;
        std::vector<const char *> labels;
        for (const std::string &choice : option.choices)
            labels.push_back(choice.c_str());
        // The values that would do nothing greyed out (Rogue Galaxy: added).
        std::unique_ptr<bool[]> usable;
        if (m_host.gameChoiceBlocked) {
            usable = std::make_unique<bool[]>(labels.size());
            for (size_t choice = 0; choice < labels.size(); ++choice)
                usable[choice] = !m_host.gameChoiceBlocked(option.key, option.minimum + static_cast<int>(choice));
        }
        if (segmented(option.key.c_str(), &index, labels.data(), static_cast<int>(labels.size()), usable.get())) {
            const int next = std::clamp(option.minimum + index, option.minimum, option.maximum);
            if (next != value) {
                m_settings.gameNumbers[option.key] = next;
                m_settings.gameOptions.erase(option.key);
                if (m_host.setGameNumber)
                    m_host.setGameNumber(option.key, next);
                markDirty();
            }
        }
        return;
    }
    if (option.number()) {
        const int value = gameNumber(option);
        int next = value;
        const ImGuiStyle &style = ImGui::GetStyle();
        const float button = ImGui::GetFrameHeight();
        const float reset = ImGui::CalcTextSize(L("Default")).x + style.FramePadding.x * 2.0f;
        ImGui::PushID(option.key.c_str());
        if (ImGui::Button("-", ImVec2(button, 0.0f)))
            next = value - option.step;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(button * 3.0f, ImGui::GetContentRegionAvail().x - button -
                                                            reset - style.ItemSpacing.x * 2.0f));
        // A multiplier's text has its value written in already: the
        // slider prints it as it is (no % in it), and typing a value in
        // is off, as what it would read is hundredths.
        const bool multiplier = option.format == kMultiplierFormat;
        char text[16];
        const char *format = option.format.empty() ? "%d" : L(option.format.c_str());
        if (multiplier) {
            multiplierText(value, text, sizeof(text));
            format = text;
        }
        sliderInt("##value", &next, option.minimum, option.maximum, format,
                         multiplier ? ImGuiSliderFlags_NoInput : ImGuiSliderFlags_None);
        ImGui::SameLine();
        if (ImGui::Button("+", ImVec2(button, 0.0f)))
            next = value + option.step;
        ImGui::SameLine();
        ImGui::BeginDisabled(value == option.defaultNumber);
        if (ImGui::Button(L("Default")))
            next = option.defaultNumber;
        ImGui::EndDisabled();
        ImGui::PopID();
        next = std::clamp(next, option.minimum, option.maximum);
        if (next != value) {
            m_settings.gameNumbers[option.key] = next;
            // A "0" or "1" in the file reads as a switch too.
            m_settings.gameOptions.erase(option.key);
            if (m_host.setGameNumber)
                m_host.setGameNumber(option.key, next);
            markDirty();
        }
        return;
    }
    bool on = gameOption(option);
    if (!option.remembered) {
        if (toggle(option.key.c_str(), &on) && m_host.setGameOption)
            m_host.setGameOption(option.key, on);
        return;
    }
    if (toggle(option.key.c_str(), &on)) {
        m_settings.gameOptions[option.key] = on;
        m_settings.gameNumbers.erase(option.key);
        if (m_host.setGameOption)
            m_host.setGameOption(option.key, on);
        markDirty();
    }
}

void Overlay::drawControlsPage() {
    sectionHeader("Buttons");
    {
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 size(std::min(width * 0.64f, em(21.0f)), std::min(width * 0.64f, em(21.0f)) * 0.43f);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(size);
        drawPadDiagram(ImGui::GetWindowDrawList(), at, size, m_backend.padInput().preview(), m_hoveredInput);
        ImGui::SameLine(0.0f, em(1.2f));
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0.0f, em(0.6f)));
        ImGui::TextUnformatted(L("Test your bindings"));
        ImGui::PushTextWrapPos(0.0f);
        mutedText("Press anything: the pad lights up with what the game would read.");
        if (padInUse())
            mutedText("%s on a binding changes it; %s clears it.", padButtonName(SDL_GAMEPAD_BUTTON_SOUTH).c_str(),
                      padButtonName(SDL_GAMEPAD_BUTTON_WEST).c_str());
        else
            mutedText("Click a binding to change it; right-click to clear it.");
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }
    ImGui::Dummy(ImVec2(0.0f, em(0.2f)));
    m_keyboardBindings = false;
    drawBindingTable(false);

    sectionHeader("Analog sticks");
    if (beginSettings("analog")) {
        gfx::PadConfig config = m_settings.pad;
        bool changed = false;
        settingRow("Dead zone", "Ignores the small drift of a worn stick.");
        float deadZone = config.stickDeadZone * 100.0f;
        if (sliderFloat("##deadzone", &deadZone, 0.0f, 40.0f, "%.0f%%")) {
            config.stickDeadZone = deadZone / 100.0f;
            changed = true;
        }
        settingRow("Trigger threshold", "How far L2 and R2 travel before they count as pressed.");
        float threshold = config.triggerThreshold * 100.0f;
        if (sliderFloat("##trigger", &threshold, 5.0f, 90.0f, "%.0f%%")) {
            config.triggerThreshold = threshold / 100.0f;
            changed = true;
        }
        settingRow("Invert camera left and right", "The right stick.");
        changed |= toggle("invertx", &config.invertCameraX);
        settingRow("Invert camera up and down",
                   "The right stick.");
        changed |= toggle("inverty", &config.invertCameraY);
        endSettings();
        if (changed)
            setPadConfig(config);
    }
}

void Overlay::drawBindingTable(bool keyboard) {
    const SDL_GamepadType type = displayedControllerType();
    const float chip = std::floor(ImGui::GetFrameHeight() * 0.92f);
    PadInput hovered = PadInput::Count;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable(keyboard ? "##keys" : "##buttons", 3, flags)) {
        ImGui::TableSetupColumn("PS2", ImGuiTableColumnFlags_WidthStretch, 1.25f);
        ImGui::TableSetupColumn(L(keyboard ? "Key" : "Button"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(L("Alternate"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color(palette::kTextMuted));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();
        for (size_t index = 0; index < gfx::kPadInputCount; ++index) {
            const auto input = static_cast<PadInput>(index);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, chip + em(0.35f));
            ImGui::TableNextColumn();
            ImDrawList *list = ImGui::GetWindowDrawList();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const ImVec2 name = ImGui::CalcTextSize(L(gfx::padInputName(input)));
            ImGui::Dummy(ImVec2(chip + em(0.6f) + name.x, chip));
            drawPadGlyph(list, input, ImVec2(at.x + chip * 0.5f, at.y + chip * 0.5f), chip * 0.9f);
            list->AddText(ImVec2(at.x + chip + em(0.6f), std::floor(at.y + (chip - name.y) * 0.5f)), palette::kText,
                          L(gfx::padInputName(input)));

            for (int slot = 0; slot < static_cast<int>(gfx::kBindingSlots); ++slot) {
                ImGui::TableNextColumn();
                const gfx::PadBinding binding = m_settings.pad.slots(input, keyboard)[slot];
                ImGui::PushID(static_cast<int>(index * gfx::kBindingSlots + slot));
                const ImVec2 cell = ImGui::GetCursorScreenPos();
                const float cellWidth = ImGui::GetContentRegionAvail().x;
                if (ImGui::InvisibleButton("##bind", ImVec2(cellWidth, chip), ImGuiButtonFlags_EnableNav))
                    beginBindingCapture(input, keyboard, slot);
                const bool over = ImGui::IsItemHovered() || ImGui::IsItemFocused();
                // Cleared by a right-click, or X (Square) or Delete on the cursor.
                const bool clear = ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) ||
                                                             ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                                                             ImGui::IsKeyPressed(ImGuiKey_Backspace, false));
                if (clear || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    gfx::PadConfig config = m_settings.pad;
                    config.slots(input, keyboard)[slot] = {};
                    setPadConfig(config);
                }
                if (over)
                    list->AddRectFilled(ImVec2(cell.x - em(0.2f), cell.y - em(0.12f)),
                                        ImVec2(cell.x + cellWidth, cell.y + chip + em(0.12f)), palette::kHover,
                                        ImGui::GetStyle().FrameRounding);
                drawBindingChip(list, binding, type, ImVec2(cell.x + em(0.2f), cell.y), chip,
                                binding.bound() ? 1.0f : 0.7f);
                const std::string clash = conflictWith(m_settings.pad, input, keyboard, binding);
                if (!clash.empty()) {
                    const float x = cell.x + em(0.2f) + bindingChipWidth(binding, type, chip) + em(0.7f);
                    drawIcon(list, Icon::Warning, ImVec2(x, cell.y + chip * 0.5f), em(0.95f), palette::kWarn);
                }
                if (ImGui::BeginItemTooltip()) {
                    ImGui::TextUnformatted(bindingLabel(binding, type).c_str());
                    if (!clash.empty())
                        ImGui::TextColored(color(palette::kWarn), L("Also bound to %s"), clash.c_str());
                    mutedText("Click to change, right-click to clear");
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
            if (ImGui::TableGetHoveredRow() == ImGui::TableGetRowIndex())
                hovered = input;
        }
        ImGui::EndTable();
    }
    m_hoveredInput = hovered;
    ImGui::Dummy(ImVec2(0.0f, em(0.2f)));
    if (iconButton(keyboard ? "Restore default keys" : "Restore default buttons", Icon::Reset)) {
        gfx::PadConfig config = m_settings.pad;
        const gfx::PadConfig defaults = gfx::PadConfig::defaults();
        (keyboard ? config.keyboard : config.controller) = keyboard ? defaults.keyboard : defaults.controller;
        setPadConfig(config);
        notify(keyboard ? "Keys restored" : "Buttons restored", Icon::Check);
    }
}

void Overlay::drawAbout() {
    centerNextWindow();
    ImGui::SetNextWindowSize(ImVec2(std::min(ImGui::GetMainViewport()->WorkSize.x - em(2.0f), em(38.0f)), 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em(1.4f), em(1.2f)));
    if (beginWindow("##about", nullptr, kDialogFlags & ~ImGuiWindowFlags_AlwaysAutoResize)) {
        const IconRoom room = iconRoom(em(3.0f));
        ImGui::SameLine(0.0f, em(0.9f));
        ImGui::BeginGroup();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);
        ImGui::TextUnformatted("Deck Galaxy");
        const float titleMiddle = lastItemMiddle();
        ImGui::PopFont();
        if (m_host.revision.empty())
            mutedText("Version %s", m_host.version.c_str());
        else
            mutedText("Version %s (%s)", m_host.version.c_str(), m_host.revision.c_str());
        const auto newer = m_host.newerRelease ? m_host.newerRelease() : std::pair<std::string, std::string>{};
        if (!newer.first.empty())
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(palette::kGold), L("Version %s is out"),
                               newer.first.c_str());
        ImGui::EndGroup();
        drawIconBeside(room, Icon::Disc, palette::kGold);
        if (closeButtonBeside("##close", room.pos.y, titleMiddle))
            m_aboutOpen = false;
        ImGui::Dummy(ImVec2(0.0f, em(0.3f)));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(L("A native PC port of Rogue Galaxy, made by statically recompiling the PlayStation 2 "
                                 "game. It plays from your own copy of the game."));
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0.0f, em(0.3f)));
        if (beginSettings("about")) {
            settingRow("Renderer");
            ImGui::Text("SDL GPU (%s)", m_backend.driverName().c_str());
            settingRow("Internal resolution");
            ImGui::Text("%u%s", m_backend.activeResolutionScale(), kTimes);
            const int sdl = SDL_GetVersion();
            settingRow("Built with");
            ImGui::Text("SDL %d.%d.%d, Dear ImGui %s", SDL_VERSIONNUM_MAJOR(sdl), SDL_VERSIONNUM_MINOR(sdl),
                        SDL_VERSIONNUM_MICRO(sdl), IMGUI_VERSION);
            settingRow("Recompiler");
            ImGui::TextUnformatted("PS2Recomp");
            endSettings();
        }
        ImGui::Dummy(ImVec2(0.0f, em(0.3f)));
        if (!newer.first.empty()) {
            if (iconButton(Lf("Get version %s", newer.first.c_str()).c_str(), Icon::Link, true))
                SDL_OpenURL(newer.second.c_str());
            ImGui::SameLine();
        }
        if (iconButton("Project page", Icon::Link, newer.first.empty()))
            SDL_OpenURL(kProjectUrl);
        ImGui::SameLine();
        if (iconButton("Report a problem", Icon::Warning))
            SDL_OpenURL(kIssuesUrl);
        ImGui::Dummy(ImVec2(0.0f, em(0.2f)));
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.84f);
        ImGui::PushTextWrapPos(0.0f);
        mutedText("Rogue Galaxy is a trademark of Sony Interactive Entertainment. This project is not "
                        "affiliated with Sony or Level-5, and includes nothing from the game.");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Overlay::drawShortcuts() {
    centerNextWindow();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em(1.4f), em(1.2f)));
    if (beginWindow("##shortcuts", nullptr, kDialogFlags)) {
        dialogHeader(Icon::Keyboard, "Shortcuts", &m_shortcutsOpen);
        sectionHeader("Keyboard");
        if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_None)) {
            ImGui::TableSetupColumn("keys", ImGuiTableColumnFlags_WidthFixed, em(7.5f));
            ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthFixed, em(15.0f));
            static constexpr const char *kF1[] = {"F1"}, *kF4[] = {"F4"}, *kF5[] = {"F5"},
                                        *kF12[] = {"F12"}, *kEsc[] = {"Esc"};
            shortcutRow(kF1, 1, "Open or close this menu");
            if (canPause())
                shortcutRow(kF5, 1, "Pause or resume");
            if (canChangeSpeed())
                shortcutRow(kF4, 1, "Fast-forward");
            // The game's own keys (Rogue Galaxy: added; F6, the free camera).
            for (const HostServices::Shortcut &shortcut : m_host.gameShortcuts) {
                if (!shortcut.listed)
                    continue;
                const char *const keys[] = {SDL_GetScancodeName(shortcut.key)};
                shortcutRow(keys, 1, shortcut.what.c_str());
            }
            shortcutRow(kF12, 1, "Take screenshot");
            shortcutRow(kEsc, 1, "Back, in the menu");
            ImGui::EndTable();
        }
        sectionHeader("Controller");
        if (ImGui::BeginTable("##pad", 2, ImGuiTableFlags_None)) {
            ImGui::TableSetupColumn("keys", ImGuiTableColumnFlags_WidthFixed, em(7.5f));
            ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthFixed, em(15.0f));
            static constexpr const char *kGuide[] = {"Guide"}, *kL4[] = {"L4"},
                                        *kShoulders[] = {"L1", "R1"},
                                        *kBack[] = {"B / Circle"},
                                        *kClear[] = {"X / Square"};
            if (m_host.steamDeck)
                shortcutRow(kL4, 1, "Open or close the menu");
            else
                shortcutRow(kGuide, 1, "Open or close the menu");
            shortcutRow(kShoulders, 2, "Previous and next settings page", "/");
            shortcutRow(kBack, 1, "Back, in the menu");
            shortcutRow(kClear, 1, "Clear a binding (Controls)");
            ImGui::EndTable();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// The save states' slots, to save into or load from (Steam Deck fork: added).
// A row a picture, the slot and its time; Enter or A chooses, Esc or B leaves.
// A saved slot asks before it is overwritten; an empty one cannot be loaded.
void Overlay::drawSaveSlots() {
    const bool saving = m_slotMode == 1;
    const std::vector<HostServices::SaveSlot> slots = m_host.saveSlots ? m_host.saveSlots() : std::vector<HostServices::SaveSlot>{};
    if (slots.empty()) {
        closeSaveSlots();
        return;
    }
    const ImGuiIO &io = ImGui::GetIO();
    const bool back = ((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) &&
                       ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) ||
                      ((io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) &&
                       ImGui::IsKeyPressed(ImGuiKey_Escape, false));
    if (back) {
        if (m_slotConfirm != 0) {
            m_slotCancel = true;
        } else {
            closeSaveSlots();
            return;
        }
    }
    const bool anyUsed = std::any_of(slots.begin(), slots.end(), [](const HostServices::SaveSlot &slot) { return slot.used; });
    // Where the cursor starts: the slot used last, or for a load the first one
    // that holds something.
    int focus = std::clamp(m_slotLast, 1, static_cast<int>(slots.size()));
    if (!saving && !slots[focus - 1].used)
        for (int index = 0; index < static_cast<int>(slots.size()); ++index)
            if (slots[index].used) {
                focus = index + 1;
                break;
            }

    centerNextWindow();
    if (m_slotFocus)
        ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowSize(ImVec2(std::min(ImGui::GetMainViewport()->WorkSize.x - em(2.0f), em(30.0f)), 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em(1.4f), em(1.2f)));
    bool open = true;
    if (beginWindow("##slots", nullptr, kDialogFlags & ~ImGuiWindowFlags_AlwaysAutoResize)) {
        dialogHeader(saving ? Icon::Check : Icon::Disc, saving ? "Save" : "Load", &open);
        if (!saving && !anyUsed)
            mutedText("%s", L("No save state yet"));
        const float thumbHeight = em(3.6f);
        int chosen = 0;
        for (int index = 0; index < static_cast<int>(slots.size()); ++index) {
            const int slot = index + 1;
            const HostServices::SaveSlot &entry = slots[index];
            if (slotRow(slot, entry, saving || entry.used, thumbHeight))
                chosen = slot;
            if (m_slotFocus && slot == focus) {
                ImGui::FocusItem();
                ImGui::SetNavCursorVisible(true);
            }
        }
        m_slotFocus = false;
        if (chosen != 0) {
            m_slotLast = chosen;
            if (saving && slots[chosen - 1].used) {
                m_slotConfirm = chosen;
            } else {
                const int slot = chosen;
                closeSaveSlots();
                if (saving) {
                    if (m_host.saveSlotSave)
                        m_host.saveSlotSave(slot);
                } else if (m_host.saveSlotLoad) {
                    m_host.saveSlotLoad(slot);
                }
            }
        }

        // The overwrite question.
        constexpr const char *kQuestion = "##overwrite";
        if (m_slotConfirm != 0 && !ImGui::IsPopupOpen(kQuestion))
            ImGui::OpenPopup(kQuestion);
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->GetCenter()), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em(1.6f), em(1.2f)));
        if (ImGui::BeginPopupModal(kQuestion, nullptr,
                                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
            decorateWindow();
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
            ImGui::TextUnformatted(Lf("Overwrite slot %d?", m_slotConfirm).c_str());
            ImGui::PopFont();
            mutedText("What is in it is lost.");
            ImGui::Dummy(ImVec2(0.0f, em(0.6f)));
            const ImVec2 buttonSize(em(7.0f), 0.0f);
            // Cancel first, but the cursor starts on Overwrite.
            if (ImGui::Button(L("Cancel"), buttonSize) || m_slotCancel) {
                ImGui::CloseCurrentPopup();
                m_slotConfirm = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button(L("Overwrite"), buttonSize)) {
                const int slot = m_slotConfirm;
                ImGui::CloseCurrentPopup();
                closeSaveSlots();
                if (m_host.saveSlotSave)
                    m_host.saveSlotSave(slot);
            }
            ImGui::SetItemDefaultFocus();
            ImGui::EndPopup();
        } else if (m_slotConfirm != 0 && m_slotCancel) {
            m_slotConfirm = 0;
        }
        ImGui::PopStyleVar();
        m_slotCancel = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
    if (!open)
        closeSaveSlots();
}

void Overlay::drawAchievementsPage() {
    const AchievementAccount account = m_host.achievementAccount ? m_host.achievementAccount() : AchievementAccount{};
    // Signing in turns the option on and signing out turns it off, from here
    // (the first time the page is drawn only notes who is signed in).
    if (m_achievementsSignedInKnown && account.signedIn != m_achievementsSignedIn)
        setGameOptionValue("achievements", account.signedIn);
    m_achievementsSignedIn = account.signedIn;
    m_achievementsSignedInKnown = true;
    const ImVec4 gold = ImGui::ColorConvertU32ToFloat4(palette::kGold);
    const ImVec4 muted = ImGui::ColorConvertU32ToFloat4(palette::kTextMuted);
    const ImVec4 good = ImGui::ColorConvertU32ToFloat4(palette::kGood);
    const ImVec4 bad = ImGui::ColorConvertU32ToFloat4(palette::kBad);

    // First the switch, alone while it is off. Turned on, a player not yet
    // signed in gets the sign-in fields, and one signed in gets the account and
    // the list. Its switch before its name, and its help across the whole width
    // (the settings table keeps a help in the name's column).
    const auto option = std::find_if(m_host.gameOptions.begin(), m_host.gameOptions.end(),
                                     [](const GameOption &candidate) { return candidate.key == "achievements"; });
    if (option == m_host.gameOptions.end())
        return;
    ImGui::Dummy(ImVec2(0.0f, em(0.3f)));
    ImGui::AlignTextToFramePadding();
    drawGameControl(*option);
    ImGui::SameLine(0.0f, em(0.8f));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(L(option->label.c_str()));
    if (!option->help.empty()) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.84f);
        ImGui::PushStyleColor(ImGuiCol_Text, muted);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(L(option->help.c_str()));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    if (!gameOption(*option))
        return;

    sectionHeader("RetroAchievements account");
    ImGui::PushTextWrapPos(0.0f);
    if (account.signedIn) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(good, L("Signed in as %s"), account.user.c_str());
        ImGui::SameLine(0.0f, em(1.0f));
        if (ImGui::Button(L("Sign out")) && m_host.achievementSignOut)
            m_host.achievementSignOut();
    } else {
        ImGui::InputTextWithHint("##ra-user", L("RetroAchievements user"), m_raUser, sizeof m_raUser);
        ImGui::InputTextWithHint("##ra-password", L("Password"), m_raPassword, sizeof m_raPassword,
                                 ImGuiInputTextFlags_Password);
        const bool ready = !account.busy && m_raUser[0] != '\0' && m_raPassword[0] != '\0';
        ImGui::BeginDisabled(!ready);
        if (iconButton("Sign in", Icon::Check, true) && m_host.achievementSignIn) {
            m_host.achievementSignIn(m_raUser, m_raPassword);
            std::memset(m_raPassword, 0, sizeof m_raPassword); // kept nowhere
        }
        ImGui::EndDisabled();
    }
    if (!account.message.empty())
        ImGui::TextColored(account.failed ? bad : muted, "%s", L(account.message.c_str()));
    if (!account.signedIn)
        mutedText("The password is sent once to retroachievements.org and never kept; only a connection token "
                  "is, on this machine.");
    ImGui::PopTextWrapPos();
    if (!account.signedIn)
        return;

    const AchievementList list = m_host.achievements ? m_host.achievements() : AchievementList{};
    sectionHeader("Achievements");
    ImGui::PushTextWrapPos(0.0f);
    uint32_t earnedPoints = 0u, totalPoints = 0u;
    for (const AchievementRow &row : list.rows) {
        totalPoints += row.points;
        if (row.earned)
            earnedPoints += row.points;
    }
    mutedText("%s", L(list.summary.c_str()));
    if (!list.rows.empty()) {
        mutedText("%u of %u points", earnedPoints, totalPoints);
        ImGui::Checkbox(L("Only the ones still to earn"), &m_achievementsLockedOnly);
    }
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, em(0.3f)));
    const float side = em(3.2f);
    for (size_t i = 0; i < list.rows.size(); ++i) {
        const AchievementRow &row = list.rows[i];
        if (m_achievementsLockedOnly && row.earned)
            continue;
        ImGui::PushID(static_cast<int>(i));
        // A row the cursor can stop on, so the controller scrolls the page
        // along the list; its picture and texts are drawn over it.
        const ImVec2 corner = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemAllowOverlap();
        ImGui::Selectable("##row", false, ImGuiSelectableFlags_None, ImVec2(0.0f, side));
        const ImVec2 after = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(corner);
        // The badge, as on RetroAchievements' page: the colour one once
        // earned, the grey one before.
        ImTextureRef badge;
        ImVec2 badgePixels;
        if (!row.badge.empty() && fileTexture(row.badge.c_str(), badge, badgePixels))
            ImGui::Image(badge, ImVec2(side, side));
        else
            ImGui::Dummy(ImVec2(side, side));
        if (row.earned)
            ImGui::GetWindowDrawList()->AddRect(corner, ImVec2(corner.x + side, corner.y + side), palette::kGold, 3.0f,
                                                2.0f);
        ImGui::SameLine(0.0f, em(0.8f));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(row.earned ? gold : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", row.title.c_str());
        ImGui::SameLine();
        ImGui::TextColored(muted, "(%u)", row.points);
        if (!row.progress.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(gold, "%s", row.progress.c_str());
        }
        ImGui::TextColored(muted, "%s", row.description.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(after);
        ImGui::PopID();
    }
}

void Overlay::drawBestiaryPage() {
    if (!m_host.bestiary)
        return;

    if (!m_bestiaryAtlasTried && m_host.bestiaryIcons) {
        m_bestiaryAtlasTried = true;
        const BestiaryAtlas atlas = m_host.bestiaryIcons();
        if (atlas.width > 0 && atlas.height > 0 && !atlas.rgba.empty())
            m_bestiaryAtlasReady = rgbaTexture("bestiary_icons", atlas.width, atlas.height, atlas.rgba.data(),
                                               m_bestiaryAtlasTex, m_bestiaryAtlasSize);
    }

    const double t = now();
    if (m_bestiaryAt < 0.0 || t - m_bestiaryAt > 0.5) {
        m_bestiaryCache = m_host.bestiary();
        m_bestiaryAt = t;
    }

    const ImVec4 muted = ImGui::ColorConvertU32ToFloat4(palette::kTextMuted);

    if (!m_bestiaryCache.ready) {
        ImGui::Dummy(ImVec2(0.0f, em(0.4f)));
        ImGui::PushTextWrapPos(0.0f);
        mutedText("%s", L("Start or load a game to view the bestiary."));
        ImGui::PopTextWrapPos();
        return;
    }

    static constexpr const char *kTabs[] = {"Monsters", "Items"};
    ImGui::Dummy(ImVec2(0.0f, em(0.2f)));
    segmented("bestiary_tab", &m_bestiaryTab, kTabs, 2);
    ImGui::Dummy(ImVec2(0.0f, em(0.3f)));

    ImGui::SetNextItemWidth(std::max(em(12.0f), ImGui::GetContentRegionAvail().x - em(11.5f)));
    ImGui::InputTextWithHint("##bestiary-search", L("Search..."), m_bestiarySearch, sizeof m_bestiarySearch);
    ImGui::SameLine(0.0f, em(0.6f));
    ImGui::Checkbox(L("Defeated only"), &m_bestiaryDefeatedOnly);

    std::string needle;
    for (const char *p = m_bestiarySearch; *p; ++p)
        needle.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    const auto matches = [&](const std::string &haystack) {
        if (needle.empty())
            return true;
        std::string lower;
        lower.reserve(haystack.size());
        for (char c : haystack)
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return lower.find(needle) != std::string::npos;
    };

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                       ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp;
    const float iconSize = std::floor(em(1.3f));
    const float lineH = ImGui::GetTextLineHeight();
    const float cellRowH = std::max(lineH, iconSize) + em(0.22f);
    const float colGap = em(0.9f);
    const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);

    // Draws `text` inside `[pos.x, pos.x + maxW]`, scrolling smoothly back and
    // forth if the text is wider than `maxW`.
    const auto drawMarquee = [&](ImDrawList *dl, ImVec2 pos, float maxW, ImU32 col, const char *text) {
        if (maxW <= 1.0f)
            return;
        const float textW = ImGui::CalcTextSize(text).x;
        if (textW <= maxW) {
            dl->AddText(pos, col, text);
            return;
        }
        const float overflow = textW - maxW;
        const float speed = std::max(16.0f, em(2.2f));
        const double pause = 1.2;
        const double travel = static_cast<double>(overflow) / static_cast<double>(speed);
        const double cycle = 2.0 * (pause + travel);
        const double phase = std::fmod(t, cycle);
        float offset = 0.0f;
        if (phase < pause)
            offset = 0.0f;
        else if (phase < pause + travel)
            offset = static_cast<float>((phase - pause) * speed);
        else if (phase < 2.0 * pause + travel)
            offset = overflow;
        else
            offset = overflow - static_cast<float>((phase - 2.0 * pause - travel) * speed);
        dl->PushClipRect(ImVec2(pos.x, pos.y - 1.0f), ImVec2(pos.x + maxW, pos.y + lineH + 2.0f), true);
        dl->AddText(ImVec2(pos.x - std::floor(offset), pos.y), col, text);
        dl->PopClipRect();
    };

    const auto drawIconAt = [&](ImDrawList *dl, int iconId, ImVec2 pos, float px) -> bool {
        if (!m_bestiaryAtlasReady || iconId < 0 || m_bestiaryAtlasSize.x <= 0.0f || m_bestiaryAtlasSize.y <= 0.0f)
            return false;
        const int group = iconId / 1000;
        const int page = (iconId % 1000) / 100;
        int idx = (iconId % 100) - 1;
        if (idx < 0 || idx >= 65)
            idx = 0;
        int sheetX = 0, sheetY = 0;
        if (group == 1) {
            const int p = (page >= 1 && page < 10) ? (page - 1) : 0;
            sheetX = (p & 3) * 256;
            sheetY = ((p >> 2) + 1) * 256;
        } else if (group == 2) {
            const int p = (page >= 1 && page < 6) ? (page - 1) : 0;
            sheetX = p * 256;
            sheetY = 0;
        } else if (group == 3) {
            const int p = (page >= 1 && page < 6) ? (page - 1) : 0;
            sheetX = p * 256;
            sheetY = 768;
        } else {
            return false;
        }
        const int cell = (iconId == 0x8FD) ? 40 : 32;
        const int u = sheetX + (idx & 7) * cell;
        const int v = sheetY + ((idx >> 3) & 7) * cell;
        if (u + cell > static_cast<int>(m_bestiaryAtlasSize.x) || v + cell > static_cast<int>(m_bestiaryAtlasSize.y))
            return false;
        const ImVec2 uv0(static_cast<float>(u) / m_bestiaryAtlasSize.x, static_cast<float>(v) / m_bestiaryAtlasSize.y);
        const ImVec2 uv1(static_cast<float>(u + cell) / m_bestiaryAtlasSize.x,
                         static_cast<float>(v + cell) / m_bestiaryAtlasSize.y);
        dl->AddImage(m_bestiaryAtlasTex, pos, ImVec2(pos.x + px, pos.y + px), uv0, uv1);
        return true;
    };

    int monsterRendersThisFrame = 0;
    const auto drawMonsterIconAt = [&](ImDrawList *dl, const std::string &modelName, ImVec2 pos, float px) -> bool {
        if (modelName.empty() || !m_host.bestiaryMonsterIcon)
            return false;
        const ImVec2 maxPos(pos.x + px, pos.y + px);
        if (!ImGui::IsRectVisible(pos, maxPos))
            return true; // reserve space without rendering off-screen models
        auto it = m_bestiaryMonsterIcons.find(modelName);
        if (it == m_bestiaryMonsterIcons.end()) {
            if (monsterRendersThisFrame >= 4)
                return true;
            ++monsterRendersThisFrame;
            MonsterIconCache entry;
            const BestiaryAtlas atlas = m_host.bestiaryMonsterIcon(modelName);
            if (atlas.width > 0 && atlas.height > 0 && !atlas.rgba.empty()) {
                ImVec2 sz{};
                const std::string texKey = "bestiary_mon_" + modelName;
                entry.ready =
                    rgbaTexture(texKey.c_str(), atlas.width, atlas.height, atlas.rgba.data(), entry.tex, sz);
            }
            it = m_bestiaryMonsterIcons.emplace(modelName, entry).first;
        }
        if (it->second.ready) {
            dl->AddImage(it->second.tex, pos, maxPos);
            return true;
        }
        return false;
    };

    if (m_bestiaryTab == 0) {
        size_t defeatedCount = 0;
        for (const BestiaryMonster &m : m_bestiaryCache.monsters)
            if (m.defeated > 0)
                ++defeatedCount;
        mutedText(L("%zu monsters (%zu defeated)"), m_bestiaryCache.monsters.size(), defeatedCount);
        ImGui::Dummy(ImVec2(0.0f, em(0.2f)));

        size_t shown = 0;
        if (ImGui::BeginTable("##bestiary-monsters", 2, tableFlags)) {
            ImGui::TableSetupColumn(L("Monster"), ImGuiTableColumnFlags_WidthStretch, 0.32f);
            ImGui::TableSetupColumn(L("Possible drops"), ImGuiTableColumnFlags_WidthStretch, 0.68f);
            ImGui::PushStyleColor(ImGuiCol_Text, muted);
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            for (size_t i = 0; i < m_bestiaryCache.monsters.size(); ++i) {
                const BestiaryMonster &monster = m_bestiaryCache.monsters[i];
                if (m_bestiaryDefeatedOnly && monster.defeated <= 0)
                    continue;
                bool ok = matches(monster.name);
                if (!ok) {
                    for (const BestiaryDrop &drop : monster.drops) {
                        if (matches(drop.itemName)) {
                            ok = true;
                            break;
                        }
                    }
                }
                if (!ok)
                    continue;
                ++shown;
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                // Focusable span so controller D-pad/stick scrolls the table smoothly.
                ImGui::SetNextItemAllowOverlap();
                ImGui::Selectable("##mon", false, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0.0f, 0.0f));
                ImGui::SameLine(0.0f, 0.0f);
                ImDrawList *dl = ImGui::GetWindowDrawList();
                const float monIcon = std::floor(em(1.45f));
                const ImVec2 monOrigin = ImGui::GetCursorScreenPos();
                const float monAvailW = ImGui::GetContentRegionAvail().x;
                float monX = monOrigin.x;
                if (drawMonsterIconAt(dl, monster.modelName, ImVec2(monX, monOrigin.y), monIcon)) {
                    if (monster.defeated > 0)
                        dl->AddCircleFilled(ImVec2(monX + monIcon - em(0.15f), monOrigin.y + monIcon - em(0.15f)),
                                            em(0.22f), palette::kGold);
                    monX += monIcon + em(0.45f);
                } else {
                    const float badgeR = em(0.26f);
                    if (monster.defeated > 0)
                        dl->AddCircleFilled(ImVec2(monX + badgeR, monOrigin.y + monIcon * 0.5f), badgeR,
                                            palette::kGold);
                    else
                        dl->AddCircle(ImVec2(monX + badgeR, monOrigin.y + monIcon * 0.5f), badgeR, palette::kTextMuted,
                                      0, 1.3f);
                    monX += em(0.9f);
                }
                const float monW = std::max(0.0f, monOrigin.x + monAvailW - monX);
                const float monTextY = monOrigin.y + std::floor((monIcon - lineH) * 0.5f);
                if (monster.defeated > 0) {
                    char countBuf[24];
                    std::snprintf(countBuf, sizeof(countBuf), "%s%d", kTimes, monster.defeated);
                    const float countW = ImGui::CalcTextSize(countBuf).x;
                    const float nameW = std::max(0.0f, monW - countW - em(0.35f));
                    drawMarquee(dl, ImVec2(monX, monTextY), nameW, palette::kGold, monster.name.c_str());
                    dl->AddText(ImVec2(monX + std::min(ImGui::CalcTextSize(monster.name.c_str()).x, nameW) + em(0.35f),
                                       monTextY),
                                palette::kTextMuted, countBuf);
                } else {
                    drawMarquee(dl, ImVec2(monX, monTextY), monW, textCol, monster.name.c_str());
                }
                ImGui::Dummy(ImVec2(monAvailW, std::max(lineH, monIcon)));

                ImGui::TableNextColumn();
                if (monster.drops.empty()) {
                    ImGui::TextColored(muted, "\xE2\x80\x94");
                } else {
                    const float availW = ImGui::GetContentRegionAvail().x;
                    const float cellW = std::floor(std::max(em(6.0f), (availW - colGap) * 0.5f));
                    const size_t rows = (monster.drops.size() + 1u) / 2u;
                    const ImVec2 origin = ImGui::GetCursorScreenPos();
                    const float iconPad = em(0.35f);
                    const float pctPad = em(0.35f);
                    for (size_t d = 0; d < monster.drops.size(); ++d) {
                        const BestiaryDrop &drop = monster.drops[d];
                        const size_t r = d / 2u;
                        const size_t c = d % 2u;
                        const float x = origin.x + static_cast<float>(c) * (cellW + colGap);
                        const float y = origin.y + static_cast<float>(r) * cellRowH;
                        float cursorX = x;
                        if (drawIconAt(dl, drop.iconId, ImVec2(cursorX, y + std::floor((cellRowH - iconSize) * 0.5f)),
                                       iconSize))
                            cursorX += iconSize + iconPad;
                        char pct[16];
                        std::snprintf(pct, sizeof(pct), "%d%%", drop.ratePercent);
                        const float pctW = ImGui::CalcTextSize(pct).x;
                        const float textY = y + std::floor((cellRowH - lineH) * 0.5f);
                        const float nameW = std::max(0.0f, (x + cellW) - cursorX - pctW - pctPad);
                        drawMarquee(dl, ImVec2(cursorX, textY), nameW, textCol, drop.itemName.c_str());
                        dl->AddText(ImVec2(x + cellW - pctW, textY), palette::kTextMuted, pct);
                    }
                    ImGui::Dummy(ImVec2(availW, static_cast<float>(rows) * cellRowH));
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (shown == 0) {
            ImGui::Dummy(ImVec2(0.0f, em(0.3f)));
            mutedText("%s", L("No matching entries."));
        }
    } else {
        mutedText(L("%zu items"), m_bestiaryCache.items.size());
        ImGui::Dummy(ImVec2(0.0f, em(0.2f)));

        size_t shown = 0;
        if (ImGui::BeginTable("##bestiary-items", 2, tableFlags)) {
            ImGui::TableSetupColumn(L("Item"), ImGuiTableColumnFlags_WidthStretch, 0.32f);
            ImGui::TableSetupColumn(L("Dropped by"), ImGuiTableColumnFlags_WidthStretch, 0.68f);
            ImGui::PushStyleColor(ImGuiCol_Text, muted);
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            std::vector<const BestiaryItemSource *> visibleSources;
            for (size_t i = 0; i < m_bestiaryCache.items.size(); ++i) {
                const BestiaryItem &item = m_bestiaryCache.items[i];
                visibleSources.clear();
                bool nameMatch = matches(item.name);
                bool sourceMatch = false;
                for (const BestiaryItemSource &src : item.sources) {
                    if (m_bestiaryDefeatedOnly && src.monsterDefeated <= 0)
                        continue;
                    visibleSources.push_back(&src);
                    if (matches(src.monsterName))
                        sourceMatch = true;
                }
                if (visibleSources.empty() || (!nameMatch && !sourceMatch))
                    continue;
                ++shown;
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::SetNextItemAllowOverlap();
                ImGui::Selectable("##item", false, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0.0f, 0.0f));
                ImGui::SameLine(0.0f, 0.0f);
                ImDrawList *dl = ImGui::GetWindowDrawList();
                const float itemIcon = std::floor(em(1.45f));
                const ImVec2 itemOrigin = ImGui::GetCursorScreenPos();
                const float itemAvailW = ImGui::GetContentRegionAvail().x;
                float itemX = itemOrigin.x;
                if (drawIconAt(dl, item.iconId, ImVec2(itemX, itemOrigin.y), itemIcon))
                    itemX += itemIcon + em(0.45f);
                drawMarquee(dl, ImVec2(itemX, itemOrigin.y + std::floor((itemIcon - lineH) * 0.5f)),
                            std::max(0.0f, itemOrigin.x + itemAvailW - itemX), textCol, item.name.c_str());
                ImGui::Dummy(ImVec2(itemAvailW, std::max(lineH, itemIcon)));

                ImGui::TableNextColumn();
                const float availW = ImGui::GetContentRegionAvail().x;
                const float cellW = std::floor(std::max(em(6.0f), (availW - colGap) * 0.5f));
                const size_t rows = (visibleSources.size() + 1u) / 2u;
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                const float pctPad = em(0.35f);
                const float iconPad = em(0.35f);
                for (size_t s = 0; s < visibleSources.size(); ++s) {
                    const BestiaryItemSource &src = *visibleSources[s];
                    const size_t r = s / 2u;
                    const size_t c = s % 2u;
                    const float x = origin.x + static_cast<float>(c) * (cellW + colGap);
                    const float y = origin.y + static_cast<float>(r) * cellRowH;
                    float cursorX = x;
                    if (drawMonsterIconAt(dl, src.modelName,
                                          ImVec2(cursorX, y + std::floor((cellRowH - iconSize) * 0.5f)), iconSize))
                        cursorX += iconSize + iconPad;
                    char pct[16];
                    std::snprintf(pct, sizeof(pct), "%d%%", src.ratePercent);
                    const float pctW = ImGui::CalcTextSize(pct).x;
                    const float textY = y + std::floor((cellRowH - lineH) * 0.5f);
                    const float nameW = std::max(0.0f, (x + cellW) - cursorX - pctW - pctPad);
                    drawMarquee(dl, ImVec2(cursorX, textY), nameW, src.monsterDefeated > 0 ? palette::kGold : textCol,
                                src.monsterName.c_str());
                    dl->AddText(ImVec2(x + cellW - pctW, textY), palette::kTextMuted, pct);
                }
                ImGui::Dummy(ImVec2(availW, static_cast<float>(rows) * cellRowH));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (shown == 0) {
            ImGui::Dummy(ImVec2(0.0f, em(0.3f)));
            mutedText("%s", L("No matching entries."));
        }
    }
}

void Overlay::drawStats() {
    const double time = now();
    if (m_statsAt < 0.0 || time - m_statsAt > 0.5) {
        m_statsPrevious = m_stats;
        m_stats = m_backend.stats();
        m_statsSpan = m_statsAt < 0.0 ? 0.0 : time - m_statsAt;
        m_statsAt = time;
    }
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + em(0.8f), viewport->WorkPos.y + em(0.8f)), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing;
    if (!m_menuOpen)
        flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em(1.0f), em(0.8f)));
    if (beginWindow("##stats", nullptr, flags)) {
        dialogHeader(Icon::Chart, "Renderer statistics", m_menuOpen ? &m_statsOpen : nullptr);
        const double span = m_statsSpan > 0.0 ? m_statsSpan : 1.0;
        const auto rate = [&](uint64_t now, uint64_t before) {
            return m_statsSpan > 0.0 && now >= before ? static_cast<double>(now - before) / span : 0.0;
        };
        if (ImGui::BeginTable("##stats", 2, ImGuiTableFlags_None)) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, em(11.0f));
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, em(8.0f));
            const auto row = [&](const char *name, const char *format, double value) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                mutedText("%s", name);
                ImGui::TableNextColumn();
                ImGui::Text(format, value);
            };
            row("Frames shown", "%.0f /s", rate(m_stats.presents, m_statsPrevious.presents));
            row("Draw calls", "%.0f /s", rate(m_stats.drawCalls, m_statsPrevious.drawCalls));
            row("Triangles", "%.1f K/s", rate(m_stats.trianglesDrawn, m_statsPrevious.trianglesDrawn) / 1000.0);
            row("Render passes", "%.0f /s", rate(m_stats.renderPasses, m_statsPrevious.renderPasses));
            row("Texture cache hits", "%.1f %%", m_stats.textureHitRate() * 100.0);
            row("Textures built", "%.0f /s", rate(m_stats.textureBuilds, m_statsPrevious.textureBuilds));
            row("Target resolves", "%.0f /s", rate(m_stats.colorResolves, m_statsPrevious.colorResolves));
            row("Transfers", "%.0f KiB/s", rate(m_stats.transferBytes, m_statsPrevious.transferBytes) / 1024.0);
            row("Pipelines", "%.0f", static_cast<double>(m_stats.pipelinesCreated));
            ImGui::EndTable();
        }
        sectionHeader("Approximations");
        if (ImGui::BeginTable("##approximations", 2, ImGuiTableFlags_None)) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, em(11.0f));
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, em(8.0f));
            const auto row = [&](const char *name, uint64_t value) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                mutedText("%s", name);
                ImGui::TableNextColumn();
                ImGui::TextColored(color(value ? palette::kWarn : palette::kGood), "%llu",
                                   static_cast<unsigned long long>(value));
            };
            row("Inexact blends", m_stats.inexactBlends);
            row("Saturated factors", m_stats.saturatedBlendFactors);
            row("Destination alpha", m_stats.destinationAlphaFactors);
            row("Partial masks", m_stats.partialChannelMasks);
            row("Alpha fail modes", m_stats.alphaFailModes);
            ImGui::EndTable();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace dq8::ui
