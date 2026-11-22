#include "ui/ui_text.h"

#include "ui/ui_text_data.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dq8::ui {

const char *const kMenuLanguageCodes[kMenuLanguageCount] = {"en", "fr", "de", "es", "it"};
const char *const kMenuLanguageNames[kMenuLanguageCount] = {"English", "Fran\xC3\xA7" "ais", "Deutsch",
                                                            "Espa\xC3\xB1ol", "Italiano"};

namespace {
std::mutex gMutex; // the menu draws on one thread, but the host may ask from another
int gLanguage = 0;
bool gLogMissing = false;

int indexOfCode(std::string_view code) {
    for (int index = 0; index < kMenuLanguageCount; ++index)
        if (code == kMenuLanguageCodes[index])
            return index;
    return -1;
}

// "fr_FR.UTF-8" -> "fr"; -1 for a language the menu has no texts for.
int indexOfLocale(const std::string &value) {
    std::string code = value.substr(0, value.find_first_of("._@"));
    std::transform(code.begin(), code.end(), code.begin(), [](unsigned char c) { return std::tolower(c); });
    const auto dash = code.find('-');
    if (dash != std::string::npos)
        code.resize(dash);
    return indexOfCode(code);
}

// What the environment says: a language of the menu's, or -1 when it says
// nothing, or -2 when it names a language the menu has no texts for.
int languageFromEnvironment(std::initializer_list<const char *> names) {
    for (const char *name : names) {
        const char *raw = std::getenv(name);
        if (!raw)
            continue;
        std::istringstream list(raw);
        for (std::string part; std::getline(list, part, ':');) {
            if (part.empty() || part == "C" || part == "POSIX")
                continue;
            const int found = indexOfLocale(part);
            return found >= 0 ? found : -2;
        }
    }
    return -1;
}

// The language in Steam's own settings, which is the Deck's: -1 when there is
// none, and English for one the menu has no texts for.
int languageFromSteam() {
    const char *home = std::getenv("HOME");
    if (!home)
        return -1;
    std::ifstream file(std::string(home) + "/.steam/registry.vdf");
    for (std::string line; std::getline(file, line);) {
        const auto key = line.find("\"language\"");
        if (key == std::string::npos)
            continue;
        const auto open = line.find('"', key + 10u);
        const auto close = open == std::string::npos ? open : line.find('"', open + 1u);
        if (close == std::string::npos)
            continue;
        const std::string value = line.substr(open + 1u, close - open - 1u);
        if (value == "french") return 1;
        if (value == "german") return 2;
        if (value == "spanish" || value == "latam") return 3;
        if (value == "italian") return 4;
        return 0;
    }
    return -1;
}

// The Deck's language: RG_LANG for a run of one's own, else Steam's language
// setting, else the system's locale, else English.
int detectLanguage() {
    for (const int found : {languageFromEnvironment({"RG_LANG"}), languageFromSteam(),
                            languageFromEnvironment({"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"})})
        if (found >= 0)
            return found;
    return 0;
}

struct Table {
    // English text -> its row, built on first use.
    std::unordered_map<std::string_view, const TextEntry *> byEnglish;
    Table() {
        const struct {
            const TextEntry *rows;
            size_t count;
        } groups[] = {{kTextsHome, kTextsHomeCount}, {kTextsMenu, kTextsMenuCount},
                      {kTextsOptions, kTextsOptionsCount}};
        for (const auto &group : groups)
            for (size_t index = 0; index < group.count; ++index)
                byEnglish.emplace(group.rows[index].en, &group.rows[index]);
        gLogMissing = std::getenv("RG_LANG_MISSING") != nullptr;
    }
};

Table &table() {
    static Table instance;
    return instance;
}

const char *pick(const TextEntry &row, int language) {
    const char *text = nullptr;
    switch (language) {
    case 1: text = row.fr; break;
    case 2: text = row.de; break;
    case 3: text = row.es; break;
    case 4: text = row.it; break;
    default: break;
    }
    return text && *text ? text : row.en;
}

const char *translate(const char *english) {
    if (gLanguage == 0)
        return english;
    Table &rows = table();
    const auto found = rows.byEnglish.find(english);
    if (found != rows.byEnglish.end())
        return pick(*found->second, gLanguage);
    if (gLogMissing && english[0] != '\0') {
        static std::unordered_map<std::string, bool> reported;
        if (reported.emplace(english, true).second)
            std::fprintf(stderr, "[lang] no %s text for: %s\n", kMenuLanguageCodes[gLanguage], english);
    }
    return english;
}
} // namespace

void setMenuLanguage(const std::string &setting) {
    std::lock_guard<std::mutex> lock(gMutex);
    const int found = indexOfCode(setting);
    gLanguage = found >= 0 ? found : setting == "auto" ? detectLanguage() : 0;
    table();
}

int detectedLanguageIndex() { return detectLanguage(); }

const char *menuLanguage() { return kMenuLanguageCodes[gLanguage]; }

int menuLanguageIndex() { return gLanguage; }

const char *L(const char *english) {
    if (!english)
        return english;
    // "Quit?##quit": the label changes, the id stays.
    if (const char *hashes = std::strstr(english, "##")) {
        static std::unordered_map<std::string, std::string> cache;
        const std::string key = std::string(1, static_cast<char>('0' + gLanguage)) + english;
        auto found = cache.find(key);
        if (found == cache.end()) {
            const std::string head(english, hashes);
            found = cache.emplace(key, std::string(translate(head.c_str())) + hashes).first;
        }
        return found->second.c_str();
    }
    return translate(english);
}

std::string L(const std::string &english) { return L(english.c_str()); }

std::string Lf(const char *englishFormat, ...) {
    va_list args;
    va_start(args, englishFormat);
    const char *format = L(englishFormat);
    va_list copy;
    va_copy(copy, args);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    const int size = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::string text(size > 0 ? static_cast<size_t>(size) : 0u, '\0');
    if (size > 0)
        std::vsnprintf(text.data(), text.size() + 1u, format, args);
#pragma GCC diagnostic pop
    va_end(args);
    return text;
}

} // namespace dq8::ui
