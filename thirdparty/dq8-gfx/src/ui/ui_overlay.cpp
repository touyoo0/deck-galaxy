#include "ui/ui_overlay.h"

#include "gfx/backends/sdlgpu/sdlgpu_input.h"
#include "ui/ui_art.h"
#include "ui/ui_style.h"
#include "ui/ui_text.h"
#include "ui/ui_widgets.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace dq8::ui {
namespace {
constexpr double kCaptureSeconds = 6.0;

// Function keys the menu owns; with the game's own (HostServices::
// gameShortcuts: Rogue Galaxy's F6, the free camera), never bound: binding
// one would fight the shortcut.
constexpr SDL_Scancode kMenuKeys[] = {SDL_SCANCODE_F1, SDL_SCANCODE_F4, SDL_SCANCODE_F5,
                                      SDL_SCANCODE_F12};

bool reservedKey(SDL_Scancode scancode, const std::vector<HostServices::Shortcut> &game) {
    return std::find(std::begin(kMenuKeys), std::end(kMenuKeys), scancode) != std::end(kMenuKeys) ||
           std::any_of(game.begin(), game.end(),
                       [&](const HostServices::Shortcut &shortcut) { return shortcut.key == scancode; });
}

// The reserved keys as the binding prompt names them, runs of function keys
// shortened: "F1 to F4, F6 and F12".
std::string reservedKeysText(const std::vector<HostServices::Shortcut> &game) {
    std::vector<int> functions;
    std::vector<std::string> others;
    const auto add = [&](SDL_Scancode key) {
        if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12)
            functions.push_back(static_cast<int>(key - SDL_SCANCODE_F1) + 1);
        else
            others.push_back(SDL_GetScancodeName(key));
    };
    for (const SDL_Scancode key : kMenuKeys)
        add(key);
    for (const HostServices::Shortcut &shortcut : game)
        add(shortcut.key);
    std::sort(functions.begin(), functions.end());
    functions.erase(std::unique(functions.begin(), functions.end()), functions.end());
    std::vector<std::string> parts;
    for (size_t first = 0; first < functions.size();) {
        size_t last = first;
        while (last + 1 < functions.size() && functions[last + 1] == functions[last] + 1)
            ++last;
        if (last - first >= 2) {
            parts.push_back(Lf("F%d to F%d", functions[first], functions[last]));
        } else {
            for (size_t index = first; index <= last; ++index)
                parts.push_back("F" + std::to_string(functions[index]));
        }
        first = last + 1;
    }
    parts.insert(parts.end(), others.begin(), others.end());
    std::string text;
    for (size_t index = 0; index < parts.size(); ++index)
        text += (index == 0 ? "" : index + 1 == parts.size() ? L(" and ") : ", ") + parts[index];
    return text;
}

std::string timestamp() {
    const std::time_t time = std::time(nullptr);
    char text[32] = {};
    std::strftime(text, sizeof(text), "%Y-%m-%d_%H-%M-%S", std::localtime(&time));
    return text;
}

float easeOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
} // namespace

Overlay::Overlay(gfx::SdlGpuBackend &backend, Settings settings, std::string settingsPath, HostServices host)
    : m_backend(backend), m_settings(std::move(settings)), m_settingsPath(std::move(settingsPath)),
      m_host(std::move(host)) {}

Overlay::~Overlay() {
    for (auto &saver : m_savers)
        saver.thread.join();
    saveIfDirty(true);
    if (!m_context)
        return;
    ImGui::SetCurrentContext(m_context);
    if (m_rendererReady)
        ImGui_ImplSDLGPU3_Shutdown();
    if (m_platformReady)
        ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(m_context);
}

double Overlay::now() const {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

bool Overlay::initialize(SDL_Window *window, SDL_GPUTextureFormat format, std::string &error) {
    m_window = window;
    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_context);
    ImGuiIO &io = ImGui::GetIO();
    // Settings are ours; ImGui keeps no files.
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    // Back never leaves the cursor nowhere: a controller has no mouse to
    // find it again with.
    io.ConfigNavEscapeClearFocusItem = false;
    // Holding X would bring up ImGui's window switcher.
    m_context->ConfigNavWindowingWithGamepad = false;
    loadFonts();
    if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
        error = "Dear ImGui could not attach to the window";
        return false;
    }
    m_platformReady = true;
    // No controllers for the backend: feedGamepad() reads them instead.
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
    ImGui_ImplSDLGPU3_InitInfo info;
    info.Device = m_backend.gpuDevice();
    info.ColorTargetFormat = format;
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&info)) {
        error = "Dear ImGui could not create its GPU resources";
        return false;
    }
    m_rendererReady = true;
    setMenuLanguage(m_settings.menuLanguage);
    updateScale();
    loadScript();
    // No button combination opens the menu: on the Deck the Steam Input layout
    // puts it on L4, elsewhere F1 or the Guide button.
    if (m_host.steamDeck)
        notify("Press L4 for the menu", Icon::Info, 6.0);
    else
        notify("Press F1, or Guide on a controller, for the menu", Icon::Info, 6.0);
    return true;
}

void Overlay::applySettings() {
    setMenuLanguage(m_settings.menuLanguage);
    m_backend.setDisplayOptions(m_settings.display);
    m_backend.padInput().setConfig(m_settings.pad);
    if (m_window && m_window == m_backend.window() && m_settings.fullscreen)
        SDL_SetWindowFullscreen(m_window, true);
    applyVolume();
    applyPause();
    for (const GameOption &option : m_host.gameOptions) {
        if (option.number()) {
            if (m_host.setGameNumber)
                m_host.setGameNumber(option.key, gameNumber(option));
        } else if (m_host.setGameOption && option.remembered) {
            m_host.setGameOption(option.key, gameOption(option));
        }
        if (!m_startCaptured && !option.number())
            m_startOptions[option.key] = gameOption(option);
        if (!m_startCaptured && option.number())
            m_startNumbers[option.key] = gameNumber(option);
    }
    m_startCaptured = true;
}

bool Overlay::waitsForRestart(const GameOption &option) const {
    if (option.number()) {
        const auto found = m_startNumbers.find(option.key);
        return option.restart && found != m_startNumbers.end() && found->second != gameNumber(option);
    }
    const auto found = m_startOptions.find(option.key);
    return option.restart && found != m_startOptions.end() && found->second != gameOption(option);
}

bool Overlay::gameOption(const GameOption &option) const {
    if (!option.remembered)
        return m_host.gameOptionState ? m_host.gameOptionState(option.key) : option.defaultValue;
    const auto found = m_settings.gameOptions.find(option.key);
    return found != m_settings.gameOptions.end() ? found->second : option.defaultValue;
}

void Overlay::setGameOptionValue(const std::string &key, bool on) {
    for (const GameOption &option : m_host.gameOptions) {
        if (option.key != key || option.number())
            continue;
        if (option.remembered) {
            m_settings.gameOptions[key] = on;
            m_settings.gameNumbers.erase(key);
            // Saved now: the menu, closed, draws no frame to save it in.
            markDirty();
            saveIfDirty(true);
        }
        if (m_host.setGameOption)
            m_host.setGameOption(key, on);
    }
}

void Overlay::setGameNumberValue(const std::string &key, int value) {
    for (const GameOption &option : m_host.gameOptions) {
        if (option.key != key || !option.number())
            continue;
        value = std::clamp(value, option.minimum, option.maximum);
        m_settings.gameNumbers[key] = value;
        m_settings.gameOptions.erase(key);
        markDirty();
        saveIfDirty(true);
        if (m_host.setGameNumber)
            m_host.setGameNumber(key, value);
    }
}

int Overlay::gameNumber(const GameOption &option) const {
    const auto found = m_settings.gameNumbers.find(option.key);
    const int value = found != m_settings.gameNumbers.end() ? found->second : option.defaultNumber;
    return std::clamp(value, option.minimum, option.maximum);
}

bool Overlay::capturesInput() const { return m_menuOpen || m_capture.active || m_homeOpen || m_slotMode != 0; }

bool Overlay::wantsFrames() {
    if (!m_context)
        return false;
    // Queued input has to be consumed by a frame, or it replays later.
    const bool scriptWaiting = std::any_of(m_script.begin(), m_script.end(),
                                           [](const ScriptStep &step) { return !step.done && step.seconds >= 0.0; });
    return m_menuOpen || m_homeOpen || m_slotMode != 0 || m_capture.active || !m_toasts.empty() || m_statsOpen ||
           m_iconSheet || pausing() || scriptWaiting || !m_context->InputEventsQueue.empty();
}

void Overlay::setMenuOpen(bool open) {
    if (m_menuOpen == open)
        return;
    m_menuOpen = open;
    if (!open) {
        m_settingsOpen = m_aboutOpen = m_shortcutsOpen = false;
        m_capture.active = false;
        m_closePopups = true;
    }
    applyPause();
}

void Overlay::openSettings(SettingsPage page) {
    setMenuOpen(true);
    m_settingsOpen = true;
    m_focusSettings = true;
    m_focusList = true;
    m_page = page;
}

void Overlay::openAbout() {
    setMenuOpen(true);
    m_aboutOpen = true;
}

void Overlay::openShortcuts() {
    setMenuOpen(true);
    m_shortcutsOpen = true;
}

void Overlay::openHome() {
    if (!canPause())
        return;
    m_homeOpen = true;
    m_autoState = m_host.autoSlot && m_host.autoSlot().used;
    m_homeNavReady = false;
    m_toasts.clear(); // the menu's hint does not apply here
    // The button that started the game is still down: it must not press Play.
    m_gamepadNavHeld = true;
    applyPause();
}

void Overlay::closeHome() {
    if (!m_homeOpen)
        return;
    m_homeOpen = false;
    m_gamepadNavHeld = true;
    applyPause();
}

void Overlay::startLaunch(bool resume) {
    if (!m_homeOpen || m_launchAt >= 0.0)
        return;
    m_resumeOnLaunch = resume && m_autoState && m_host.continueGame;
    m_launchAt = ImGui::GetTime();
}

void Overlay::openSaveSlots(bool save) {
    if (m_homeOpen || !m_host.saveSlots)
        return;
    const uint8_t mode = save ? 1u : 2u;
    if (m_slotMode == mode) {
        closeSaveSlots();
        return;
    }
    setMenuOpen(false);
    m_slotMode = mode;
    m_slotConfirm = 0;
    m_slotFocus = true;
    // The buttons of the combination that opened it are still down.
    m_gamepadNavHeld = true;
    applyPause();
}

void Overlay::closeSaveSlots() {
    if (m_slotMode == 0)
        return;
    m_slotMode = 0;
    m_slotConfirm = 0;
    // The button that chose a slot must not reach the game as it resumes.
    m_gamepadNavHeld = true;
    m_backend.padInput().suppressHeld();
    applyPause();
}

void Overlay::toggleMenu(bool fromController) {
    if (m_homeOpen)
        return;
    if (m_menuOpen) {
        setMenuOpen(false);
        return;
    }
    // The settings window is the whole menu: the mouse and the keyboard
    // open it as the controller does. Only a controller needs the button
    // that opened it let go before it moves the cursor.
    setMenuOpen(true);
    m_settingsOpen = true;
    m_focusSettings = true;
    m_focusList = true;
    if (fromController)
        m_gamepadNavHeld = true;
}

void Overlay::notify(std::string text, Icon kind, double seconds, std::string picture) {
    if (!m_settings.notifications)
        return;
    text = L(text);
    for (Toast &toast : m_toasts)
        if (toast.text == text) {
            toast.startedAt = std::max(toast.startedAt, now() - 0.2);
            toast.duration = seconds;
            return;
        }
    if (m_toasts.size() >= 4u)
        m_toasts.erase(m_toasts.begin());
    m_toasts.push_back({std::move(text), kind, now(), seconds, std::move(picture)});
}

void Overlay::beginBindingCapture(gfx::PadInput input, bool keyboard, int slot) {
    m_capture = {true, input, keyboard, slot, now()};
}

void Overlay::setUserPaused(bool paused) {
    if (!canPause())
        return;
    m_userPaused = paused;
    applyPause();
}

std::string Overlay::speedLabel(float speed) const {
    if (speed <= 0.0f)
        return L("Unlimited");
    char text[16];
    std::snprintf(text, sizeof(text), "%g\xC3\x97", static_cast<double>(speed));
    return text;
}

void Overlay::setFastForward(bool on) {
    if (!canChangeSpeed() || m_fastForward == on)
        return;
    m_fastForward = on;
    m_host.setSpeed(on ? m_settings.fastForwardSpeed : 1.0);
    notify(on ? Lf("Fast-forward: %s", speedLabel(m_settings.fastForwardSpeed).c_str()) : std::string("Normal speed"),
           on ? Icon::Speed : Icon::Play, 1.5);
}

bool Overlay::handleEvent(const SDL_Event &event) {
    if (!m_context)
        return false;
    ImGui::SetCurrentContext(m_context);
    if (m_capture.active && captureEvent(event))
        return true;
    if (m_captureMouseDown != 0u) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == m_captureMouseDown) {
            m_captureMouseDown = 0u;
            return true;
        }
        // Its release went elsewhere (the window lost the focus): nothing
        // to keep from the menu any more.
        if ((SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(m_captureMouseDown)) == 0u)
            m_captureMouseDown = 0u;
    }
    switch (event.type) {
    case SDL_EVENT_GAMEPAD_ADDED: {
        const char *name = SDL_GetGamepadNameForID(event.gdevice.which);
        m_controllerNames.emplace_back(event.gdevice.which, name ? name : "Controller");
        break;
    }
    case SDL_EVENT_GAMEPAD_REMOVED: {
        const auto it = std::find_if(m_controllerNames.begin(), m_controllerNames.end(),
                                     [&](const auto &entry) { return entry.first == event.gdevice.which; });
        if (it != m_controllerNames.end()) {
            notify(Lf("Disconnected: %s", it->second.c_str()), Icon::Warning);
            m_controllerNames.erase(it);
        }
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        m_windowFocused = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        // The Steam menu or another window takes the front: the state Continue
        // will load is taken now, while nothing is lost by it (Steam Deck fork: added).
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && !m_homeOpen && canPause() && m_host.autoSave)
            m_host.autoSave();
        applyVolume();
        applyPause();
        break;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
        if (m_window && event.window.windowID == SDL_GetWindowID(m_window))
            rememberWindow(event.window);
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: {
        const bool fullscreen = event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        if (m_settings.fullscreen != fullscreen) {
            m_settings.fullscreen = fullscreen;
            markDirty();
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        m_lastMouseMotion = now();
        m_lastInputPad = false;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        m_lastInputPad = false;
        break;
    // Back (Esc, B) reaches ImGui like any key, and drawFrame() takes it
    // from there, knowing where the cursor was.
    case SDL_EVENT_KEY_DOWN:
        m_lastInputPad = false;
        if (!event.key.repeat && hotkey(event.key))
            return true;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        m_lastInputPad = true;
        if (gamepadShortcut(event.gbutton))
            return true;
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (std::abs(static_cast<int>(event.gaxis.value)) > 16000)
            m_lastInputPad = true;
        break;
    default:
        break;
    }
    ImGui_ImplSDL3_ProcessEvent(&event);
    return false;
}

bool Overlay::hotkey(const SDL_KeyboardEvent &key) {
    switch (key.scancode) {
    case SDL_SCANCODE_F1:
        toggleMenu(false);
        return true;
    // F5 pauses: F2 and F3 are the save states' save and load (Rogue Galaxy: moved).
    case SDL_SCANCODE_F5:
        if (!canPause())
            return false;
        setUserPaused(!m_userPaused);
        return true;
    case SDL_SCANCODE_F4:
        if (!canChangeSpeed())
            return false;
        setFastForward(!m_fastForward);
        return true;
    case SDL_SCANCODE_F12:
        takeScreenshot(false);
        return true;
    default:
        return false;
    }
}

bool Overlay::gamepadShortcut(const SDL_GamepadButtonEvent &event) {
    const auto button = static_cast<SDL_GamepadButton>(event.button);
    if (button == SDL_GAMEPAD_BUTTON_GUIDE) {
        toggleMenu(true);
        return true;
    }
    return false;
}

bool Overlay::captureEvent(const SDL_Event &event) {
    const auto finish = [&](gfx::PadBinding binding) {
        gfx::PadConfig config = m_settings.pad;
        config.slots(m_capture.input, m_capture.keyboard)[m_capture.slot] = binding;
        setPadConfig(config);
        m_capture.active = false;
        m_gamepadNavHeld = true;
    };
    if (m_capture.keyboard) {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            if (reservedKey(event.key.scancode, m_host.gameShortcuts)) {
                notify(Lf("%s is kept for a shortcut", SDL_GetScancodeName(event.key.scancode)),
                       Icon::Warning);
                return true;
            }
            finish(gfx::PadBinding::key(event.key.scancode));
            return true;
        }
        // A mouse button (the five the pad reads), outside the prompt (its
        // own buttons take theirs), not a touch; or a whole notch of the
        // wheel, which only presses a button (a stick or Walk is held).
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !m_capturePromptHovered &&
            event.button.which != SDL_TOUCH_MOUSEID && event.button.button >= SDL_BUTTON_LEFT &&
            event.button.button <= SDL_BUTTON_X2) {
            m_captureMouseDown = event.button.button;
            finish(gfx::PadBinding::mouse(event.button.button));
            return true;
        }
        if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.integer_y != 0) {
            if (gfx::padButtonMask(m_capture.input) == 0u) {
                notify("The wheel can only press a button", Icon::Warning);
                return true;
            }
            const bool flipped = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED;
            finish(gfx::PadBinding::wheel((event.wheel.integer_y > 0) != flipped ? 1 : -1));
            return true;
        }
        return event.type == SDL_EVENT_KEY_UP;
    }
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
            m_capture.active = false;
        return true;
    case SDL_EVENT_KEY_UP:
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        // A tap of B binds it; a hold cancels (drawCapturePrompt()).
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST && m_capture.backDownAt >= 0.0) {
            m_capture.backDownAt = -1.0;
            finish(gfx::PadBinding::button(SDL_GAMEPAD_BUTTON_EAST));
        }
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)
            m_capture.backDownAt = now();
        else if (event.gbutton.button != SDL_GAMEPAD_BUTTON_GUIDE)
            finish(gfx::PadBinding::button(static_cast<SDL_GamepadButton>(event.gbutton.button)));
        return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const auto axis = static_cast<SDL_GamepadAxis>(event.gaxis.axis);
        const bool trigger = axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        // A deliberate push, well past drift and resting triggers.
        if (std::abs(static_cast<int>(event.gaxis.value)) > (trigger ? 16000 : 22000))
            finish(gfx::PadBinding::axis(axis, event.gaxis.value < 0 ? -1 : 1));
        return true;
    }
    default:
        return false;
    }
}

namespace {
bool inside(const ImGuiWindow *window, const ImGuiWindow *pane) {
    for (; window && pane; window = window->ParentWindow)
        if (window == pane)
            return true;
    return false;
}
} // namespace

// ImGui has already done its part of Back by now (ended a slider's
// adjustment, closed a combo or a menu, ); this is
// the rest, judged by where the cursor was before it.
void Overlay::backOut(bool /*pad*/) {
    const NavBefore &before = m_navBefore;
    if (before.itemActive)
        return;
    if (before.modalOpen) {
        m_cancelQuit = true; // the only modal Back reaches: the capture prompt takes its own input
        return;
    }
    if (before.popupOpen)
        return;
    if (m_aboutOpen) {
        m_aboutOpen = false;
        return;
    }
    if (m_shortcutsOpen) {
        m_shortcutsOpen = false;
        return;
    }
    if (m_settingsOpen && inside(before.window, m_pagePane)) {
        m_focusList = true;
        return;
    }
    // From the page list: back to the game.
    setMenuOpen(false);
}

void Overlay::rememberNavigation() {
    const ImGuiContext &g = *m_context;
    m_navBefore.window = g.NavWindow;
    m_navBefore.itemActive = g.ActiveId != 0;
    m_navBefore.popupOpen = !g.OpenPopupStack.empty();
    m_navBefore.modalOpen = false;
    for (const ImGuiPopupData &popup : g.OpenPopupStack)
        if (popup.Window && (popup.Window->Flags & ImGuiWindowFlags_Modal))
            m_navBefore.modalOpen = true;

    // Past a slider and a list, Back is backOut()'s alone.
    // ImGui's own, inside a child window, puts the cursor on the child as a
    // whole for a frame (a bright frame around the page or the list) before
    // backOut() moves it on. Owned keys still read as pressed with
    // IsKeyPressed(key, repeat), which asks for any owner.
    if (m_menuOpen && !m_capture.active && !m_navBefore.itemActive && !m_navBefore.popupOpen) {
        static const ImGuiID kBackOwner = ImHashStr("dq8 back");
        ImGui::SetKeyOwner(ImGuiKey_Escape, kBackOwner);
        ImGui::SetKeyOwner(ImGuiKey_GamepadFaceRight, kBackOwner);
    }
}

// The same buttons as ImGui's SDL backend gives it, from every controller,
// with the left stick pressing the D-pad (past half way, let go under a
// third) and the right stick in the left stick's place, which ImGui scrolls
// with.
void Overlay::feedGamepad() {
    ImGuiIO &io = ImGui::GetIO();
    std::vector<SDL_Gamepad *> gamepads;
    for (const auto &controller : m_backend.padInput().controllers())
        if (SDL_Gamepad *gamepad = SDL_GetGamepadFromID(controller.id))
            gamepads.push_back(gamepad);
    if (gamepads.empty()) {
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
        m_stickHeld = {};
        return;
    }
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    const auto button = [&](SDL_GamepadButton which) {
        return std::any_of(gamepads.begin(), gamepads.end(),
                           [&](SDL_Gamepad *gamepad) { return SDL_GetGamepadButton(gamepad, which); });
    };
    const auto axis = [&](SDL_GamepadAxis which) {
        // The one pushed furthest.
        float value = 0.0f;
        for (SDL_Gamepad *gamepad : gamepads) {
            const float v = static_cast<float>(SDL_GetGamepadAxis(gamepad, which)) / 32767.0f;
            if (std::fabs(v) > std::fabs(value))
                value = v;
        }
        return std::clamp(value, -1.0f, 1.0f);
    };
    static constexpr std::pair<ImGuiKey, SDL_GamepadButton> kButtons[] = {
        {ImGuiKey_GamepadStart, SDL_GAMEPAD_BUTTON_START},
        {ImGuiKey_GamepadBack, SDL_GAMEPAD_BUTTON_BACK},
        {ImGuiKey_GamepadFaceLeft, SDL_GAMEPAD_BUTTON_WEST},
        {ImGuiKey_GamepadFaceRight, SDL_GAMEPAD_BUTTON_EAST},
        {ImGuiKey_GamepadFaceUp, SDL_GAMEPAD_BUTTON_NORTH},
        {ImGuiKey_GamepadFaceDown, SDL_GAMEPAD_BUTTON_SOUTH},
        {ImGuiKey_GamepadL1, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
        {ImGuiKey_GamepadR1, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
        {ImGuiKey_GamepadL3, SDL_GAMEPAD_BUTTON_LEFT_STICK},
        {ImGuiKey_GamepadR3, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    };
    for (const auto &[key, which] : kButtons)
        io.AddKeyEvent(key, button(which));

    // The left stick by its larger axis, so a diagonal push moves one way.
    const float x = axis(SDL_GAMEPAD_AXIS_LEFTX), y = axis(SDL_GAMEPAD_AXIS_LEFTY);
    const bool horizontal = std::fabs(x) >= std::fabs(y);
    const float push[4] = {horizontal ? -x : 0.0f, horizontal ? x : 0.0f, horizontal ? 0.0f : -y,
                           horizontal ? 0.0f : y};
    for (size_t direction = 0; direction < 4; ++direction)
        m_stickHeld[direction] = push[direction] > (m_stickHeld[direction] ? 0.33f : 0.5f);
    static constexpr std::pair<ImGuiKey, SDL_GamepadButton> kDpad[] = {
        {ImGuiKey_GamepadDpadLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
        {ImGuiKey_GamepadDpadRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
        {ImGuiKey_GamepadDpadUp, SDL_GAMEPAD_BUTTON_DPAD_UP},
        {ImGuiKey_GamepadDpadDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    };
    for (size_t direction = 0; direction < 4; ++direction)
        io.AddKeyEvent(kDpad[direction].first, button(kDpad[direction].second) || m_stickHeld[direction]);

    const auto analog = [&](ImGuiKey key, float value) {
        constexpr float kDeadZone = 0.25f;
        const float past = std::max(0.0f, value - kDeadZone) / (1.0f - kDeadZone);
        io.AddKeyAnalogEvent(key, past > 0.0f, past);
    };
    analog(ImGuiKey_GamepadL2, axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    analog(ImGuiKey_GamepadR2, axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    const float rx = axis(SDL_GAMEPAD_AXIS_RIGHTX), ry = axis(SDL_GAMEPAD_AXIS_RIGHTY);
    analog(ImGuiKey_GamepadLStickLeft, -rx);
    analog(ImGuiKey_GamepadLStickRight, rx);
    analog(ImGuiKey_GamepadLStickUp, -ry);
    analog(ImGuiKey_GamepadLStickDown, ry);
}

std::string Overlay::padButtonName(SDL_GamepadButton button) const {
    return bindingLabel(gfx::PadBinding::button(button), displayedControllerType());
}

void Overlay::render(SDL_GPUCommandBuffer *commands, SDL_GPUTexture *target, uint32_t width, uint32_t height) {
    if (!m_rendererReady)
        return;
    m_targetWidth = width;
    m_targetHeight = height;
    ImGui::SetCurrentContext(m_context);
    ++m_presents;
    runScript();
    updateScale();
    updateNavigation();
    rememberNavigation();
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    feedGamepad();
    ImGui::NewFrame();
    drawFrame();
    ImGui::Render();
    ImDrawData *data = ImGui::GetDrawData();
    // Always: this is also where the font atlas reaches the GPU.
    ImGui_ImplSDLGPU3_PrepareDrawData(data, commands);
    if (data->TotalVtxCount == 0)
        return;
    SDL_GPUColorTargetInfo info{};
    info.texture = target;
    info.load_op = SDL_GPU_LOADOP_LOAD;
    info.store_op = SDL_GPU_STOREOP_STORE;
    if (SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &info, 1u, nullptr)) {
        ImGui_ImplSDLGPU3_RenderDrawData(data, commands, pass);
        SDL_EndGPURenderPass(pass);
    }
}

void Overlay::updateScale() {
    float display = 1.0f;
    if (m_window) {
        // A scaled desktop shows up as the display scale.
        const float density = SDL_GetWindowPixelDensity(m_window);
        const float scale = SDL_GetWindowDisplayScale(m_window);
        if (density > 0.0f && scale > 0.0f)
            display = scale / density;
    }
    const float scale = std::clamp(display * static_cast<float>(m_settings.uiScale) / 100.0f, 0.5f, 4.0f);
    if (std::fabs(scale - m_appliedScale) < 0.001f)
        return;
    applyStyle(scale);
    m_appliedScale = scale;
}

void Overlay::updateNavigation() {
    ImGuiIO &io = ImGui::GetIO();
    if (m_gamepadNavHeld) {
        // The button that opened the menu or ended a capture is still down;
        // navigation waits for its release so it does not activate anything.
        bool down = false;
        for (const auto &controller : m_backend.padInput().controllers())
            if (SDL_Gamepad *gamepad = SDL_GetGamepadFromID(controller.id))
                for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT && !down; ++button)
                    down = SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(button));
        m_gamepadNavHeld = down;
    }
    io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
    if ((m_menuOpen || m_homeOpen || m_slotMode != 0) && !m_capture.active) {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        if (!m_gamepadNavHeld)
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    }
}

void Overlay::drawFrame() {
    if (m_closePopups) {
        // All of them: a Quit question left open would stay on ImGui's
        // popup stack and take Back's next press.
        if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
            ImGui::ClosePopupToLevel(0, false);
        m_closePopups = false;
    }
    const ImGuiIO &io = ImGui::GetIO();
    if (m_menuOpen && !m_capture.active) {
        const bool padBack = (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) &&
                             ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
        const bool keyBack = (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) &&
                             ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (padBack || keyBack)
            backOut(padBack);
    }
    collectSavedScreenshots();
    sampleFrameRate();
    if (m_pendingScale != 0u) {
        // A new scale is applied as a frame is prepared, and a paused game
        // prepares none, so the wait only counts while it runs.
        if (pausing())
            m_pendingScaleSince = now();
        if (m_backend.activeResolutionScale() == m_pendingScale) {
            m_pendingScale = 0u;
        } else if (now() - m_pendingScaleSince > 3.0) {
            notify("The internal resolution could not be changed", Icon::Warning);
            m_pendingScale = 0u;
        }
    }
    // The settings window is the menu: closing it closes the menu with it.
    if (m_menuOpen && !m_settingsOpen && !m_aboutOpen && !m_shortcutsOpen)
        setMenuOpen(false);
    if (m_homeOpen)
        drawHome();
    drawLoadingVeil();
    if (m_settingsOpen)
        drawSettingsWindow();
    if (m_aboutOpen)
        drawAbout();
    if (m_shortcutsOpen)
        drawShortcuts();
    if (m_statsOpen)
        drawStats();
    drawQuitPrompt();
    if (m_slotMode != 0)
        drawSaveSlots();
    drawCapturePrompt();
    m_toastTop = 0.0f;
    drawSpeedBadge();
    drawPauseBanner();
    drawToasts();
    drawIconSheet();
    updateCursor();
    saveIfDirty(false);
}

// Quit, from the page list, asked first: a stray press would
// lose what was not saved. B or Esc answers no.
void Overlay::drawQuitPrompt() {
    constexpr const char *kName = "Quit?##quit"; // the id; the title is not drawn
    if (m_quitAsk) {
        ImGui::OpenPopup(kName);
        m_quitAsk = false;
    }
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em(1.6f), em(1.2f)));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal(kName, nullptr, flags)) {
        decorateWindow();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
        ImGui::TextUnformatted(L("Quit Rogue Galaxy?"));
        ImGui::PopFont();
        mutedText("Progress since your last save is lost.");
        ImGui::Dummy(ImVec2(0.0f, em(0.6f)));
        // Cancel first: it is where the cursor starts.
        const ImVec2 buttonSize(em(7.0f), 0.0f);
        if (ImGui::Button(L("Cancel"), buttonSize) || m_cancelQuit)
            ImGui::CloseCurrentPopup();
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button(L("Quit"), buttonSize)) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    m_cancelQuit = false;
    ImGui::PopStyleVar();
}

namespace {
// The home screen's pieces (Steam Deck fork). All of them draw on the
// window's own list, so a hidden window or a missing artwork costs nothing.
float smooth(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const auto lerp = [&](int shift) {
        const float x = static_cast<float>((a >> shift) & 0xFFu);
        const float y = static_cast<float>((b >> shift) & 0xFFu);
        return static_cast<ImU32>(x + (y - x) * t) << shift;
    };
    return lerp(IM_COL32_R_SHIFT) | lerp(IM_COL32_G_SHIFT) | lerp(IM_COL32_B_SHIFT) | lerp(IM_COL32_A_SHIFT);
}

// What the update says it is doing, in the menu's language: update.sh and the
// host write English, some with a version or a count after it.
std::string updateText(const std::string &text) {
    static const struct {
        const char *prefix;
        const char *format;
    } kWithValue[] = {{"Downloading ", "Downloading %s"}, {"Building ", "Building %s"}, {"Updated to ", "Updated to %s"}};
    for (const auto &entry : kWithValue)
        if (text.rfind(entry.prefix, 0) == 0 && text.size() > std::strlen(entry.prefix))
            return Lf(entry.format, text.c_str() + std::strlen(entry.prefix));
    return L(text);
}

// Text with tracking and a soft shadow; `size` in pixels. Returns its width.
float trackedText(ImDrawList *list, ImVec2 at, float size, float tracking, ImU32 from, ImU32 to, const char *text,
                  float fade) {
    ImFont *font = ImGui::GetFont();
    // One letter at a time, a letter being a whole UTF-8 sequence (accents).
    const auto lengthAt = [&](size_t offset) {
        const unsigned char lead = static_cast<unsigned char>(text[offset]);
        const size_t length = lead >= 0xF0 ? 4u : lead >= 0xE0 ? 3u : lead >= 0xC0 ? 2u : 1u;
        return std::min(length, std::strlen(text + offset));
    };
    size_t count = 0;
    for (size_t offset = 0; text[offset] != '\0'; offset += lengthAt(offset))
        ++count;
    float x = at.x;
    size_t byte = 0;
    for (size_t i = 0; i < count; ++i) {
        char glyph[5] = {};
        const size_t length = lengthAt(byte);
        std::memcpy(glyph, text + byte, length);
        byte += length;
        const float t = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f;
        const float advance = font->CalcTextSizeA(size, FLT_MAX, 0.0f, glyph).x;
        list->AddText(font, size, ImVec2(x, at.y), withAlpha(mix(from, to, t), fade), glyph);
        x += advance + tracking;
    }
    return x - at.x - tracking;
}

// A spinning arc, for work in progress.
void spinner(ImDrawList *list, ImVec2 centre, float radius, ImU32 color) {
    const float t = static_cast<float>(ImGui::GetTime());
    list->PathClear();
    list->PathArcTo(centre, radius, t * 5.0f, t * 5.0f + 4.2f, 24);
    list->PathStroke(color, std::max(2.0f, radius * 0.28f));
}

// True while the news panel has the d-pad (reading mode): it owns the arrows
// and A, so ImGui's navigation leaves the column's buttons where they are, and
// the buttons show no selection; the update button, when there is one, shows
// it instead.
bool gHomeReading = false;
// Which button the mouse is over, this frame and the last: with the mouse at
// rest over one and the focus on another, only one may light up.
bool gHoverNow = false, gHoverPrev = false;
// A pointer resting where the window opened selects nothing: hovering counts
// once the mouse has moved.
bool gMouseMoved = false;
ImVec2 gMouseStart(-1.0f, -1.0f);

// One entry of the home screen: plain text at rest; under the cursor a white
// pill lifts behind it and the text turns dark. `filled` keeps a faint plate
// at rest, for an action that stands alone (the update).
bool homeButton(const char *id, const char *label, ImVec2 size, bool enabled, float fade, int adornment,
                bool filled = false, bool selected = false) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::Button(id, size);
    ImGui::PopStyleColor(4);
    const bool hovered = ImGui::IsItemHovered() && gMouseMoved;
    gHoverNow = gHoverNow || hovered;
    // The controller or keyboard moved last: the focus is the selection. The
    // mouse moved last: what is under it, else the focus.
    const bool navLast = ImGui::GetCurrentContext()->NavCursorVisible;
    const bool lit = enabled && (selected || (!gHomeReading && (navLast ? ImGui::IsItemFocused()
                                                                        : (hovered || (ImGui::IsItemFocused() && !gHoverPrev)))));
    const bool held = enabled && ImGui::IsItemActive();
    const ImVec2 max(min.x + size.x, min.y + size.y);

    float &glow = *ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID(id), 0.0f);
    glow += ((lit ? 1.0f : 0.0f) - glow) * std::min(1.0f, ImGui::GetIO().DeltaTime * 16.0f);
    const float dim = (enabled ? 1.0f : 0.45f) * fade;
    const float round = size.y * 0.5f;
    ImDrawList *list = ImGui::GetWindowDrawList();

    if (filled)
        list->AddRectFilled(min, max, withAlpha(IM_COL32(255, 255, 255, 22), dim * (1.0f - glow)), round);
    if (glow > 0.02f) {
        drawShadow(list, min, max, round, glow * 0.6f * fade);
        list->AddRectFilled(min, max, withAlpha(palette::kPlate, glow * dim), round);
        if (held)
            list->AddRectFilled(min, max, IM_COL32(0, 0, 0, 28), round);
    }

    ImFont *font = ImGui::GetFont();
    const float textSize = em(1.12f);
    const ImVec2 measured = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, label);
    const ImVec2 at(min.x + em(1.5f) + glow * em(0.25f), std::floor(min.y + (size.y - measured.y) * 0.5f));
    const ImU32 ink = mix(IM_COL32(226, 230, 240, 255), palette::kInk, glow);
    list->AddText(font, textSize, at, withAlpha(ink, dim), label);

    // Right side: a chevron under the cursor, a spinner while work goes on,
    // or a small accent dot when something waits.
    const ImVec2 mark(max.x - em(1.5f), min.y + size.y * 0.5f);
    if (glow > 0.05f && enabled && adornment == 0) {
        const float e = em(0.32f);
        list->PathLineTo(ImVec2(mark.x - e * 0.5f, mark.y - e));
        list->PathLineTo(ImVec2(mark.x + e * 0.5f, mark.y));
        list->PathLineTo(ImVec2(mark.x - e * 0.5f, mark.y + e));
        list->PathStroke(withAlpha(palette::kInk, glow * dim), 2.0f);
    }
    if (adornment == 1) {
        spinner(list, mark, em(0.4f), withAlpha(mix(palette::kSky, palette::kInk, glow), dim));
    } else if (adornment == 2) {
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.0f);
        list->AddCircleFilled(mark, em(0.26f) + pulse * em(0.22f), withAlpha(palette::kGold, 0.16f * dim));
        list->AddCircleFilled(mark, em(0.24f), withAlpha(palette::kGold, dim));
    }
    return pressed;
}

// The changelog on the home screen (Steam Deck fork: added): the embedded
// CHANGELOG.md cut into versions, sections and bullets, the last few versions
// only, without the install notes and known issues.
struct ChangeLine {
    enum Kind { Version, Section, Bullet } kind;
    std::string text;
    std::string date; // Version only: the release's date, from "## X.Y.Z - DATE"
};

std::string plainMarkdown(const std::string &in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '`' || in[i] == '*')
            continue;
        if (in[i] == '[') {
            const size_t close = in.find("](", i);
            const size_t end = close == std::string::npos ? close : in.find(')', close);
            if (end != std::string::npos) {
                out += in.substr(i + 1, close - i - 1);
                i = end;
                continue;
            }
        }
        out += in[i];
    }
    return out;
}

// "2026-11-18" -> "18 nov. 2026" (in the menu's language); anything else is
// shown as written.
std::string shortDate(const std::string &iso) {
    static const char *const kMonths[5][12] = {
        {"jan.", "feb.", "mar.", "apr.", "may", "jun.", "jul.", "aug.", "sep.", "oct.", "nov.", "dec."},
        {"janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août", "sept.", "oct.", "nov.", "déc."},
        {"Jan.", "Feb.", "März", "Apr.", "Mai", "Juni", "Juli", "Aug.", "Sept.", "Okt.", "Nov.", "Dez."},
        {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sept", "oct", "nov", "dic"},
        {"gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic"}};
    int y = 0, m = 0, d = 0;
    char extra = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%d%c", &y, &m, &d, &extra) != 3 || m < 1 || m > 12 || d < 1 || d > 31)
        return iso;
    const int language = menuLanguageIndex();
    return std::to_string(d) + (language == 2 ? ". " : " ") + kMonths[language][m - 1] + " " + std::to_string(y);
}

// One version of the notes: its name ("0.9.0", "Unreleased") and its lines.
struct ChangeBlock {
    std::string key;
    std::vector<ChangeLine> lines;
};

// The first `maxVersions` versions of a CHANGELOG.md, without the install
// notes and known issues.
std::vector<ChangeBlock> parseChangelog(const std::string &text, int maxVersions) {
    std::vector<ChangeBlock> blocks;
    int versions = 0;
    bool skip = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("## ", 0) == 0) {
            if (++versions > maxVersions)
                break;
            skip = false;
            std::string head = plainMarkdown(line.substr(3));
            std::string date;
            const size_t dash = head.find(" - ");
            if (dash != std::string::npos) {
                date = head.substr(dash + 3);
                head.resize(dash);
            }
            const size_t paren = head.find(" (");
            if (paren != std::string::npos)
                head.resize(paren);
            blocks.push_back({head, {{ChangeLine::Version, head, shortDate(date)}}});
        } else if (blocks.empty()) {
            continue;
        } else if (line.rfind("### ", 0) == 0) {
            const std::string name = line.substr(4);
            skip = name.rfind("Install", 0) == 0 || name.rfind("Update from", 0) == 0 ||
                   name.rfind("Known issues", 0) == 0 || name.rfind("Highlights", 0) == 0;
            if (!skip)
                blocks.back().lines.push_back({ChangeLine::Section, name});
        } else if (!skip) {
            std::vector<ChangeLine> &lines = blocks.back().lines;
            if (line.rfind("- ", 0) == 0)
                lines.push_back({ChangeLine::Bullet, plainMarkdown(line.substr(2))});
            else if (line.rfind("  ", 0) == 0 && lines.back().kind == ChangeLine::Bullet) {
                size_t first = line.find_first_not_of(' ');
                if (first != std::string::npos)
                    lines.back().text += " " + plainMarkdown(line.substr(first));
            }
        }
    }
    return blocks;
}

// The last versions' notes in the menu's language: the English ones, each
// version replaced by its translation (docs/changelog/CHANGELOG.<code>.md)
// when that file has it.
const std::vector<ChangeLine> &changelogLines() {
    static std::vector<ChangeLine> lines;
    static int built = -1;
    const int language = menuLanguageIndex();
    if (built == language)
        return lines;
    built = language;
    lines.clear();
    unsigned int size = 0;
    const char *data = homeChangelog(size);
    if (size <= 1u)
        return lines;
    constexpr int kVersions = 3;
    std::vector<ChangeBlock> translated;
    if (language != 0) {
        const char *other = homeChangelogIn(language, size);
        if (size > 1u)
            translated = parseChangelog(std::string(other, size), 2 * kVersions);
    }
    data = homeChangelog(size);
    for (ChangeBlock &block : parseChangelog(std::string(data, size), kVersions)) {
        const auto match = std::find_if(translated.begin(), translated.end(),
                                        [&](const ChangeBlock &candidate) { return candidate.key == block.key; });
        const ChangeBlock &chosen = match != translated.end() ? *match : block;
        lines.insert(lines.end(), chosen.lines.begin(), chosen.lines.end());
    }
    return lines;
}
} // namespace

namespace {
// How long the home screen takes to leave once Play is pressed.
constexpr double kLaunchSeconds = 1.2;

float hash01(uint32_t n) {
    n = n * 747796405u + 2891336453u;
    n = ((n >> ((n >> 28u) + 4u)) ^ n) * 277803737u;
    n = (n >> 22u) ^ n;
    return static_cast<float>(n) / 4294967295.0f;
}

// Stars streaming out of `centre`, each on its own ray, faster as they near
// the edge; `streak` stretches them into lines (0 is dots), `alpha` fades all.
// The motion follows the clock alone, so the screens that hand over to each
// other show the same stars.
void drawWarp(ImDrawList *list, ImVec2 centre, ImVec2 view, float t, float streak, float alpha) {
    if (alpha <= 0.01f)
        return;
    const float reach = 0.5f * std::sqrt(view.x * view.x + view.y * view.y) * 1.05f;
    constexpr int kStars = 170;
    for (int i = 0; i < kStars; ++i) {
        const uint32_t seed = static_cast<uint32_t>(i) * 4u;
        const float angle = hash01(seed + 1u) * 6.2831853f;
        const float pace = 0.35f + 0.65f * hash01(seed + 2u);
        const float phase = std::fmod(t * 0.33f * pace + hash01(seed + 3u), 1.0f);
        const float r = phase * phase * reach;
        const float r0 = std::max(0.0f, r - streak * phase * reach * 0.22f);
        const ImVec2 dir(std::cos(angle), std::sin(angle));
        const float a = alpha * smooth(phase * 3.0f) * (0.30f + 0.70f * pace);
        const ImU32 tone = i % 5 == 0 ? IM_COL32(244, 203, 82, 255) : IM_COL32(200, 220, 255, 255);
        list->AddLine(ImVec2(centre.x + dir.x * r0, centre.y + dir.y * r0), ImVec2(centre.x + dir.x * r, centre.y + dir.y * r),
                      withAlpha(tone, a), em(0.08f) * (0.6f + 1.8f * phase));
    }
}
} // namespace

// The screen the game starts on: Play, Update, Options, Quit. It covers the window
// with the game's artwork, slowly moving and lit, and keeps the game paused
// until Play. Everything is reached with the controller (A selects).
void Overlay::drawHome() {
    static bool logged = false;
    static double openedAt = -1.0;
    if (!logged) {
        logged = true;
        std::fprintf(stderr, "[ui] home screen drawn\n");
    }
    gHoverPrev = gHoverNow;
    gHoverNow = false;
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (gMouseStart.x < 0.0f)
            gMouseStart = mouse;
        else if (std::fabs(mouse.x - gMouseStart.x) + std::fabs(mouse.y - gMouseStart.y) > 4.0f)
            gMouseMoved = true;
    }
    const double time = ImGui::GetTime();
    if (openedAt < 0.0)
        openedAt = time;
    const float intro = smooth(static_cast<float>((time - openedAt) / 0.9));
    // Play pressed: the interface leaves at once, the picture dives into the
    // warp (p goes 0 to 1 over kLaunchSeconds).
    const bool launching = m_launchAt >= 0.0;
    const float p = launching ? smooth(static_cast<float>((time - m_launchAt) / kLaunchSeconds)) : 0.0f;
    const float fade =
        intro * (launching ? 1.0f - smooth(static_cast<float>((time - m_launchAt) / 0.45)) : 1.0f);
    const float t = static_cast<float>(time);

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 view = viewport->Size;
    const ImVec2 corner(origin.x + view.x, origin.y + view.y);
    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(view);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(8, 10, 16, 255));
    ImGui::Begin("##homebg", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    {
        ImDrawList *list = ImGui::GetWindowDrawList();
        // Plain dark, when there is no artwork, or beneath its edges.
        list->AddRectFilled(origin, corner, IM_COL32(8, 10, 16, 255));
        ImVec2 image(origin.x, origin.y), imageSize = view;
        ImTextureRef art;
        ImVec2 artPixels;
        if (homeArt(HomePicture::Background, art, artPixels)) {
            // Covers the window, breathing in and out and drifting a little,
            // the picture shown wider than the window so no edge ever shows.
            const float cover = std::max(view.x / artPixels.x, view.y / artPixels.y);
            const float zoom = 1.03f + 0.02f * std::sin(t * 0.11f) + 0.05f * (1.0f - intro);
            const float scale = cover * zoom * (1.0f + 0.55f * p * p);
            imageSize = ImVec2(artPixels.x * scale, artPixels.y * scale);
            const float spareX = imageSize.x - view.x, spareY = imageSize.y - view.y;
            image = ImVec2(origin.x - spareX * (0.5f + 0.45f * std::sin(t * 0.07f)),
                           origin.y - spareY * (0.5f + 0.38f * std::sin(t * 0.05f + 1.0f)));
            list->AddImage(art, image, ImVec2(image.x + imageSize.x, image.y + imageSize.y), ImVec2(0, 0), ImVec2(1, 1),
                           IM_COL32(255, 255, 255, 255));
        }

        // Legibility: a soft dark veil from the left and from below, nothing
        // else on the picture.
        const ImU32 shade = IM_COL32(5, 7, 12, 0);
        list->AddRectFilled(origin, corner, IM_COL32(5, 7, 12, 46));
        list->AddRectFilledMultiColor(origin, ImVec2(origin.x + view.x * 0.62f, corner.y), IM_COL32(5, 7, 12, 232),
                                      shade, shade, IM_COL32(5, 7, 12, 232));
        list->AddRectFilledMultiColor(ImVec2(origin.x, corner.y - view.y * 0.35f), corner, shade, shade,
                                      IM_COL32(5, 7, 12, 190), IM_COL32(5, 7, 12, 190));

        // The foot of the screen: the edition and the version, quietly.
        const float left = origin.x + view.x * 0.08f;
        const float small = em(0.78f);
        const ImU32 faint = IM_COL32(139, 147, 166, 255);
        const float editionWidth = trackedText(list, ImVec2(left, corner.y - view.y * 0.06f - small), small, em(0.16f),
                                               faint, faint, L("STEAM DECK EDITION"), fade);
        trackedText(list, ImVec2(left + editionWidth + em(1.0f), corner.y - view.y * 0.06f - small), small, em(0.05f),
                    faint, faint, m_host.version.c_str(), fade * 0.8f);
        // The first moments: up from black.
        list->AddRectFilled(origin, corner, IM_COL32(0, 0, 0, static_cast<int>(255 * (1.0f - intro))));
        if (launching) {
            // The picture sinks into the loading screen's night, a light
            // swelling at the centre as the stars start streaming.
            const ImVec2 centre(origin.x + view.x * 0.5f, origin.y + view.y * 0.5f);
            list->AddRectFilled(origin, corner,
                                IM_COL32(6, 8, 14, static_cast<int>(255 * smooth((p - 0.2f) / 0.8f))));
            drawWarp(list, centre, view, t, 0.3f + 1.0f * p, smooth(p / 0.45f));
            const float swell = smooth(p / 0.7f) * (1.0f - smooth((p - 0.7f) / 0.3f));
            for (int ring = 0; ring < 4; ++ring)
                list->AddCircleFilled(centre, view.y * (0.10f + 0.09f * static_cast<float>(ring)) * (0.6f + 0.6f * p),
                                      withAlpha(IM_COL32(150, 180, 255, 255), 0.10f * swell), 48);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    const HostServices::HomeUpdate update = m_host.homeUpdate ? m_host.homeUpdate() : HostServices::HomeUpdate{};
    using State = HostServices::HomeUpdate::State;
    const bool working = update.state == State::Working;
    // Nothing else can be pressed once Play was.
    const bool busy = working || launching;

    // The update's button, as the news panel and (without a panel) the column
    // draw it.
    std::string label = L("Update");
    if (update.state == State::Available)
        label = Lf("Update to %s", update.version.c_str());
    else if (update.state == State::UpToDate)
        label = L("Up to date");
    else if (update.state == State::Checking)
        label = L("Checking for updates...");
    else if (update.state == State::Offline)
        label = L("Cannot check for updates");
    else if (working)
        label = L("Updating...");
    else if (update.state == State::Failed)
        label = L("Update failed: try again");
    else if (update.state == State::Done)
        label = L("Restart to finish");
    const bool canUpdate = static_cast<bool>(m_host.startUpdate) &&
                           (update.state == State::Available || update.state == State::Failed);
    const int adornment = (update.state == State::Checking || working) ? 1 : update.state == State::Available ? 2 : 0;
    // The progress bar and what the update says, at the cursor.
    const auto updateStatus = [&](ImDrawList *list, float width) {
        if (!(working || update.state == State::Failed || update.state == State::Done))
            return;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (working) {
            const float bar = em(0.28f);
            const ImVec2 end(at.x + width, at.y + bar);
            list->AddRectFilled(at, end, IM_COL32(255, 255, 255, 26), bar * 0.5f);
            list->PushClipRect(at, end, true);
            if (update.progress < 0.0f) {
                const float run = std::fmod(t * 0.8f, 1.4f) - 0.2f;
                const float from = at.x + width * run;
                list->AddRectFilledMultiColor(ImVec2(from, at.y), ImVec2(from + width * 0.3f, end.y), IM_COL32(244, 203, 82, 0),
                                              palette::kGold, palette::kGold, IM_COL32(244, 203, 82, 0));
            } else {
                const float done = std::clamp(update.progress, 0.0f, 1.0f);
                list->AddRectFilled(at, ImVec2(at.x + width * done, end.y), palette::kGold, bar * 0.5f);
            }
            list->PopClipRect();
            ImGui::Dummy(ImVec2(width, bar));
        }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::PushStyleColor(ImGuiCol_Text, update.state == State::Failed ? palette::kBad : palette::kTextMuted);
        ImGui::TextUnformatted(updateText(update.text).c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    };

    // What changed: the last versions' notes, on the right. The wheel or the
    // right stick scrolls; it takes no focus, so the buttons keep the pad.
    const std::vector<ChangeLine> &changes = changelogLines();
    // Stays visible behind the settings; it cannot be read then.
    if (m_settingsOpen)
        gHomeReading = false;
    // With the panel, the update lives in it; without (a narrow window, no
    // notes), under the buttons.
    const bool inPanel = !changes.empty() && view.x >= view.y * 1.3f;
    if (inPanel) {
        const float panelW = std::min(view.x * 0.31f, em(26.0f));
        const float panelH = view.y * 0.70f;
        ImGui::SetNextWindowPos(ImVec2(corner.x - view.x * 0.06f, origin.y + view.y * 0.5f), ImGuiCond_Always,
                                ImVec2(1.0f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em(1.4f), em(1.2f)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, em(1.1f));
        // Its text leaves with the rest once Play is pressed (the window's own
        // alpha reaches the text and the update button; `fade` only its plate).
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade / std::max(intro, 0.001f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(10, 12, 18, static_cast<int>(176 * fade)));
        if (ImGui::Begin("##homenews", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoBringToFrontOnFocus)) {
            ImDrawList *list = ImGui::GetWindowDrawList();
            const ImVec2 wmin = ImGui::GetWindowPos();
            if (!m_settingsOpen) // behind the settings window
                ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            const ImGuiIO &io = ImGui::GetIO();
            const bool navOn = (io.ConfigFlags & (ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard)) != 0;
            const auto pressed = [](ImGuiKey pad, ImGuiKey key, bool repeat) {
                return ImGui::IsKeyPressed(pad, repeat) || ImGui::IsKeyPressed(key, repeat);
            };
            // Right from the buttons enters reading; Left or B leaves. Owning
            // the keys takes effect the next frame.
            bool &reading = gHomeReading;
            const ImGuiID readId = ImGui::GetID("##newsread");
            if (!navOn || m_settingsOpen) {
                reading = false;
            } else if (!reading) {
                reading = pressed(ImGuiKey_GamepadDpadRight, ImGuiKey_RightArrow, false);
            } else if (pressed(ImGuiKey_GamepadDpadLeft, ImGuiKey_LeftArrow, false) ||
                       ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                reading = false;
            }
            // A (or Enter) while reading starts an available update.
            if (reading && canUpdate &&
                (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
                m_host.startUpdate();
            if (reading) {
                for (const ImGuiKey key : {ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadDpadLeft,
                                           ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadFaceDown, ImGuiKey_UpArrow,
                                           ImGuiKey_DownArrow, ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Enter,
                                           ImGuiKey_Space, ImGuiKey_KeypadEnter})
                    ImGui::SetKeyOwner(key, readId);
            }
            list->AddRect(wmin, ImVec2(wmin.x + panelW, wmin.y + panelH),
                          reading ? withAlpha(palette::kText, 0.85f * fade) : withAlpha(IM_COL32(255, 255, 255, 30), fade),
                          em(1.1f), reading ? 2.0f : 1.0f);
            const float titleSize = em(0.8f);
            trackedText(list, ImGui::GetCursorScreenPos(), titleSize, em(0.18f), palette::kTextMuted, palette::kTextMuted,
                        L("WHAT'S NEW"), fade);
            // A gold dot beside the title while an update waits.
            if (update.state == State::Available) {
                const float pulse = 0.5f + 0.5f * std::sin(t * 3.0f);
                const ImVec2 dot(wmin.x + panelW - em(1.7f), wmin.y + em(1.2f) + titleSize * 0.5f);
                list->AddCircleFilled(dot, em(0.5f), withAlpha(palette::kGold, (0.10f + 0.16f * pulse) * fade), 16);
                list->AddCircleFilled(dot, em(0.24f), withAlpha(palette::kGold, fade), 12);
            }
            ImGui::Dummy(ImVec2(0.0f, titleSize * 2.2f));

            // The update: a button when there is something to do, a green
            // card when all is well, a quiet card otherwise.
            {
                const float width = ImGui::GetContentRegionAvail().x;
                const bool button = update.state == State::Available || update.state == State::Failed || working ||
                                    update.state == State::Done;
                if (button) {
                    if (update.state == State::Available) {
                        ImGui::TextUnformatted(Lf("Version %s is available", update.version.c_str()).c_str());
                        ImGui::Dummy(ImVec2(0.0f, em(0.15f)));
                    }
                    ImGui::BeginDisabled(!canUpdate);
                    if (homeButton("##update", label.c_str(), ImVec2(width, em(2.5f)), canUpdate, fade, adornment, true,
                                   reading && canUpdate) &&
                        canUpdate)
                        m_host.startUpdate();
                    ImGui::EndDisabled();
                    updateStatus(list, width);
                    ImGui::Dummy(ImVec2(0.0f, em(0.5f)));
                } else {
                    // A container: green for up to date, grey for the rest.
                    const bool good = update.state == State::UpToDate;
                    const ImU32 tone = good ? palette::kGood : palette::kTextMuted;
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    const float height = em(2.5f);
                    const ImVec2 end(at.x + width, at.y + height);
                    list->AddRectFilled(at, end, withAlpha(tone, (good ? 0.14f : 0.10f) * fade), height * 0.5f);
                    list->AddRect(at, end, withAlpha(tone, (good ? 0.38f : 0.20f) * fade), height * 0.5f, 1.0f);
                    const ImVec2 mark(at.x + em(1.45f), at.y + height * 0.5f);
                    if (good) {
                        list->AddCircleFilled(mark, em(0.62f), withAlpha(tone, 0.22f * fade), 20);
                        drawIcon(list, Icon::Check, mark, em(0.9f), withAlpha(tone, fade));
                    } else if (update.state == State::Checking) {
                        spinner(list, mark, em(0.36f), withAlpha(tone, fade));
                    } else {
                        drawIcon(list, Icon::Warning, mark, em(0.9f), withAlpha(tone, fade));
                    }
                    const ImVec2 text = ImGui::CalcTextSize(label.c_str());
                    list->AddText(ImVec2(at.x + em(2.55f), std::floor(at.y + (height - text.y) * 0.5f)),
                                  withAlpha(good ? palette::kText : palette::kTextMuted, fade), label.c_str());
                    ImGui::Dummy(ImVec2(width, height + em(0.5f)));
                }
            }
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
            if (ImGui::BeginChild("##homenewsbody", ImVec2(0.0f, -em(0.8f)), ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground)) {
                const float stick = ImGui::GetKeyData(ImGuiKey_GamepadLStickDown)->AnalogValue -
                                    ImGui::GetKeyData(ImGuiKey_GamepadLStickUp)->AnalogValue;
                if (stick != 0.0f)
                    ImGui::SetScrollY(ImGui::GetScrollY() + stick * ImGui::GetIO().DeltaTime * em(30.0f));
                {
                    // L1 / R1 a page at any time; the d-pad a line or two while
                    // the update button has the focus.
                    float step = 0.0f;
                    if (reading && pressed(ImGuiKey_GamepadDpadDown, ImGuiKey_DownArrow, true))
                        step += em(2.2f);
                    if (reading && pressed(ImGuiKey_GamepadDpadUp, ImGuiKey_UpArrow, true))
                        step -= em(2.2f);
                    if (pressed(ImGuiKey_GamepadL1, ImGuiKey_PageUp, true))
                        step -= ImGui::GetWindowHeight() * 0.85f;
                    if (pressed(ImGuiKey_GamepadR1, ImGuiKey_PageDown, true))
                        step += ImGui::GetWindowHeight() * 0.85f;
                    if (step != 0.0f)
                        ImGui::SetScrollY(ImGui::GetScrollY() + step);
                }
                ImGui::PushTextWrapPos(0.0f);
                bool first = true;
                for (const ChangeLine &line : changes) {
                    if (line.kind == ChangeLine::Version) {
                        ImGui::Dummy(ImVec2(0.0f, first ? 0.0f : em(0.9f)));
                        first = false;
                        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.18f);
                        ImGui::TextUnformatted(L(line.text.c_str()));
                        ImGui::PopFont();
                        if (!line.date.empty()) {
                            ImGui::PushStyleColor(ImGuiCol_Text, palette::kTextMuted);
                            ImGui::TextUnformatted(line.date.c_str());
                            ImGui::PopStyleColor();
                        }
                    } else if (line.kind == ChangeLine::Section) {
                        ImGui::Dummy(ImVec2(0.0f, em(0.35f)));
                        ImGui::PushStyleColor(ImGuiCol_Text, palette::kGold);
                        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.84f);
                        ImGui::TextUnformatted(L(line.text.c_str()));
                        ImGui::PopFont();
                        ImGui::PopStyleColor();
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(190, 196, 212, 255));
                        ImGui::Bullet();
                        ImGui::TextUnformatted(line.text.c_str());
                        ImGui::PopStyleColor();
                        ImGui::Dummy(ImVec2(0.0f, em(0.15f)));
                    }
                }
                ImGui::PopTextWrapPos();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    // The column: title, buttons, update status.
    const float column = std::min(em(17.0f), view.x * 0.42f);
    // Padding all round, so a button's shadow is not cut by the window.
    const float pad = em(1.2f);
    ImGui::SetNextWindowPos(ImVec2(origin.x + view.x * 0.08f - pad, origin.y + view.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.0f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(column + pad * 2.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, em(0.35f)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                                   ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##home", nullptr, flags)) {
        // Without this the first A does nothing: the cursor stays hidden
        // until a direction key shows it. Play is the default focus.
        if (!m_homeNavReady && !m_settingsOpen &&
            (ImGui::GetIO().ConfigFlags & (ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard))) {
            ImGui::SetWindowFocus();
            ImGui::SetNavCursorVisible(true);
            m_homeNavReady = true;
        }
        ImDrawList *list = ImGui::GetWindowDrawList();
        const ImVec2 top = ImGui::GetCursorScreenPos();
        const float slide = (1.0f - fade) * em(1.5f);

        // The title: the game's logo, or its name in text without the picture.
        float titleHeight = 0.0f;
        ImTextureRef logo;
        ImVec2 logoPixels;
        if (homeArt(HomePicture::Logo, logo, logoPixels)) {
            const float width = column * 1.08f;
            titleHeight = width * logoPixels.y / logoPixels.x;
            const ImVec2 at(top.x - slide - column * 0.03f, top.y);
            list->AddImage(logo, at, ImVec2(at.x + width, at.y + titleHeight), ImVec2(0, 0), ImVec2(1, 1),
                           IM_COL32(255, 255, 255, static_cast<int>(255 * fade)));
        } else {
            const float titleSize = em(2.6f);
            list->AddText(ImGui::GetFont(), titleSize, ImVec2(top.x - slide, top.y), withAlpha(palette::kText, fade),
                          "Rogue");
            list->AddText(ImGui::GetFont(), titleSize, ImVec2(top.x - slide, top.y + titleSize * 1.05f),
                          withAlpha(palette::kGold, fade), "Galaxy");
            titleHeight = titleSize * 2.1f;
        }
        ImGui::Dummy(ImVec2(column, titleHeight + em(2.6f)));

        // The entries.
        const ImVec2 buttonSize(column, em(2.9f));
        // Continue: where the game was left, when it kept a state (Steam Deck
        // fork: added). Play stays below it, for a new start from the title.
        if (m_autoState && m_host.continueGame) {
            ImGui::BeginDisabled(busy);
            if (homeButton("##continue", L("Continue"), buttonSize, !busy, fade, 0))
                startLaunch(true);
            ImGui::SetItemDefaultFocus();
            ImGui::EndDisabled();
        }
        ImGui::BeginDisabled(busy);
        if (homeButton("##play", L("Play"), buttonSize, !busy, fade, 0))
            startLaunch();
        if (!m_autoState || !m_host.continueGame)
            ImGui::SetItemDefaultFocus();
        ImGui::EndDisabled();

        if (!inPanel) {
            ImGui::BeginDisabled(!canUpdate);
            if (homeButton("##update", label.c_str(), buttonSize, canUpdate || update.state == State::UpToDate, fade,
                           adornment) && canUpdate)
                m_host.startUpdate();
            ImGui::EndDisabled();
        }

        // Options: the settings window, over the home screen. Back returns here.
        ImGui::BeginDisabled(busy);
        if (homeButton("##options", L("Options"), buttonSize, !busy, fade, 0))
            openSettings(SettingsPage::Display);
        ImGui::EndDisabled();

        ImGui::BeginDisabled(busy);
        if (homeButton("##quit", L("Quit"), buttonSize, !busy, fade, 0)) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
        }
        ImGui::EndDisabled();
        if (!inPanel) {
            ImGui::Dummy(ImVec2(0.0f, em(0.4f)));
            updateStatus(list, column);
        }
        ImGui::End();
    } else {
        ImGui::End();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // The warp is complete: the game starts behind the loading screen, which
    // takes over this same frame (drawLoadingVeil).
    if (launching && time - m_launchAt >= kLaunchSeconds) {
        m_launchAt = -1.0;
        closeHome();
        if (m_resumeOnLaunch) {
            m_resumeOnLaunch = false;
            m_host.continueGame();
        }
        m_veilAt = time;
        m_veilDoneAt = -1.0;
        m_veilBar = 0.0f;
        m_veilBase = m_host.completedRenders ? m_host.completedRenders() : m_presents;
    }
}

// The loading screen: the night and its stars, the logo breathing, a bar that
// follows the game's first frames. It lifts, the stars leaping forward, once
// the game is drawing; a failsafe lifts it anyway.
void Overlay::drawLoadingVeil() {
    if (m_veilAt < 0.0)
        return;
    constexpr uint64_t kFramesNeeded = 30; // about half a second of picture at 60 fps
    constexpr double kMinSeconds = 1.5, kMaxSeconds = 10.0, kLiftSeconds = 0.7;
    const double time = ImGui::GetTime();
    const double shown = time - m_veilAt;
    const uint64_t renders = m_host.completedRenders ? m_host.completedRenders() : m_presents;
    const uint64_t drawn = renders - m_veilBase;
    if (m_veilDoneAt < 0.0 && ((shown >= kMinSeconds && drawn >= kFramesNeeded) || shown >= kMaxSeconds))
        m_veilDoneAt = time;
    const float lift = m_veilDoneAt >= 0.0 ? smooth(static_cast<float>((time - m_veilDoneAt) / kLiftSeconds)) : 0.0f;
    if (lift >= 1.0f) {
        m_veilAt = -1.0;
        m_veilDoneAt = -1.0;
        // The hint that went unseen behind the home screen.
        if (m_host.steamDeck)
            notify("Press L4 for the menu", Icon::Info, 6.0);
        return;
    }

    const float t = static_cast<float>(time);
    const float enter = smooth(static_cast<float>(shown / 0.6));
    const float content = enter * (1.0f - smooth(lift * 2.0f));
    const float backdrop = 1.0f - lift;
    // The bar: half by the clock, half by the frames, and full at the lift.
    const float target = m_veilDoneAt >= 0.0
                             ? 1.0f
                             : 0.97f * (0.5f * std::min(1.0f, static_cast<float>(shown / kMinSeconds)) +
                                        0.5f * std::min(1.0f, static_cast<float>(drawn) / static_cast<float>(kFramesNeeded)));
    m_veilBar += (target - m_veilBar) * std::min(1.0f, ImGui::GetIO().DeltaTime * 4.0f);

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos, view = viewport->Size;
    const ImVec2 corner(origin.x + view.x, origin.y + view.y);
    const ImVec2 centre(origin.x + view.x * 0.5f, origin.y + view.y * 0.5f);
    ImDrawList *list = ImGui::GetForegroundDrawList();

    list->AddRectFilled(origin, corner, withAlpha(IM_COL32(6, 8, 14, 255), backdrop));
    const float breath = 0.8f + 0.2f * std::sin(t * 1.3f);
    for (int ring = 0; ring < 5; ++ring)
        list->AddCircleFilled(centre, view.y * (0.14f + 0.12f * static_cast<float>(ring)),
                              withAlpha(IM_COL32(70, 100, 190, 255), 0.07f * breath * backdrop), 56);
    // Calm cruising after the dive, then the jump forward as it lifts.
    const float streak = 1.3f - 0.55f * smooth(static_cast<float>(shown / 1.5)) + 2.6f * lift;
    drawWarp(list, centre, view, t, streak, backdrop);

    // The logo, or the name, above the centre.
    ImTextureRef logo;
    ImVec2 logoPixels;
    float bottom = centre.y;
    if (homeArt(HomePicture::Logo, logo, logoPixels)) {
        const float width = std::min(view.x * 0.34f, em(26.0f)) * (1.0f + 0.015f * std::sin(t * 1.6f)) * (1.0f + 0.12f * lift);
        const float height = width * logoPixels.y / logoPixels.x;
        const ImVec2 at(centre.x - width * 0.5f, centre.y - height * 0.62f);
        list->AddImage(logo, at, ImVec2(at.x + width, at.y + height), ImVec2(0, 0), ImVec2(1, 1),
                       IM_COL32(255, 255, 255, static_cast<int>(255 * content)));
        bottom = at.y + height;
    } else {
        const float size = em(2.6f);
        const char *name = "Rogue Galaxy";
        const float width = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, name).x;
        list->AddText(ImGui::GetFont(), size, ImVec2(centre.x - width * 0.5f, centre.y - size), withAlpha(palette::kText, content),
                      name);
        bottom = centre.y;
    }

    // The loading line and its bar.
    const float barW = std::min(view.x * 0.30f, em(22.0f));
    const float barH = em(0.2f);
    const float barY = std::max(bottom + em(2.6f), centre.y + view.y * 0.16f);
    const char *word = L("Loading...");
    const float small = em(0.8f), tracking = em(0.16f);
    const float wordW = ImGui::GetFont()->CalcTextSizeA(small, FLT_MAX, 0.0f, word).x +
                        tracking * static_cast<float>(std::strlen(word));
    trackedText(list, ImVec2(centre.x - wordW * 0.5f, barY - small - em(0.9f)), small, tracking, palette::kTextMuted,
                palette::kTextMuted, word, content);
    const ImVec2 barMin(centre.x - barW * 0.5f, barY), barMax(centre.x + barW * 0.5f, barY + barH);
    list->AddRectFilled(barMin, barMax, withAlpha(IM_COL32(255, 255, 255, 255), 0.10f * content), barH * 0.5f);
    const float fill = barW * std::clamp(m_veilBar, 0.0f, 1.0f);
    if (fill > 1.0f) {
        const ImVec2 fillMax(barMin.x + fill, barMax.y);
        list->AddRectFilled(barMin, fillMax, withAlpha(palette::kGold, content), barH * 0.5f);
        // A glint running over what is filled, and a halo at its head.
        const float glint = std::fmod(t * 0.9f, 1.6f) - 0.3f;
        list->PushClipRect(barMin, fillMax, true);
        const float from = barMin.x + fill * glint;
        list->AddRectFilledMultiColor(ImVec2(from, barMin.y), ImVec2(from + fill * 0.25f, barMax.y), IM_COL32(255, 255, 255, 0),
                                      withAlpha(IM_COL32(255, 255, 255, 255), 0.6f * content),
                                      withAlpha(IM_COL32(255, 255, 255, 255), 0.6f * content), IM_COL32(255, 255, 255, 0));
        list->PopClipRect();
        list->AddCircleFilled(ImVec2(fillMax.x, barMin.y + barH * 0.5f), em(0.7f),
                              withAlpha(palette::kGold, 0.22f * content), 20);
    }
}

void Overlay::drawCapturePrompt() {
    constexpr const char *kName = "##capture";
    if (m_capture.active && now() - m_capture.startedAt > kCaptureSeconds)
        m_capture.active = false;
    // B held on a controller cancels; a tap binds it (captureEvent()).
    if (m_capture.active && m_capture.backDownAt >= 0.0 && now() - m_capture.backDownAt > 0.6) {
        m_capture.active = false;
        m_capture.backDownAt = -1.0;
        m_gamepadNavHeld = true;
    }
    if (m_capture.active && !ImGui::IsPopupOpen(kName))
        ImGui::OpenPopup(kName);
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em(1.6f), em(1.2f)));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    m_capturePromptHovered = false;
    if (ImGui::BeginPopupModal(kName, nullptr, flags)) {
        if (!m_capture.active) {
            ImGui::CloseCurrentPopup();
        } else {
            m_capturePromptHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
            decorateWindow();
            ImDrawList *list = ImGui::GetWindowDrawList();
            const float glyph = em(2.4f);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(glyph, glyph));
            drawPadGlyph(list, m_capture.input, ImVec2(at.x + glyph * 0.5f, at.y + glyph * 0.5f), glyph);
            ImGui::SameLine(0.0f, em(1.0f));
            ImGui::BeginGroup();
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
            ImGui::Text(L("Press %s for %s"), L(m_capture.keyboard ? "a key" : "a button"),
                        L(gfx::padInputName(m_capture.input)));
            ImGui::PopFont();
            if (m_capture.keyboard) {
                // Wrapped, so the prompt keeps a reading width at any menu size.
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + em(32.0f));
                mutedText("Any key, Esc included, but %s; a mouse button outside this box, or the wheel.",
                          reservedKeysText(m_host.gameShortcuts).c_str());
                ImGui::PopTextWrapPos();
            } else
                mutedText("On the controller. Hold %s or press Esc to cancel.",
                          padButtonName(SDL_GAMEPAD_BUTTON_EAST).c_str());
            ImGui::EndGroup();

            // Time left, as a gold line draining away.
            const float left = 1.0f - static_cast<float>((now() - m_capture.startedAt) / kCaptureSeconds);
            ImGui::Dummy(ImVec2(0.0f, em(0.4f)));
            const ImVec2 bar = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            list->AddRectFilled(bar, ImVec2(bar.x + width, bar.y + 3.0f), IM_COL32(255, 255, 255, 30), 2.0f);
            list->AddRectFilled(bar, ImVec2(bar.x + width * std::max(left, 0.0f), bar.y + 3.0f), palette::kGold, 2.0f);
            ImGui::Dummy(ImVec2(width, em(0.6f)));
            if (iconButton("Clear", Icon::Close)) {
                gfx::PadConfig config = m_settings.pad;
                config.slots(m_capture.input, m_capture.keyboard)[m_capture.slot] = {};
                setPadConfig(config);
                m_capture.active = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(L("Cancel")))
                m_capture.active = false;
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

void Overlay::sampleFrameRate() {
    const double time = now();
    const uint64_t renders = m_host.completedRenders ? m_host.completedRenders() : m_presents;
    // A paused game draws nothing; its rate is not zero, it is not running.
    if (m_rateStart == 0.0 || pausing()) {
        m_rateStart = time;
        m_rateRenders = renders;
        return;
    }
    const double elapsed = time - m_rateStart;
    if (elapsed < 0.5)
        return;
    m_fps = static_cast<float>(static_cast<double>(renders - m_rateRenders) / elapsed);
    m_rateStart = time;
    m_rateRenders = renders;
}

// The window in points. The speed badge and the
// notifications sit in its corners, as the notifications do, rather than in
// the picture's: with bars at the sides (4:3 in a wide window) they moved
// in with the picture (Rogue Galaxy: changed).
void Overlay::windowBounds(ImVec2 &min, ImVec2 &max) const {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    min = viewport->WorkPos;
    max = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y);
}

void Overlay::drawSpeedBadge() {
    if (!m_fastForward)
        return;
    ImVec2 min, max;
    windowBounds(min, max);
    const float margin = em(0.6f);
    ImGui::SetNextWindowPos(ImVec2(min.x + margin, min.y + margin), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em(0.7f), em(0.35f)));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    if (beginWindow("##speed", nullptr, flags)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        const IconRoom room = iconRoom(em(1.0f));
        ImGui::SameLine(0.0f, em(0.45f));
        ImGui::TextUnformatted(speedLabel(m_settings.fastForwardSpeed).c_str());
        drawIconBeside(room, Icon::Speed, palette::kGold);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Overlay::drawPauseBanner() {
    if (!canPause() || m_menuOpen || m_homeOpen)
        return;
    const bool background = m_settings.pauseInBackground && !m_windowFocused;
    if (!m_userPaused && !background)
        return;
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em(1.6f), em(1.0f)));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (beginWindow("##paused", nullptr, flags)) {
        const IconRoom room = iconRoom(em(1.6f));
        ImGui::SameLine(0.0f, em(0.8f));
        ImGui::BeginGroup();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
        ImGui::TextUnformatted(L("Paused"));
        ImGui::PopFont();
        mutedText("%s", L(background && !m_userPaused ? "Until the game's window is active again"
                          : padInUse()                ? "Open the menu: Actions > Pause resumes"
                                                      : "Press F5 to resume"));
        ImGui::EndGroup();
        drawIconBeside(room, Icon::Pause, palette::kGold);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Overlay::drawToasts() {
    const double time = now();
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(),
                                  [&](const Toast &toast) { return time - toast.startedAt > toast.duration; }),
                   m_toasts.end());
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    float y = std::max(viewport->WorkPos.y, m_toastTop) + em(0.8f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    for (size_t slot = 0; slot < m_toasts.size(); ++slot) {
        const Toast &toast = m_toasts[slot];
        const float age = static_cast<float>(time - toast.startedAt);
        const float remaining = static_cast<float>(toast.duration) - age;
        const float alpha = std::clamp(std::min(age / 0.18f, remaining / 0.4f), 0.0f, 1.0f);
        const float slide = (1.0f - easeOut(std::min(age / 0.3f, 1.0f))) * em(2.5f);
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - em(0.8f) + slide, y),
                                ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em(1.0f), em(0.6f)));
        char name[16];
        std::snprintf(name, sizeof(name), "##toast%zu", slot);
        if (ImGui::Begin(name, nullptr, flags)) {
            // Messages stay above the windows they may be talking about.
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            decorateWindow(alpha);
            ImTextureRef picture;
            ImVec2 pictureSize;
            if (!toast.picture.empty() && fileTexture(toast.picture.c_str(), picture, pictureSize)) {
                // An achievement's badge, with the text beside it.
                const float side = em(2.6f);
                ImGui::Image(picture, ImVec2(side, side));
                ImGui::SameLine(0.0f, em(0.7f));
                ImGui::BeginGroup();
                ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, (side - ImGui::GetTextLineHeight()) * 0.5f - em(0.2f))));
                ImGui::TextUnformatted(toast.text.c_str());
                ImGui::EndGroup();
            } else {
                const IconRoom room = iconRoom(em(1.15f));
                ImGui::SameLine(0.0f, em(0.6f));
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(toast.text.c_str());
                drawIconBeside(room, toast.icon, withAlpha(palette::kGold, alpha));
            }
            y += ImGui::GetWindowHeight() + em(0.5f);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
}

// Every icon at the sizes the menu draws them (a button's, a notice's, a
// window title's, the About window's), with its name: RG_UI_SCRIPT's
// "icons", for a screenshot when an icon changes.
void Overlay::drawIconSheet() {
    if (!m_iconSheet)
        return;
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em(1.2f), em(1.0f)));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing;
    if (beginWindow("##icons", nullptr, flags)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        constexpr int kColumns = 6;
        if (ImGui::BeginTable("##sheet", kColumns, ImGuiTableFlags_None)) {
            ImDrawList *list = ImGui::GetWindowDrawList();
            for (int index = 0; index < kIconCount; ++index) {
                ImGui::TableNextColumn();
                const Icon kind = static_cast<Icon>(index);
                const ImVec2 at = ImGui::GetCursorScreenPos();
                const float row = em(3.0f);
                float x = at.x;
                for (const float size : {em(0.95f), em(1.15f), em(1.4f), em(3.0f)}) {
                    drawIcon(list, kind, ImVec2(x + size * 0.5f, at.y + row * 0.5f), size, palette::kGold);
                    x += size + em(0.5f);
                }
                ImGui::Dummy(ImVec2(x - at.x, row));
                mutedText("%s", iconName(kind));
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Overlay::updateCursor() {
    if (m_menuOpen)
        return;
    if (now() - m_lastMouseMotion > 2.5)
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
}

std::string Overlay::screenshotFolder() const {
    const std::filesystem::path settings(m_settingsPath);
    return (settings.has_parent_path() ? settings.parent_path() / "screenshots"
                                       : std::filesystem::path("screenshots"))
        .string();
}

void Overlay::takeScreenshot(bool withMenu) {
    m_backend.requestCapture(withMenu, [this](gfx::SdlGpuCapture capture) {
        if (capture.rgba.empty()) {
            notify("The screenshot could not be taken", Icon::Warning);
            return;
        }
        const std::string folder = screenshotFolder();
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        std::string path = folder + "/RogueGalaxy_" + timestamp() + kImageExtension;
        for (int copy = 2; std::filesystem::exists(path, error); ++copy)
            path = folder + "/RogueGalaxy_" + timestamp() + "_" + std::to_string(copy) + kImageExtension;
        // Encoding a PNG takes longer than a frame, so it happens off thread.
        auto done = std::make_shared<std::atomic<bool>>(false);
        std::thread saver([this, capture = std::move(capture), path, done]() mutable {
            for (size_t alpha = 3u; alpha < capture.rgba.size(); alpha += 4u)
                capture.rgba[alpha] = 0xFFu;
            bool saved = false;
            if (SDL_Surface *surface = SDL_CreateSurfaceFrom(static_cast<int>(capture.width),
                                                             static_cast<int>(capture.height),
                                                             SDL_PIXELFORMAT_RGBA32, capture.rgba.data(),
                                                             static_cast<int>(capture.width * 4u))) {
                saved = saveImage(surface, path.c_str());
                SDL_DestroySurface(surface);
            }
            {
                std::lock_guard lock(m_savedMutex);
                m_saved.emplace_back(saved, path);
            }
            done->store(true, std::memory_order_release);
        });
        m_savers.push_back({std::move(saver), std::move(done)});
    });
}

void Overlay::collectSavedScreenshots() {
    std::vector<std::pair<bool, std::string>> saved;
    {
        std::lock_guard lock(m_savedMutex);
        saved.swap(m_saved);
    }
    for (const auto &[ok, path] : saved) {
        if (ok)
            notify(Lf("Screenshot saved: %s", std::filesystem::path(path).filename().string().c_str()), Icon::Camera);
        else
            notify("The screenshot could not be saved", Icon::Warning);
    }
    for (auto it = m_savers.begin(); it != m_savers.end();) {
        if (!it->done->load(std::memory_order_acquire)) {
            ++it;
            continue;
        }
        it->thread.join();
        it = m_savers.erase(it);
    }
}

void Overlay::markDirty() {
    m_dirty = true;
    m_dirtyAt = now();
}

// What the window will open as next time: whether it is maximized, and a
// plain window's size and place, a maximized one keeping those it will be
// restored to. A fullscreen, minimized or hidden window says nothing new.
void Overlay::rememberWindow(const SDL_WindowEvent &event) {
    const SDL_WindowFlags flags = SDL_GetWindowFlags(m_window);
    if (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN))
        return;
    Settings &s = m_settings;
    const bool maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    bool changed = s.maximized != maximized;
    s.maximized = maximized;
    if (!maximized && event.type == SDL_EVENT_WINDOW_RESIZED &&
        (s.windowWidth != event.data1 || s.windowHeight != event.data2)) {
        s.windowWidth = event.data1;
        s.windowHeight = event.data2;
        changed = true;
    } else if (!maximized && event.type == SDL_EVENT_WINDOW_MOVED &&
               (!s.windowPlaced || s.windowX != event.data1 || s.windowY != event.data2)) {
        s.windowPlaced = true;
        s.windowX = event.data1;
        s.windowY = event.data2;
        changed = true;
    }
    if (changed)
        markDirty();
}

void Overlay::saveIfDirty(bool force) {
    if (!m_dirty || (!force && now() - m_dirtyAt < 0.75))
        return;
    m_dirty = false;
    if (!saveSettings(m_settingsPath, m_settings))
        std::fprintf(stderr, "[ui] could not save settings to %s\n", m_settingsPath.c_str());
}

void Overlay::applyVolume() {
    if (!m_host.setVolume)
        return;
    const bool silent = m_settings.muted;
    m_host.setVolume(silent ? 0.0f : static_cast<float>(m_settings.volume) / 100.0f);
}

bool Overlay::pausing() const {
    return canPause() && (m_homeOpen || m_slotMode != 0 || m_userPaused || (m_settings.pauseInMenu && m_menuOpen) ||
                          (m_settings.pauseInBackground && !m_windowFocused));
}

void Overlay::applyPause() {
    const bool paused = pausing();
    if (!m_host.setPaused || paused == m_hostPaused)
        return;
    m_hostPaused = paused;
    m_host.setPaused(paused);
}

void Overlay::setPadConfig(const gfx::PadConfig &config) {
    m_settings.pad = config;
    m_backend.padInput().setConfig(config);
    markDirty();
}

void Overlay::requestScale(uint32_t scale) {
    scale = std::clamp(scale, 1u, 8u);
    m_backend.requestResolutionScale(scale);
    m_pendingScale = scale != m_backend.activeResolutionScale() ? scale : 0u;
    m_pendingScaleSince = now();
    m_settings.resolutionScale = scale;
    markDirty();
}

SDL_GamepadType Overlay::displayedControllerType() const {
    const auto controllers = m_backend.padInput().controllers();
    for (const auto &controller : controllers)
        if (controller.active)
            return controller.type;
    return controllers.empty() ? SDL_GAMEPAD_TYPE_STANDARD : controllers.front().type;
}

} // namespace dq8::ui
