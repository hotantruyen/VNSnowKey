/*
 * VNSnowKey - Vietnamese Input Method for Arch Linux + KDE Plasma (Wayland)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "../engine/session_processor.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace vnsnowkey {

enum class OutputMethod : int {
    XkbKeymap = 0,           // Primary: ~/.config/xkb virtual keymap overlay + atomic uinput keycodes
    ClipboardCtrlV = 1,      // Fallback: Save clipboard -> set UTF-8 -> Ctrl+V -> restore clipboard
    ClipboardCtrlShiftV = 2, // Terminal fallback: Save clipboard -> set UTF-8 -> Ctrl+Shift+V -> restore
    CtrlShiftU = 3           // GTK/Chromium fallback: Ctrl+Shift+U + hex digits + Enter
};

enum class AppMode : int {
    Default = 0,   // Follow global/remembered language
    AlwaysVi = 1,  // Switch to VI when focused
    AlwaysEn = 2,  // Switch to EN when focused
    Excluded = 3,  // Bypass VNSnowKey completely (raw pass-through)
    GameMode = 4   // Game Mode: raw pass-through in 3D, VI typing when chat/UI cursor/text-input active
};

struct AppProfile {
    std::string appClass;
    AppMode mode = AppMode::Default;
    OutputMethod outputMethod = OutputMethod::XkbKeymap;
    bool useCustomOutputMethod = false;
    int delayUs = 0;
    bool autocompleteFix = false;
};

constexpr uint8_t kModCtrl  = 1u << 0;
constexpr uint8_t kModAlt   = 1u << 1;
constexpr uint8_t kModShift = 1u << 2;
constexpr uint8_t kModSuper = 1u << 3;

struct ParsedHotkey {
    uint8_t modifiers = 0;
    uint16_t keyCode = 0; // Linux evdev KEY_*; 0 when modifier-only chord (e.g. Ctrl+Shift)

    bool isModOnly() const { return keyCode == 0 && modifiers != 0; }
    bool isValid() const {
        if (keyCode != 0) return true;
        int modCount = ((modifiers & kModCtrl) ? 1 : 0) +
                       ((modifiers & kModAlt) ? 1 : 0) +
                       ((modifiers & kModShift) ? 1 : 0) +
                       ((modifiers & kModSuper) ? 1 : 0);
        return modCount >= 2;
    }
};

struct ShortcutConfig {
    std::string togglePrimary = "Ctrl+Shift";
    std::string toggleSecondary = "Super+Space";
    std::string toggleExtra = "";
    std::string restoreWord = "";
    std::string panicSafeMode = "Ctrl+Alt+Pause";
};

struct Config {
    static constexpr int kCurrentConfigVersion = 2;

    // [general]
    int configVersion = kCurrentConfigVersion;
    bool vietnameseEnabled = true;
    std::string inputMethod = "Telex";
    std::string outputCharset = "Unicode";
    bool autostart = true;
    bool osdEnabled = true;
    bool rememberPerAppLang = true;
    bool bypassPasswordDialogs = true;
    bool safeMode = false;

    // [typing]
    bool freeToneMarking = true;
    bool modernToneStyle = true; // true: hoà, thuỷ; false: hòa, thủy
    bool spellCheckEnabled = true;
    bool autoRestoreEnglish = true;
    bool shorthandInitialConsonant = false;
    bool shorthandFinalConsonant = false;
    bool macroEnabled = true;
    bool macroAutoCase = true;

    // [shortcuts]
    ShortcutConfig shortcuts;

    // [output]
    OutputMethod defaultOutputMethod = OutputMethod::XkbKeymap;
    int defaultDelayUs = 0;
    int clipboardRestoreDelayMs = 45;
    bool resetOnMouseClick = true;
    bool resetOnFocusChange = true;
    bool resetOnNavigationKeys = true;
    int bufferTimeoutMs = 15000;
    bool statsEnabled = false;
    bool debugRedacted = false;

    // [macros]
    std::map<std::string, std::string> macros;

    // [[app_profiles]]
    std::vector<AppProfile> appProfiles;

    // Convert to SessionOptions
    bamboo::SessionOptions toSessionOptions() const;

    // Find matching AppProfile for an application class/name (case-insensitive substring/exact match)
    const AppProfile* findProfileForApp(const std::string& appClass) const;

    // Default configuration with sensible defaults for Arch Linux + KDE Plasma
    static Config createDefault();

    // Serialization (TOML format at ~/.config/vnsnowkey/config.toml)
    std::string toToml() const;
    static Config fromToml(std::string_view tomlText);

    // Load / Save from disk
    static std::string defaultConfigDir();
    static std::string defaultConfigPath();
    bool saveToFile(const std::string& path = "", std::string* errorMsg = nullptr) const;
    static Config loadFromFile(const std::string& path = "", bool* loadedOk = nullptr);

    static std::string outputMethodToString(OutputMethod m);
    static OutputMethod stringToOutputMethod(const std::string& s);
    static std::string appModeToString(AppMode m);
    static AppMode stringToAppMode(const std::string& s);

    // Hotkey parsing & formatting helpers
    static ParsedHotkey parseHotkey(std::string_view spec);
    static std::string formatHotkey(uint8_t modifiers, uint16_t keyCode);
    static std::string canonicalizeHotkey(std::string_view spec);
    static uint16_t hotkeyNameToEvdevCode(std::string_view keyName);
    static std::string evdevCodeToHotkeyName(uint16_t code);
};

} // namespace vnsnowkey
