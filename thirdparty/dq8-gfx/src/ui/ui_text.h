// The menu's languages (Rogue Galaxy: added): English, French, German, Spanish
// and Italian. Each text in the code is its English wording; L() gives the
// same text in the menu's language, or the English one when it has no
// translation. The translations are in ui_text.cpp.
#pragma once

#include <string>

namespace dq8::ui {

// The languages a player can pick, "auto" first: codes for settings.ini.
constexpr int kMenuLanguageCount = 5;
extern const char *const kMenuLanguageCodes[kMenuLanguageCount]; // "en", "fr", "de", "es", "it"
extern const char *const kMenuLanguageNames[kMenuLanguageCount]; // each in its own language

// Sets the language: "auto", the default, is the Deck's (RG_LANG, then Steam's
// own language setting, then the system's locale), else one of the codes.
// Anything else is English.
void setMenuLanguage(const std::string &setting);
// The code in use ("en", "fr", ...), what "auto" came to.
const char *menuLanguage();
// Its place in kMenuLanguageCodes.
int menuLanguageIndex();

// The text in the menu's language. The returned pointer lives as long as the
// program (or as long as `english`, when there is no translation). A text
// with "##" keeps what follows it as it is: only what is shown changes.
const char *L(const char *english);
// The same for a std::string; a copy, as the text may be built on the spot.
std::string L(const std::string &english);

// A printf-style text in the menu's language: translate the format, keep
// every % the English one has, in the same order.
std::string Lf(const char *englishFormat, ...) __attribute__((format(printf, 1, 2)));

} // namespace dq8::ui
