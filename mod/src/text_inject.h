// Adds the mod's texts (Settings row, DualSense page) to the game's loaded string table - supporting common
// RDR2 languages with automatic detection and English fallback (see text_inject.cpp). Starts its own thread.
#pragma once

#include <cctype>
#include <map>
#include <string>

namespace dsr2 {

void text_inject_start();
void text_inject_stop();
bool text_inject_ready();
void text_inject_set_game_lang(int id);

// Maps RDR2's GET_CURRENT_LANGUAGE() enum to mod translation codes; unsupported languages fall back to "en".
inline std::string game_lang_code(int id) {
    switch (id) {
    case 0: return "en";
    case 1: return "fr";
    case 2: return "de";
    case 3: return "it";
    case 4: return "es";
    case 5: return "pt";
    case 6: return "pl";
    case 7: return "ru";
    case 11: return "es";  // Latin American / Mexican Spanish
    default: return "en";  // Korean, Chinese, Japanese, or unknown -> English fallback
    }
}

// Parses [General] Language=auto|en|de|fr|es|it|pt|pl|ru; returns "" for auto, or normalized language code.
inline std::string parse_language_override(const std::string& raw) {
    std::string s;
    for (unsigned char c : raw)
        if (!std::isspace(c)) s += char(std::tolower(c));
    if (s.empty() || s == "auto") return "";
    if (s == "en" || s == "eng" || s == "english" || s == "englisch") return "en";
    if (s == "de" || s == "deu" || s == "ger" || s == "german" || s == "deutsch") return "de";
    if (s == "fr" || s == "fra" || s == "fre" || s == "french" || s == "francais") return "fr";
    if (s == "es" || s == "spa" || s == "spanish" || s == "espanol" || s == "mx") return "es";
    if (s == "it" || s == "ita" || s == "italian" || s == "italiano") return "it";
    if (s == "pt" || s == "por" || s == "pt-br" || s == "pt_br" || s == "portuguese" || s == "portugues") return "pt";
    if (s == "pl" || s == "pol" || s == "polish" || s == "polski") return "pl";
    if (s == "ru" || s == "rus" || s == "russian") return "ru";
    if (s.size() == 2 && std::isalpha(static_cast<unsigned char>(s[0])) &&
        std::isalpha(static_cast<unsigned char>(s[1])))
        return s;
    return "en";
}

// Inspects global.yldb probe strings ("MO_ON", "MO_OFF", "IB_BACK", "IB_SELECT") on title/menu load;
// falls back cleanly to "en" for English or unsupported languages (e.g. Japanese, Korean, Chinese).
inline std::string detect_lang_from_probes(const std::string& mo_on, const std::string& mo_off,
                                           const std::string& ib_back, const std::string& ib_select) {
    auto low = [](const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (unsigned char c : in) out += c < 0x80 ? char(std::tolower(c)) : char(c);
        return out;
    };
    const std::string on = low(mo_on), off = low(mo_off), back = low(ib_back), sel = low(ib_select);
    // Prioritize distinct button labels first across supported languages
    if (back.rfind("zur", 0) == 0 || sel.rfind("ausw", 0) == 0) return "de";
    if (back == "retour" || sel.rfind("s\xC3\xA9lection", 0) == 0 || sel.rfind("selection", 0) == 0) return "fr";
    if (back == "volver" || back.rfind("atr\xC3\xA1s", 0) == 0 || back == "atras" || sel == "seleccionar") return "es";
    if (back == "indietro" || sel == "seleziona") return "it";
    if (back == "voltar" || sel == "selecionar") return "pt";
    if (back == "wstecz" || sel == "wybierz") return "pl";
    if (back == "\xD0\x9D\xD0\xB0\xD0\xB7\xD0\xB0\xD0\xB4" || back == "\xD0\xBD\xD0\xB0\xD0\xB7\xD0\xB0\xD0\xB4" ||
        sel == "\xD0\x92\xD1\x8B\xD0\xB1\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C" || sel == "\xD0\xB2\xD1\x8B\xD0\xB1\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C")
        return "ru";
    // Secondary toggle string checks
    if (on == "ein" || off == "aus") return "de";
    if (on == "oui" || on.rfind("activ\xC3\xA9", 0) == 0) return "fr";
    if (on == "s\xC3\xAD" || on == "activado" || off == "desactivado") return "es";
    if (on == "s\xC3\xAC" || on == "attivo" || off == "disattivo") return "it";
    if (on == "sim" || on == "ligado" || off == "desligado" || off.rfind("n\xC3\xA3o", 0) == 0) return "pt";
    if (on.rfind("w\xC5\x82", 0) == 0 || off.rfind("wy\xC5\x82", 0) == 0 || on == "tak") return "pl";
    if (on.rfind("\xD0\x92\xD0\xBA\xD0\xBB", 0) == 0 || on.rfind("\xD0\xB2\xD0\xBA\xD0\xBB", 0) == 0 ||
        off.rfind("\xD0\x92\xD1\x8B\xD0\xBA\xD0\xBB", 0) == 0 || off.rfind("\xD0\xB2\xD1\x8B\xD0\xBA\xD0\xBB", 0) == 0)
        return "ru";
    return "en";
}

// Resolves a UI string key from a loaded language dictionary (with shared Touchpad option key support),
// falling back cleanly to compiled-in English whenever a key or language is unavailable.
inline std::string resolve_translation(const std::map<std::string, std::string>& dict, const std::string& key,
                                       const char* fallback_en) {
    auto it = dict.find(key);
    if (it != dict.end() && !it->second.empty()) return it->second;
    if (key.rfind("DS_TOUCHPAD_", 0) == 0) {
        size_t sep = key.find('_', 12);
        if (sep != std::string::npos) {
            std::string suffix = key.substr(sep + 1);
            if (suffix != "DESC") {
                auto oit = dict.find("DS_TOUCHPAD_OPT_" + suffix);
                if (oit != dict.end() && !oit->second.empty()) return oit->second;
            }
        }
    }
    return fallback_en ? std::string(fallback_en) : std::string();
}

}  // namespace dsr2
