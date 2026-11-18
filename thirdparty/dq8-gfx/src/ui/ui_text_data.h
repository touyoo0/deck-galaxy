// The translation tables behind L() (ui_text.h), one per area of the menu.
// An empty text means "no translation": the English one shows.
#pragma once

#include <cstddef>

namespace dq8::ui {

struct TextEntry {
    const char *en; // the English wording, which is the key
    const char *fr;
    const char *de;
    const char *es;
    const char *it;
};

extern const TextEntry kTextsHome[];    // the home screen, the quit box, the update
extern const size_t kTextsHomeCount;
extern const TextEntry kTextsMenu[];    // the settings window and the menu bar
extern const size_t kTextsMenuCount;
extern const TextEntry kTextsOptions[]; // the game's own options (src/graphics.cpp)
extern const size_t kTextsOptionsCount;

} // namespace dq8::ui
