/*
 * VNSnowKey - Vietnamese Input Method for Arch Linux + KDE Plasma (Wayland)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdint>
#include <linux/input-event-codes.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vnsnowkey {

// Use dedicated non-media evdev keycodes unbound in xkb symbols/inet so GNOME/KDE/compositors
// never trigger hardware OSD popups (unlike KEY_F23 which inet maps to XF86TouchpadOff).
inline constexpr uint16_t kVNSnowKeyLevel3Evdev = KEY_ISO;  // evdev 170 -> XKB <I178>
inline constexpr uint16_t kVNSnowKeyLevel5Evdev = KEY_EDIT; // evdev 176 -> XKB <I184>

struct KeyChord {
    uint16_t keycode = 0;   // Linux evdev keycode (e.g. KEY_A = 30)
    bool shift = false;     // Hold KEY_LEFTSHIFT
    bool level3 = false;    // Hold kVNSnowKeyLevel3Evdev (ISO_Level3_Shift)
    bool level5 = false;    // Hold kVNSnowKeyLevel5Evdev (ISO_Level5_Shift)
};

class XkbOverlay {
public:
    XkbOverlay();

    // Look up the exact uinput KeyChord for any ASCII or Vietnamese Unicode character
    std::optional<KeyChord> lookupChar(char32_t cp) const;

    // Generate the complete content of ~/.config/xkb/symbols/us
    std::string generateSymbolsUs() const;

    // Generate the complete content of ~/.config/xkb/types/level5 (CapsLock-safe Levels 3..8)
    std::string generateTypesLevel5() const;

    // Install XKB overlay files into xkbConfigDir (defaults to ~/.config/xkb)
    bool installOverlayFiles(const std::string& xkbConfigDir = "", std::string* errorMsg = nullptr) const;

    // Notify the active compositor/desktop (KWin, Hyprland, Sway, GNOME, X11) to reload XKB keymap
    static bool reloadCompositorXkb();

    // Backward-compatible wrapper calling reloadCompositorXkb()
    static bool reloadKWinXkb();

    // Check if ~/.config/xkb/symbols/us is installed and contains VNSnowKey's marker
    static bool isOverlayInstalled(const std::string& xkbConfigDir = "");

    static std::string defaultXkbConfigDir();

private:
    struct AlphaKeySlot {
        const char* xkbName;
        uint16_t evdevCode;
        char lowerAscii;
        char upperAscii;
        // Up to 3 pairs of (lowerVN, upperVN) on Levels (3,4), (5,6), (7,8)
        char32_t vnLower[3] = {0, 0, 0};
        char32_t vnUpper[3] = {0, 0, 0};
    };

    std::vector<AlphaKeySlot> alphaSlots_;
    std::unordered_map<char32_t, KeyChord> charToChord_;
};

} // namespace vnsnowkey
