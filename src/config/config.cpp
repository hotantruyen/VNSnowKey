/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.hpp"
#include "../engine/bamboo_utils.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <linux/input-event-codes.h>
#include <sstream>

namespace vnsnowkey {

namespace {

std::string trim(std::string_view sv) {
    size_t s = 0;
    while (s < sv.size() && (sv[s] == ' ' || sv[s] == '\t' || sv[s] == '\r' || sv[s] == '\n')) ++s;
    size_t e = sv.size();
    while (e > s && (sv[e - 1] == ' ' || sv[e - 1] == '\t' || sv[e - 1] == '\r' || sv[e - 1] == '\n')) --e;
    return std::string(sv.substr(s, e - s));
}

std::string toLowerAscii(std::string_view sv) {
    std::string out(sv);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return out;
}

std::string escapeTomlString(std::string_view sv) {
    std::string out;
    out.reserve(sv.size() + 4);
    for (char c : sv) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out.push_back(c);
    }
    return out;
}

std::string unescapeTomlString(std::string_view sv) {
    std::string s = trim(sv);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[i + 1];
            if (next == 'n') { out.push_back('\n'); ++i; }
            else if (next == 'r') { out.push_back('\r'); ++i; }
            else if (next == 't') { out.push_back('\t'); ++i; }
            else if (next == '"' || next == '\\') { out.push_back(next); ++i; }
            else out.push_back(s[i]);
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

bool parseBool(std::string_view sv, bool defaultVal = false) {
    std::string s = toLowerAscii(trim(sv));
    if (s == "true" || s == "1" || s == "yes" || s == "on") return true;
    if (s == "false" || s == "0" || s == "no" || s == "off") return false;
    return defaultVal;
}

int parseInt(std::string_view sv, int defaultVal = 0) {
    std::string s = trim(sv);
    if (s.empty()) return defaultVal;
    try {
        return std::stoi(s);
    } catch (...) {
        return defaultVal;
    }
}

} // namespace

bamboo::SessionOptions Config::toSessionOptions() const {
    bamboo::SessionOptions opts;
    opts.vietnameseEnabled = vietnameseEnabled && !safeMode;
    opts.inputMethodName = inputMethod;
    opts.outputCharset = outputCharset;
    opts.freeToneMarking = freeToneMarking;
    opts.modernToneStyle = modernToneStyle;
    opts.spellCheckEnabled = spellCheckEnabled;
    opts.autoRestoreEnglish = autoRestoreEnglish;
    opts.shorthandInitialConsonant = shorthandInitialConsonant;
    opts.shorthandFinalConsonant = shorthandFinalConsonant;
    opts.macroEnabled = macroEnabled;
    opts.macroAutoCase = macroAutoCase;
    return opts;
}

const AppProfile* Config::findProfileForApp(const std::string& appClass) const {
    if (appClass.empty()) return nullptr;
    std::string lowerApp = toLowerAscii(appClass);
    for (const auto& prof : appProfiles) {
        if (!prof.appClass.empty() && toLowerAscii(prof.appClass) == lowerApp) {
            return &prof;
        }
    }
    for (const auto& prof : appProfiles) {
        if (prof.appClass.empty()) continue;
        std::string lowerProf = toLowerAscii(prof.appClass);
        if (lowerApp.find(lowerProf) != std::string::npos) {
            return &prof;
        }
    }
    return nullptr;
}

namespace {

AppProfile makeDefaultMinecraftProfile() {
    AppProfile minecraft;
    minecraft.appClass = "minecraft";
    minecraft.mode = AppMode::GameMode;
    minecraft.delayUs = 2000;
    return minecraft;
}

} // namespace

Config Config::createDefault() {
    Config cfg;
    cfg.configVersion = kCurrentConfigVersion;
    cfg.macros["vn"] = "Việt Nam";
    cfg.macros["tphcm"] = "Thành phố Hồ Chí Minh";
    cfg.macros["hn"] = "Hà Nội";

    // Default helpful profiles:
    // 1. Password managers: excluded by default for security
    AppProfile keepass;
    keepass.appClass = "keepassxc";
    keepass.mode = AppMode::Excluded;
    cfg.appProfiles.push_back(keepass);

    AppProfile bitwarden;
    bitwarden.appClass = "bitwarden";
    bitwarden.mode = AppMode::Excluded;
    cfg.appProfiles.push_back(bitwarden);

    // 2. Browsers: autocompleteFix = true (using atomic KEY_DELETE to cancel inline autocomplete suggestions
    //    without inserting spaces or dropping characters in Omnibox/URL search bars or web text inputs).
    for (const char* browser : {"firefox", "chromium", "google-chrome", "brave", "vivaldi", "microsoft-edge", "opera"}) {
        AppProfile prof;
        prof.appClass = browser;
        prof.mode = AppMode::Default;
        prof.autocompleteFix = true;
        cfg.appProfiles.push_back(prof);
    }

    // 3. 3D Games (Game Mode): raw WASD pass-through in 3D, auto Vietnamese in chat/UI
    cfg.appProfiles.push_back(makeDefaultMinecraftProfile());

    return cfg;
}

std::string Config::outputMethodToString(OutputMethod m) {
    switch (m) {
        case OutputMethod::XkbKeymap: return "xkb_keymap";
        case OutputMethod::ClipboardCtrlV: return "clipboard_ctrl_v";
        case OutputMethod::ClipboardCtrlShiftV: return "clipboard_ctrl_shift_v";
        case OutputMethod::CtrlShiftU: return "ctrl_shift_u";
    }
    return "xkb_keymap";
}

OutputMethod Config::stringToOutputMethod(const std::string& s) {
    std::string l = toLowerAscii(trim(s));
    if (l == "clipboard_ctrl_v" || l == "clipboard") return OutputMethod::ClipboardCtrlV;
    if (l == "clipboard_ctrl_shift_v") return OutputMethod::ClipboardCtrlShiftV;
    if (l == "ctrl_shift_u" || l == "hex") return OutputMethod::CtrlShiftU;
    return OutputMethod::XkbKeymap;
}

std::string Config::appModeToString(AppMode m) {
    switch (m) {
        case AppMode::Default: return "default";
        case AppMode::AlwaysVi: return "always_vi";
        case AppMode::AlwaysEn: return "always_en";
        case AppMode::Excluded: return "excluded";
        case AppMode::GameMode: return "game_mode";
    }
    return "default";
}

AppMode Config::stringToAppMode(const std::string& s) {
    std::string l = toLowerAscii(trim(s));
    if (l == "always_vi" || l == "vi") return AppMode::AlwaysVi;
    if (l == "always_en" || l == "en") return AppMode::AlwaysEn;
    if (l == "excluded" || l == "disabled") return AppMode::Excluded;
    if (l == "game_mode" || l == "gamemode" || l == "game") return AppMode::GameMode;
    return AppMode::Default;
}

namespace {

struct HotkeyNameEntry {
    uint16_t code;
    const char* name;
};

constexpr HotkeyNameEntry kHotkeyNames[] = {
    {KEY_A, "A"}, {KEY_B, "B"}, {KEY_C, "C"}, {KEY_D, "D"}, {KEY_E, "E"},
    {KEY_F, "F"}, {KEY_G, "G"}, {KEY_H, "H"}, {KEY_I, "I"}, {KEY_J, "J"},
    {KEY_K, "K"}, {KEY_L, "L"}, {KEY_M, "M"}, {KEY_N, "N"}, {KEY_O, "O"},
    {KEY_P, "P"}, {KEY_Q, "Q"}, {KEY_R, "R"}, {KEY_S, "S"}, {KEY_T, "T"},
    {KEY_U, "U"}, {KEY_V, "V"}, {KEY_W, "W"}, {KEY_X, "X"}, {KEY_Y, "Y"},
    {KEY_Z, "Z"},
    {KEY_0, "0"}, {KEY_1, "1"}, {KEY_2, "2"}, {KEY_3, "3"}, {KEY_4, "4"},
    {KEY_5, "5"}, {KEY_6, "6"}, {KEY_7, "7"}, {KEY_8, "8"}, {KEY_9, "9"},
    {KEY_F1, "F1"}, {KEY_F2, "F2"}, {KEY_F3, "F3"}, {KEY_F4, "F4"},
    {KEY_F5, "F5"}, {KEY_F6, "F6"}, {KEY_F7, "F7"}, {KEY_F8, "F8"},
    {KEY_F9, "F9"}, {KEY_F10, "F10"}, {KEY_F11, "F11"}, {KEY_F12, "F12"},
    {KEY_SPACE, "Space"}, {KEY_TAB, "Tab"}, {KEY_ENTER, "Enter"},
    {KEY_ESC, "Esc"}, {KEY_BACKSPACE, "Backspace"}, {KEY_DELETE, "Delete"},
    {KEY_INSERT, "Insert"}, {KEY_HOME, "Home"}, {KEY_END, "End"},
    {KEY_PAGEUP, "PageUp"}, {KEY_PAGEDOWN, "PageDown"},
    {KEY_UP, "Up"}, {KEY_DOWN, "Down"}, {KEY_LEFT, "Left"}, {KEY_RIGHT, "Right"},
    {KEY_PAUSE, "Pause"}, {KEY_SYSRQ, "PrintScreen"}, {KEY_SCROLLLOCK, "ScrollLock"},
    {KEY_CAPSLOCK, "CapsLock"}, {KEY_GRAVE, "Grave"}, {KEY_MINUS, "Minus"},
    {KEY_EQUAL, "Equal"}, {KEY_LEFTBRACE, "LeftBrace"}, {KEY_RIGHTBRACE, "RightBrace"},
    {KEY_BACKSLASH, "Backslash"}, {KEY_SEMICOLON, "Semicolon"}, {KEY_APOSTROPHE, "Apostrophe"},
    {KEY_COMMA, "Comma"}, {KEY_DOT, "Dot"}, {KEY_SLASH, "Slash"}
};

} // namespace

uint16_t Config::hotkeyNameToEvdevCode(std::string_view keyName) {
    std::string low = toLowerAscii(trim(keyName));
    if (low.empty()) return 0;
    if (low == "escape") low = "esc";
    if (low == "return") low = "enter";
    if (low == "del") low = "delete";
    if (low == "ins") low = "insert";
    if (low == "pgup") low = "pageup";
    if (low == "pgdn" || low == "pgdown") low = "pagedown";
    if (low == "print" || low == "prtsc" || low == "sysrq") low = "printscreen";
    if (low == "break") low = "pause";
    if (low == "`" || low == "backquote") low = "grave";
    if (low == "-") low = "minus";
    if (low == "=") low = "equal";
    if (low == "[") low = "leftbrace";
    if (low == "]") low = "rightbrace";
    if (low == "\\") low = "backslash";
    if (low == ";") low = "semicolon";
    if (low == "'") low = "apostrophe";
    if (low == ",") low = "comma";
    if (low == ".") low = "dot";
    if (low == "/") low = "slash";

    for (const auto& entry : kHotkeyNames) {
        if (toLowerAscii(entry.name) == low) {
            return entry.code;
        }
    }
    return 0;
}

std::string Config::evdevCodeToHotkeyName(uint16_t code) {
    for (const auto& entry : kHotkeyNames) {
        if (entry.code == code) {
            return entry.name;
        }
    }
    return {};
}

ParsedHotkey Config::parseHotkey(std::string_view spec) {
    std::string trimmed = trim(spec);
    if (trimmed.empty()) return {};

    ParsedHotkey result;
    size_t start = 0;
    while (start <= trimmed.size()) {
        size_t plus = trimmed.find('+', start);
        std::string tok = trim(plus == std::string::npos
                                   ? std::string_view(trimmed).substr(start)
                                   : std::string_view(trimmed).substr(start, plus - start));
        if (!tok.empty()) {
            std::string low = toLowerAscii(tok);
            if (low == "ctrl" || low == "control") {
                result.modifiers |= kModCtrl;
            } else if (low == "alt") {
                result.modifiers |= kModAlt;
            } else if (low == "shift") {
                result.modifiers |= kModShift;
            } else if (low == "super" || low == "meta" || low == "win" || low == "mod4") {
                result.modifiers |= kModSuper;
            } else {
                uint16_t code = hotkeyNameToEvdevCode(tok);
                if (code == 0 || result.keyCode != 0) {
                    return ParsedHotkey{};
                }
                result.keyCode = code;
            }
        }
        if (plus == std::string::npos) break;
        start = plus + 1;
    }

    if (!result.isValid()) {
        return ParsedHotkey{};
    }
    return result;
}

std::string Config::formatHotkey(uint8_t modifiers, uint16_t keyCode) {
    ParsedHotkey hk;
    hk.modifiers = modifiers;
    hk.keyCode = keyCode;
    if (!hk.isValid()) return {};

    std::vector<std::string> parts;
    if (modifiers & kModCtrl)  parts.emplace_back("Ctrl");
    if (modifiers & kModAlt)   parts.emplace_back("Alt");
    if (modifiers & kModShift) parts.emplace_back("Shift");
    if (modifiers & kModSuper) parts.emplace_back("Super");
    if (keyCode != 0) {
        std::string kname = evdevCodeToHotkeyName(keyCode);
        if (kname.empty()) return {};
        parts.push_back(std::move(kname));
    }

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += "+";
        out += parts[i];
    }
    return out;
}

std::string Config::canonicalizeHotkey(std::string_view spec) {
    ParsedHotkey hk = parseHotkey(spec);
    if (!hk.isValid()) return {};
    return formatHotkey(hk.modifiers, hk.keyCode);
}

std::string Config::toToml() const {
    std::ostringstream oss;
    oss << "# VNSnowKey Configuration File (~/.config/vnsnowkey/config.toml)\n\n";

    oss << "[general]\n";
    oss << "config_version = " << configVersion << "\n";
    oss << "vietnamese_enabled = " << (vietnameseEnabled ? "true" : "false") << "\n";
    oss << "input_method = \"" << escapeTomlString(inputMethod) << "\"\n";
    oss << "output_charset = \"" << escapeTomlString(outputCharset) << "\"\n";
    oss << "autostart = " << (autostart ? "true" : "false") << "\n";
    oss << "osd_enabled = " << (osdEnabled ? "true" : "false") << "\n";
    oss << "remember_per_app_lang = " << (rememberPerAppLang ? "true" : "false") << "\n";
    oss << "bypass_password_dialogs = " << (bypassPasswordDialogs ? "true" : "false") << "\n";
    oss << "safe_mode = " << (safeMode ? "true" : "false") << "\n\n";

    oss << "[typing]\n";
    oss << "free_tone_marking = " << (freeToneMarking ? "true" : "false") << "\n";
    oss << "modern_tone_style = " << (modernToneStyle ? "true" : "false") << "\n";
    oss << "spellcheck_enabled = " << (spellCheckEnabled ? "true" : "false") << "\n";
    oss << "auto_restore_english = " << (autoRestoreEnglish ? "true" : "false") << "\n";
    oss << "shorthand_initial_consonant = " << (shorthandInitialConsonant ? "true" : "false") << "\n";
    oss << "shorthand_final_consonant = " << (shorthandFinalConsonant ? "true" : "false") << "\n";
    oss << "macro_enabled = " << (macroEnabled ? "true" : "false") << "\n";
    oss << "macro_auto_case = " << (macroAutoCase ? "true" : "false") << "\n\n";

    oss << "[shortcuts]\n";
    oss << "toggle_primary = \"" << escapeTomlString(shortcuts.togglePrimary) << "\"\n";
    oss << "toggle_secondary = \"" << escapeTomlString(shortcuts.toggleSecondary) << "\"\n";
    oss << "toggle_extra = \"" << escapeTomlString(shortcuts.toggleExtra) << "\"\n";
    oss << "restore_word = \"" << escapeTomlString(shortcuts.restoreWord) << "\"\n";
    oss << "panic_safe_mode = \"" << escapeTomlString(shortcuts.panicSafeMode) << "\"\n\n";

    oss << "[output]\n";
    oss << "default_method = \"" << outputMethodToString(defaultOutputMethod) << "\"\n";
    oss << "default_delay_us = " << defaultDelayUs << "\n";
    oss << "clipboard_restore_delay_ms = " << clipboardRestoreDelayMs << "\n";
    oss << "reset_on_mouse_click = " << (resetOnMouseClick ? "true" : "false") << "\n";
    oss << "reset_on_focus_change = " << (resetOnFocusChange ? "true" : "false") << "\n";
    oss << "reset_on_navigation_keys = " << (resetOnNavigationKeys ? "true" : "false") << "\n";
    oss << "buffer_timeout_ms = " << bufferTimeoutMs << "\n";
    oss << "stats_enabled = " << (statsEnabled ? "true" : "false") << "\n";
    oss << "debug_redacted = " << (debugRedacted ? "true" : "false") << "\n\n";

    oss << "[macros]\n";
    for (const auto& [k, v] : macros) {
        oss << "\"" << escapeTomlString(k) << "\" = \"" << escapeTomlString(v) << "\"\n";
    }
    oss << "\n";

    for (const auto& prof : appProfiles) {
        oss << "[[app_profiles]]\n";
        oss << "app_class = \"" << escapeTomlString(prof.appClass) << "\"\n";
        oss << "mode = \"" << appModeToString(prof.mode) << "\"\n";
        oss << "output_method = \"" << outputMethodToString(prof.outputMethod) << "\"\n";
        oss << "use_custom_output_method = " << (prof.useCustomOutputMethod ? "true" : "false") << "\n";
        oss << "delay_us = " << prof.delayUs << "\n";
        oss << "autocomplete_fix = " << (prof.autocompleteFix ? "true" : "false") << "\n\n";
    }

    return oss.str();
}

Config Config::fromToml(std::string_view tomlText) {
    Config cfg = Config::createDefault();
    int parsedConfigVersion = 1;
    bool clearedDefaultMacros = false;
    bool clearedDefaultProfiles = false;

    bool hasNewShortcuts = false;
    bool hasLegacyCtrlShift = false;
    bool legacyCtrlShift = true;
    bool hasLegacySuperSpace = false;
    bool legacySuperSpace = true;
    bool hasLegacyCtrlSpace = false;
    bool legacyCtrlSpace = false;
    bool hasLegacyAltZ = false;
    bool legacyAltZ = false;
    bool hasLegacyRestoreEsc = false;
    bool legacyRestoreEsc = false;
    bool hasLegacyPanic = false;
    bool legacyPanic = true;

    std::string section;
    std::istringstream iss{std::string(tomlText)};
    std::string rawLine;

    while (std::getline(iss, rawLine)) {
        // Strip unquoted comments
        bool inQuotes = false;
        size_t commentPos = std::string::npos;
        for (size_t i = 0; i < rawLine.size(); ++i) {
            if (rawLine[i] == '"' && (i == 0 || rawLine[i - 1] != '\\')) {
                inQuotes = !inQuotes;
            } else if (!inQuotes && rawLine[i] == '#') {
                commentPos = i;
                break;
            }
        }
        std::string line = trim(commentPos == std::string::npos ? rawLine : rawLine.substr(0, commentPos));
        if (line.empty()) continue;

        if (line == "[[app_profiles]]") {
            if (!clearedDefaultProfiles) {
                cfg.appProfiles.clear();
                clearedDefaultProfiles = true;
            }
            cfg.appProfiles.push_back(AppProfile{});
            section = "app_profiles";
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            if (section == "macros" && !clearedDefaultMacros) {
                cfg.macros.clear();
                clearedDefaultMacros = true;
            }
            continue;
        }

        size_t eq = std::string::npos;
        inQuotes = false;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) {
                inQuotes = !inQuotes;
            } else if (!inQuotes && line[i] == '=') {
                eq = i;
                break;
            }
        }
        if (eq == std::string::npos) continue;

        std::string key = unescapeTomlString(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (section == "general") {
            if (key == "config_version") {
                parsedConfigVersion = parseInt(val, 1);
                cfg.configVersion = parsedConfigVersion;
            }
            else if (key == "vietnamese_enabled") cfg.vietnameseEnabled = parseBool(val, cfg.vietnameseEnabled);
            else if (key == "input_method") {
                cfg.inputMethod = unescapeTomlString(val);
                if (toLowerAscii(cfg.inputMethod) == "simpletelex") cfg.inputMethod = "Simple Telex";
                else if (toLowerAscii(cfg.inputMethod) == "telex2") cfg.inputMethod = "Telex 2";
                else if (toLowerAscii(cfg.inputMethod) == "telexw") cfg.inputMethod = "Telex W";
            } else if (key == "output_charset") {
                cfg.outputCharset = unescapeTomlString(val);
                if (toLowerAscii(cfg.outputCharset) == "tcvn3") cfg.outputCharset = "TCVN3 (ABC)";
                else if (toLowerAscii(cfg.outputCharset) == "cp1258") cfg.outputCharset = "Windows 1258 codepage";
            }
            else if (key == "autostart") cfg.autostart = parseBool(val, cfg.autostart);
            else if (key == "osd_enabled") cfg.osdEnabled = parseBool(val, cfg.osdEnabled);
            else if (key == "remember_per_app_lang") cfg.rememberPerAppLang = parseBool(val, cfg.rememberPerAppLang);
            else if (key == "bypass_password_dialogs") cfg.bypassPasswordDialogs = parseBool(val, cfg.bypassPasswordDialogs);
            else if (key == "safe_mode") cfg.safeMode = parseBool(val, cfg.safeMode);
        } else if (section == "typing") {
            if (key == "free_tone_marking") cfg.freeToneMarking = parseBool(val, cfg.freeToneMarking);
            else if (key == "modern_tone_style") cfg.modernToneStyle = parseBool(val, cfg.modernToneStyle);
            else if (key == "spellcheck_enabled") cfg.spellCheckEnabled = parseBool(val, cfg.spellCheckEnabled);
            else if (key == "auto_restore_english") cfg.autoRestoreEnglish = parseBool(val, cfg.autoRestoreEnglish);
            else if (key == "shorthand_initial_consonant") cfg.shorthandInitialConsonant = parseBool(val, cfg.shorthandInitialConsonant);
            else if (key == "shorthand_final_consonant") cfg.shorthandFinalConsonant = parseBool(val, cfg.shorthandFinalConsonant);
            else if (key == "macro_enabled") cfg.macroEnabled = parseBool(val, cfg.macroEnabled);
            else if (key == "macro_auto_case") cfg.macroAutoCase = parseBool(val, cfg.macroAutoCase);
        } else if (section == "shortcuts") {
            if (key == "toggle_primary") {
                cfg.shortcuts.togglePrimary = canonicalizeHotkey(unescapeTomlString(val));
                hasNewShortcuts = true;
            } else if (key == "toggle_secondary") {
                cfg.shortcuts.toggleSecondary = canonicalizeHotkey(unescapeTomlString(val));
                hasNewShortcuts = true;
            } else if (key == "toggle_extra") {
                cfg.shortcuts.toggleExtra = canonicalizeHotkey(unescapeTomlString(val));
                hasNewShortcuts = true;
            } else if (key == "restore_word") {
                cfg.shortcuts.restoreWord = canonicalizeHotkey(unescapeTomlString(val));
                hasNewShortcuts = true;
            } else if (key == "panic_safe_mode") {
                cfg.shortcuts.panicSafeMode = canonicalizeHotkey(unescapeTomlString(val));
                hasNewShortcuts = true;
            } else if (key == "toggle_ctrl_shift") {
                hasLegacyCtrlShift = true;
                legacyCtrlShift = parseBool(val, true);
            } else if (key == "toggle_super_space") {
                hasLegacySuperSpace = true;
                legacySuperSpace = parseBool(val, true);
            } else if (key == "toggle_ctrl_space") {
                hasLegacyCtrlSpace = true;
                legacyCtrlSpace = parseBool(val, false);
            } else if (key == "toggle_alt_z") {
                hasLegacyAltZ = true;
                legacyAltZ = parseBool(val, false);
            } else if (key == "restore_on_esc") {
                hasLegacyRestoreEsc = true;
                legacyRestoreEsc = parseBool(val, false);
            } else if (key == "panic_ctrl_alt_pause") {
                hasLegacyPanic = true;
                legacyPanic = parseBool(val, true);
            }
        } else if (section == "output") {
            if (key == "default_method") cfg.defaultOutputMethod = stringToOutputMethod(unescapeTomlString(val));
            else if (key == "default_delay_us") cfg.defaultDelayUs = parseInt(val, cfg.defaultDelayUs);
            else if (key == "clipboard_restore_delay_ms") cfg.clipboardRestoreDelayMs = parseInt(val, cfg.clipboardRestoreDelayMs);
            else if (key == "reset_on_mouse_click") cfg.resetOnMouseClick = parseBool(val, cfg.resetOnMouseClick);
            else if (key == "reset_on_focus_change") cfg.resetOnFocusChange = parseBool(val, cfg.resetOnFocusChange);
            else if (key == "reset_on_navigation_keys") cfg.resetOnNavigationKeys = parseBool(val, cfg.resetOnNavigationKeys);
            else if (key == "buffer_timeout_ms") cfg.bufferTimeoutMs = parseInt(val, cfg.bufferTimeoutMs);
            else if (key == "stats_enabled") cfg.statsEnabled = parseBool(val, cfg.statsEnabled);
            else if (key == "debug_redacted") cfg.debugRedacted = parseBool(val, cfg.debugRedacted);
        } else if (section == "macros") {
            std::string rep = unescapeTomlString(val);
            if (!key.empty()) {
                cfg.macros[key] = rep;
            }
        } else if (section == "app_profiles" && !cfg.appProfiles.empty()) {
            auto& prof = cfg.appProfiles.back();
            if (key == "app_class") prof.appClass = unescapeTomlString(val);
            else if (key == "mode") prof.mode = stringToAppMode(unescapeTomlString(val));
            else if (key == "output_method") prof.outputMethod = stringToOutputMethod(unescapeTomlString(val));
            else if (key == "use_custom_output_method") prof.useCustomOutputMethod = parseBool(val, prof.useCustomOutputMethod);
            else if (key == "delay_us") prof.delayUs = parseInt(val, prof.delayUs);
            else if (key == "autocomplete_fix") prof.autocompleteFix = parseBool(val, prof.autocompleteFix);
        }
    }

    if (!hasNewShortcuts && (hasLegacyCtrlShift || hasLegacySuperSpace || hasLegacyCtrlSpace ||
                             hasLegacyAltZ || hasLegacyRestoreEsc || hasLegacyPanic)) {
        std::vector<std::string> toggles;
        if (legacyCtrlShift) toggles.push_back("Ctrl+Shift");
        if (legacySuperSpace) toggles.push_back("Super+Space");
        if (legacyCtrlSpace) toggles.push_back("Ctrl+Space");
        if (legacyAltZ) toggles.push_back("Alt+Z");
        cfg.shortcuts.togglePrimary = toggles.size() > 0 ? toggles[0] : "";
        cfg.shortcuts.toggleSecondary = toggles.size() > 1 ? toggles[1] : "";
        cfg.shortcuts.toggleExtra = toggles.size() > 2 ? toggles[2] : "";
        if (hasLegacyRestoreEsc) {
            cfg.shortcuts.restoreWord = legacyRestoreEsc ? "Esc" : "";
        }
        if (hasLegacyPanic) {
            cfg.shortcuts.panicSafeMode = legacyPanic ? "Ctrl+Alt+Pause" : "";
        }
    }

    if (parsedConfigVersion < 2) {
        if (clearedDefaultProfiles && cfg.findProfileForApp("minecraft") == nullptr) {
            cfg.appProfiles.push_back(makeDefaultMinecraftProfile());
        }
    }
    if (parsedConfigVersion < 4) {
        if (cfg.defaultDelayUs == 0) {
            cfg.defaultDelayUs = 1200;
        }
        auto isDefaultBrowser = [](const std::string& cls) {
            for (const char* b : {"firefox", "chromium", "google-chrome", "brave", "vivaldi", "microsoft-edge", "opera"}) {
                if (cls == b) return true;
            }
            return false;
        };
        for (auto& prof : cfg.appProfiles) {
            if (isDefaultBrowser(prof.appClass) && prof.mode == AppMode::Default &&
                !prof.useCustomOutputMethod && prof.delayUs == 0) {
                prof.autocompleteFix = true;
            }
        }
    }
    if (parsedConfigVersion < 5) {
        if (cfg.defaultDelayUs == 1200) {
            cfg.defaultDelayUs = 0;
        }
    }
    if (parsedConfigVersion < kCurrentConfigVersion) {
        cfg.configVersion = kCurrentConfigVersion;
    }

    return cfg;
}

std::string Config::defaultConfigDir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        return std::string(xdg) + "/vnsnowkey";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.config/vnsnowkey";
    }
    return "/tmp/vnsnowkey";
}

std::string Config::defaultConfigPath() {
    return defaultConfigDir() + "/config.toml";
}

bool Config::saveToFile(const std::string& path, std::string* errorMsg) const {
    std::string targetPath = path.empty() ? defaultConfigPath() : path;
    try {
        std::filesystem::path p(targetPath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
        std::ofstream ofs(targetPath, std::ios::trunc);
        if (!ofs.is_open()) {
            if (errorMsg) *errorMsg = "Cannot open config file for writing: " + targetPath;
            return false;
        }
        ofs << toToml();
        return ofs.good();
    } catch (const std::exception& ex) {
        if (errorMsg) *errorMsg = ex.what();
        return false;
    }
}

Config Config::loadFromFile(const std::string& path, bool* loadedOk) {
    std::string targetPath = path.empty() ? defaultConfigPath() : path;
    std::ifstream ifs(targetPath);
    if (!ifs.is_open()) {
        if (loadedOk) *loadedOk = false;
        Config def = Config::createDefault();
        def.saveToFile(targetPath);
        return def;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string content = oss.str();
    if (loadedOk) *loadedOk = true;
    Config cfg = fromToml(content);
    if ((path.empty() || targetPath == defaultConfigPath()) &&
        (content.find("config_version") == std::string::npos ||
         content.find("config_version = " + std::to_string(kCurrentConfigVersion)) == std::string::npos)) {
        cfg.saveToFile(targetPath);
    }
    return cfg;
}

} // namespace vnsnowkey
