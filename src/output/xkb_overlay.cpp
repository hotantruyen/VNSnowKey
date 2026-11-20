/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "xkb_overlay.hpp"
#include "../engine/bamboo_utils.hpp"
#include "../focus/xwayland_env.hpp"
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <linux/input-event-codes.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace vnsnowkey {

namespace {

std::string formatKeysym(char32_t cp) {
    if (cp == 0) return "NoSymbol";
    std::ostringstream oss;
    oss << "U" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << static_cast<uint32_t>(cp);
    return oss.str();
}

std::string trimAscii(std::string s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        --end;
    }
    return s.substr(start, end - start);
}

std::string toLowerAsciiStr(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return s;
}

bool hasExecutableInPath(const char* cmdName) {
    if (!cmdName || !*cmdName) return false;
    std::string cmd = std::string("command -v ") + cmdName + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

bool runPipeRead(const char* cmd, std::string& out, int* exitCode = nullptr) {
    out.clear();
    FILE* pipe = ::popen(cmd, "r");
    if (!pipe) return false;
    char buf[1024];
    while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
        out += buf;
    }
    int status = ::pclose(pipe);
    int rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (exitCode) *exitCode = rc;
    return rc == 0;
}

XkbCompositorHint detectCompositorFromEnv(XkbCompositorHint hint) {
    if (hint != XkbCompositorHint::Auto) {
        return hint;
    }
    if (const char* hypr = std::getenv("HYPRLAND_INSTANCE_SIGNATURE"); hypr && *hypr) {
        return XkbCompositorHint::Hyprland;
    }
    if (const char* sway = std::getenv("SWAYSOCK"); sway && *sway) {
        return XkbCompositorHint::Sway;
    }
    std::string deskStr;
    if (const char* desktop = std::getenv("XDG_CURRENT_DESKTOP")) {
        deskStr = toLowerAsciiStr(desktop);
    }
    if (deskStr.find("hyprland") != std::string::npos) return XkbCompositorHint::Hyprland;
    if (deskStr.find("sway") != std::string::npos) return XkbCompositorHint::Sway;
    if (deskStr.find("gnome") != std::string::npos || std::getenv("GNOME_DESKTOP_SESSION_ID") != nullptr) {
        return XkbCompositorHint::GnomeShell;
    }
    if (deskStr.find("kde") != std::string::npos || deskStr.find("plasma") != std::string::npos ||
        std::getenv("KDE_FULL_SESSION") != nullptr) {
        return XkbCompositorHint::KWin;
    }
    if (!XkbOverlay::isWaylandSession()) {
        return XkbCompositorHint::X11;
    }
    return XkbCompositorHint::Auto;
}

} // namespace

XkbOverlay::XkbOverlay() {
    alphaSlots_ = {
        {"<AD01>", KEY_Q, 'q', 'Q'},
        {"<AD02>", KEY_W, 'w', 'W'},
        {"<AD03>", KEY_E, 'e', 'E'},
        {"<AD04>", KEY_R, 'r', 'R'},
        {"<AD05>", KEY_T, 't', 'T'},
        {"<AD06>", KEY_Y, 'y', 'Y'},
        {"<AD07>", KEY_U, 'u', 'U'},
        {"<AD08>", KEY_I, 'i', 'I'},
        {"<AD09>", KEY_O, 'o', 'O'},
        {"<AD10>", KEY_P, 'p', 'P'},
        {"<AC01>", KEY_A, 'a', 'A'},
        {"<AC02>", KEY_S, 's', 'S'},
        {"<AC03>", KEY_D, 'd', 'D'},
        {"<AC04>", KEY_F, 'f', 'F'},
        {"<AC05>", KEY_G, 'g', 'G'},
        {"<AC06>", KEY_H, 'h', 'H'},
        {"<AC07>", KEY_J, 'j', 'J'},
        {"<AC08>", KEY_K, 'k', 'K'},
        {"<AC09>", KEY_L, 'l', 'L'},
        {"<AB01>", KEY_Z, 'z', 'Z'},
        {"<AB02>", KEY_X, 'x', 'X'},
        {"<AB03>", KEY_C, 'c', 'C'},
        {"<AB04>", KEY_V, 'v', 'V'},
        {"<AB05>", KEY_B, 'b', 'B'},
        {"<AB06>", KEY_N, 'n', 'N'},
        {"<AB07>", KEY_M, 'm', 'M'},
    };

    // 1. Register all 26 ASCII letters (lowercase and uppercase)
    for (const auto& slot : alphaSlots_) {
        charToChord_[static_cast<char32_t>(slot.lowerAscii)] = KeyChord{slot.evdevCode, false, false, false};
        charToChord_[static_cast<char32_t>(slot.upperAscii)] = KeyChord{slot.evdevCode, true, false, false};
    }

    // 2. Register all non-alphabetic printable ASCII characters on standard US QWERTY
    struct AsciiSym {
        uint16_t code;
        char normal;
        char shifted;
    };
    static const AsciiSym kAsciiSyms[] = {
        {KEY_GRAVE,      '`',  '~'},
        {KEY_1,          '1',  '!'},
        {KEY_2,          '2',  '@'},
        {KEY_3,          '3',  '#'},
        {KEY_4,          '4',  '$'},
        {KEY_5,          '5',  '%'},
        {KEY_6,          '6',  '^'},
        {KEY_7,          '7',  '&'},
        {KEY_8,          '8',  '*'},
        {KEY_9,          '9',  '('},
        {KEY_0,          '0',  ')'},
        {KEY_MINUS,      '-',  '_'},
        {KEY_EQUAL,      '=',  '+'},
        {KEY_LEFTBRACE,  '[',  '{'},
        {KEY_RIGHTBRACE, ']',  '}'},
        {KEY_BACKSLASH,  '\\', '|'},
        {KEY_SEMICOLON,  ';',  ':'},
        {KEY_APOSTROPHE, '\'', '"'},
        {KEY_COMMA,      ',',  '<'},
        {KEY_DOT,        '.',  '>'},
        {KEY_SLASH,      '/',  '?'},
    };
    for (const auto& s : kAsciiSyms) {
        charToChord_[static_cast<char32_t>(s.normal)]  = KeyChord{s.code, false, false, false};
        charToChord_[static_cast<char32_t>(s.shifted)] = KeyChord{s.code, true,  false, false};
    }
    charToChord_[U' ']  = KeyChord{KEY_SPACE, false, false, false};
    charToChord_[U'\n'] = KeyChord{KEY_ENTER, false, false, false};
    charToChord_[U'\t'] = KeyChord{KEY_TAB,   false, false, false};

    // 3. Assign all 67 (lowercase, uppercase) Vietnamese character pairs across alphaSlots_
    const auto& allVn = bamboo::getAllVietnameseChars(); // interleaved [lo0, up0, lo1, up1, ...]
    size_t pairIdx = 0;
    for (size_t i = 0; i + 1 < allVn.size(); i += 2) {
        char32_t lo = allVn[i];
        char32_t up = allVn[i + 1];
        size_t keyIdx = pairIdx / 3;
        size_t subIdx = pairIdx % 3;
        if (keyIdx < alphaSlots_.size()) {
            alphaSlots_[keyIdx].vnLower[subIdx] = lo;
            alphaSlots_[keyIdx].vnUpper[subIdx] = up;

            bool l3 = (subIdx == 0 || subIdx == 2);
            bool l5 = (subIdx == 1 || subIdx == 2);
            uint16_t code = alphaSlots_[keyIdx].evdevCode;
            charToChord_[lo] = KeyChord{code, false, l3, l5};
            charToChord_[up] = KeyChord{code, true,  l3, l5};
        }
        ++pairIdx;
    }
}

std::optional<KeyChord> XkbOverlay::lookupChar(char32_t cp) const {
    auto it = charToChord_.find(cp);
    if (it != charToChord_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string XkbOverlay::generateSymbolsUs() const {
    std::ostringstream oss;
    oss << "// VNSNOWKEY_XKB_OVERLAY_V2\n";
    oss << "// Auto-generated by VNSnowKey for zero-flicker, clipboard-free Vietnamese Unicode input on Wayland/X11.\n";
    oss << "// Levels 1 & 2 are 100% standard US QWERTY. Levels 3..8 map all 134 Vietnamese Unicode characters\n";
    oss << "// via <I178> (KEY_ISO -> ISO_Level3_Shift) and <I184> (KEY_EDIT -> ISO_Level5_Shift).\n\n";
    oss << "default partial alphanumeric_keys modifier_keys\n";
    oss << "xkb_symbols \"basic\" {\n\n";
    oss << "    name[Group1]= \"English (US)\";\n\n";
    oss << "    key <TLDE> {[ grave,        asciitilde   ]};\n";
    oss << "    key <AE01> {[ 1,            exclam       ]};\n";
    oss << "    key <AE02> {[ 2,            at           ]};\n";
    oss << "    key <AE03> {[ 3,            numbersign   ]};\n";
    oss << "    key <AE04> {[ 4,            dollar       ]};\n";
    oss << "    key <AE05> {[ 5,            percent      ]};\n";
    oss << "    key <AE06> {[ 6,            asciicircum  ]};\n";
    oss << "    key <AE07> {[ 7,            ampersand    ]};\n";
    oss << "    key <AE08> {[ 8,            asterisk     ]};\n";
    oss << "    key <AE09> {[ 9,            parenleft    ]};\n";
    oss << "    key <AE10> {[ 0,            parenright   ]};\n";
    oss << "    key <AE11> {[ minus,        underscore   ]};\n";
    oss << "    key <AE12> {[ equal,        plus         ]};\n\n";
    oss << "    key <AD11> {[ bracketleft,  braceleft    ]};\n";
    oss << "    key <AD12> {[ bracketright, braceright   ]};\n";
    oss << "    key <AC10> {[ semicolon,    colon        ]};\n";
    oss << "    key <AC11> {[ apostrophe,   quotedbl     ]};\n";
    oss << "    key <BKSL> {[ backslash,    bar          ]};\n";
    oss << "    key <AB08> {[ comma,        less         ]};\n";
    oss << "    key <AB09> {[ period,       greater      ]};\n";
    oss << "    key <AB10> {[ slash,        question     ]};\n\n";

    for (const auto& slot : alphaSlots_) {
        if (slot.vnLower[0] == 0) {
            oss << "    key " << slot.xkbName << " {[ " << slot.lowerAscii << ", " << slot.upperAscii << " ]};\n";
        } else {
            oss << "    key " << slot.xkbName << " {\n";
            oss << "        type[Group1] = \"EIGHT_LEVEL_ALPHABETIC\",\n";
            oss << "        symbols[Group1] = [ "
                << slot.lowerAscii << ", " << slot.upperAscii << ", "
                << formatKeysym(slot.vnLower[0]) << ", " << formatKeysym(slot.vnUpper[0]) << ", "
                << formatKeysym(slot.vnLower[1]) << ", " << formatKeysym(slot.vnUpper[1]) << ", "
                << formatKeysym(slot.vnLower[2]) << ", " << formatKeysym(slot.vnUpper[2]) << " ]\n";
            oss << "    };\n";
        }
    }

    oss << "\n    // Dedicated virtual modifiers for VNSnowKey uinput keyboard\n";
    oss << "    key <I178> {\n";
    oss << "        type[Group1] = \"ONE_LEVEL\",\n";
    oss << "        symbols[Group1] = [ ISO_Level3_Shift ]\n";
    oss << "    };\n";
    oss << "    modifier_map Mod5 { <I178> };\n\n";
    oss << "    key <I184> {\n";
    oss << "        type[Group1] = \"ONE_LEVEL\",\n";
    oss << "        symbols[Group1] = [ ISO_Level5_Shift ]\n";
    oss << "    };\n";
    oss << "    modifier_map Mod3 { <I184> };\n";
    oss << "};\n";
    return oss.str();
}

std::string XkbOverlay::generateTypesLevel5() const {
    // Read system /usr/share/X11/xkb/types/level5 if available and adjust EIGHT_LEVEL_ALPHABETIC
    // so CapsLock (Lock) does not invert case on Levels 3..8 while keeping normal CapsLock on Levels 1..2.
    std::ifstream ifs("/usr/share/X11/xkb/types/level5");
    if (ifs.is_open()) {
        std::ostringstream buf;
        buf << ifs.rdbuf();
        std::string content = buf.str();
        const std::string oldBlock =
            "\tmap[LevelThree] = Level3;\n"
            "\tmap[Shift+LevelThree] = Level4;\n"
            "\tmap[Lock+LevelThree] = Level4;\n"
            "\tmap[Shift+Lock+LevelThree] = Level3;\n\n"
            "\tmap[LevelFive] = Level5;\n"
            "\tmap[Shift+LevelFive] = Level6;\n"
            "\tmap[Lock+LevelFive] = Level6;\n"
            "\tmap[Shift+Lock+LevelFive] = Level5;\n"
            "\tmap[LevelThree+LevelFive] = Level7;\n"
            "\tmap[Shift+LevelThree+LevelFive] = Level8;\n"
            "\tmap[Lock+LevelThree+LevelFive] = Level8;\n"
            "\tmap[Shift+Lock+LevelThree+LevelFive] = Level7;";
        const std::string newBlock =
            "\tmap[LevelThree] = Level3;\n"
            "\tmap[Shift+LevelThree] = Level4;\n"
            "\tmap[Lock+LevelThree] = Level3;\n"
            "\tmap[Shift+Lock+LevelThree] = Level4;\n\n"
            "\tmap[LevelFive] = Level5;\n"
            "\tmap[Shift+LevelFive] = Level6;\n"
            "\tmap[Lock+LevelFive] = Level5;\n"
            "\tmap[Shift+Lock+LevelFive] = Level6;\n"
            "\tmap[LevelThree+LevelFive] = Level7;\n"
            "\tmap[Shift+LevelThree+LevelFive] = Level8;\n"
            "\tmap[Lock+LevelThree+LevelFive] = Level7;\n"
            "\tmap[Shift+Lock+LevelThree+LevelFive] = Level8;";
        size_t pos = content.find(oldBlock);
        if (pos != std::string::npos) {
            content.replace(pos, oldBlock.size(), newBlock);
            return "// VNSNOWKEY_XKB_TYPES_V1\n" + content;
        }
    }

    // Fallback self-contained types/level5 definition
    return R"(// VNSNOWKEY_XKB_TYPES_V1
default partial xkb_types "default" {
    virtual_modifiers LevelThree, LevelFive, NumLock;

    type "EIGHT_LEVEL" {
        modifiers = Shift + LevelThree + LevelFive;
        map[None] = Level1;
        map[Shift] = Level2;
        map[LevelThree] = Level3;
        map[Shift+LevelThree] = Level4;
        map[LevelFive] = Level5;
        map[Shift+LevelFive] = Level6;
        map[LevelThree+LevelFive] = Level7;
        map[Shift+LevelThree+LevelFive] = Level8;
        level_name[Level1] = "Base";
        level_name[Level2] = "Shift";
        level_name[Level3] = "AltGr";
        level_name[Level4] = "Shift AltGr";
        level_name[Level5] = "X";
        level_name[Level6] = "X Shift";
        level_name[Level7] = "X AltGr";
        level_name[Level8] = "X Shift AltGr";
    };

    type "EIGHT_LEVEL_ALPHABETIC" {
        modifiers = Shift + Lock + LevelThree + LevelFive;
        map[None] = Level1;
        map[Shift] = Level2;
        map[Lock] = Level2;
        map[Shift+Lock] = Level1;
        map[LevelThree] = Level3;
        map[Shift+LevelThree] = Level4;
        map[Lock+LevelThree] = Level3;
        map[Shift+Lock+LevelThree] = Level4;
        map[LevelFive] = Level5;
        map[Shift+LevelFive] = Level6;
        map[Lock+LevelFive] = Level5;
        map[Shift+Lock+LevelFive] = Level6;
        map[LevelThree+LevelFive] = Level7;
        map[Shift+LevelThree+LevelFive] = Level8;
        map[Lock+LevelThree+LevelFive] = Level7;
        map[Shift+Lock+LevelThree+LevelFive] = Level8;
        level_name[Level1] = "Base";
        level_name[Level2] = "Shift";
        level_name[Level3] = "AltGr";
        level_name[Level4] = "Shift AltGr";
        level_name[Level5] = "X";
        level_name[Level6] = "X Shift";
        level_name[Level7] = "X AltGr";
        level_name[Level8] = "X Shift AltGr";
    };
};
)";
}

std::string XkbOverlay::defaultXkbConfigDir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        return std::string(xdg) + "/xkb";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.config/xkb";
    }
    return "/tmp/xkb";
}

bool XkbOverlay::isOverlayInstalled(const std::string& xkbConfigDir) {
    std::string dir = xkbConfigDir.empty() ? defaultXkbConfigDir() : xkbConfigDir;
    std::ifstream ifs(dir + "/symbols/us");
    if (!ifs.is_open()) return false;
    std::string firstLine;
    std::getline(ifs, firstLine);
    return firstLine.find("VNSNOWKEY_XKB_OVERLAY_V2") != std::string::npos;
}

bool XkbOverlay::installOverlayFiles(const std::string& xkbConfigDir, std::string* errorMsg) const {
    std::string dir = xkbConfigDir.empty() ? defaultXkbConfigDir() : xkbConfigDir;
    try {
        std::filesystem::create_directories(dir + "/symbols");
        std::filesystem::create_directories(dir + "/types");

        {
            std::ofstream ofsSym(dir + "/symbols/us", std::ios::trunc);
            if (!ofsSym.is_open()) {
                if (errorMsg) *errorMsg = "Cannot write " + dir + "/symbols/us";
                return false;
            }
            ofsSym << generateSymbolsUs();
        }
        {
            std::ofstream ofsTyp(dir + "/types/level5", std::ios::trunc);
            if (!ofsTyp.is_open()) {
                if (errorMsg) *errorMsg = "Cannot write " + dir + "/types/level5";
                return false;
            }
            ofsTyp << generateTypesLevel5();
        }
        return true;
    } catch (const std::exception& ex) {
        if (errorMsg) *errorMsg = ex.what();
        return false;
    }
}

bool XkbOverlay::isWaylandSession() {
    if (xwayland_env::isIsolatedTestEnv()) {
        return false;
    }
    if (const char* sessionType = std::getenv("XDG_SESSION_TYPE")) {
        std::string st = toLowerAsciiStr(sessionType);
        if (st == "x11") return false;
        if (st == "wayland") return true;
    }
    if (const char* waylandDisp = std::getenv("WAYLAND_DISPLAY"); waylandDisp && waylandDisp[0] != '\0') {
        return true;
    }
    return !xwayland_env::discoverWaylandSocketPath().empty();
}

std::string XkbOverlay::resolveX11Display() {
    if (xwayland_env::isIsolatedTestEnv()) {
        return {};
    }
    if (const char* x11Disp = std::getenv("DISPLAY"); x11Disp && x11Disp[0] != '\0') {
        xwayland_env::ensureXAuthorityEnv(false);
        return std::string(x11Disp);
    }
    std::string discovered = xwayland_env::discoverX11SocketDisplay();
    if (!discovered.empty()) {
        ::setenv("DISPLAY", discovered.c_str(), 0);
        xwayland_env::ensureXAuthorityEnv(false);
    }
    return discovered;
}

std::optional<bool> XkbOverlay::queryX11CapsLockState() {
    if (xwayland_env::isIsolatedTestEnv() || isWaylandSession()) {
        return std::nullopt;
    }
    std::string disp = resolveX11Display();
    if (disp.empty()) {
        return std::nullopt;
    }
    void* libX11 = ::dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (!libX11) {
        return std::nullopt;
    }
    auto pXOpenDisplay = reinterpret_cast<void* (*)(const char*)>(::dlsym(libX11, "XOpenDisplay"));
    auto pXCloseDisplay = reinterpret_cast<int (*)(void*)>(::dlsym(libX11, "XCloseDisplay"));
    auto pXkbGetIndicatorState = reinterpret_cast<int (*)(void*, unsigned int, unsigned int*)>(
        ::dlsym(libX11, "XkbGetIndicatorState"));
    std::optional<bool> result;
    if (pXOpenDisplay && pXCloseDisplay && pXkbGetIndicatorState) {
        if (void* dpy = pXOpenDisplay(disp.c_str())) {
            unsigned int state = 0;
            // XkbUseCoreKbd = 0x0100; bit 0 is Caps Lock indicator
            if (pXkbGetIndicatorState(dpy, 0x0100u, &state) == 1) {
                result = ((state & 1u) != 0);
            }
            pXCloseDisplay(dpy);
        }
    }
    ::dlclose(libX11);
    return result;
}

bool XkbOverlay::keymapTextHasVnsnowkeyOverlay(const std::string& dumpedKeymap, std::string* missingDetails) {
    const bool hasI178 = dumpedKeymap.find("<I178>") != std::string::npos;
    const bool hasI184 = dumpedKeymap.find("<I184>") != std::string::npos;
    const bool hasType = dumpedKeymap.find("EIGHT_LEVEL_ALPHABETIC") != std::string::npos;
    const bool hasVnSym = dumpedKeymap.find("agrave") != std::string::npos ||
                          dumpedKeymap.find("U00E0") != std::string::npos ||
                          dumpedKeymap.find("U00e0") != std::string::npos;
    if (hasI178 && hasI184 && hasType && hasVnSym) {
        if (missingDetails) missingDetails->clear();
        return true;
    }
    if (missingDetails) {
        std::vector<std::string> missing;
        if (!hasI178) missing.push_back("<I178>");
        if (!hasI184) missing.push_back("<I184>");
        if (!hasType) missing.push_back("EIGHT_LEVEL_ALPHABETIC");
        if (!hasVnSym) missing.push_back("agrave");
        std::ostringstream oss;
        for (size_t i = 0; i < missing.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << missing[i];
        }
        *missingDetails = oss.str();
    }
    return false;
}

XkbLiveStatus XkbOverlay::probeX11KeymapStatus(std::string* missingDetails) {
    if (xwayland_env::isIsolatedTestEnv()) {
        return XkbLiveStatus::WaylandUnverifiedNoXDisplay;
    }
    std::string disp = resolveX11Display();
    if (disp.empty()) {
        if (isWaylandSession()) {
            return XkbLiveStatus::WaylandUnverifiedNoXDisplay;
        }
        if (missingDetails) *missingDetails = "Không tìm thấy X11 DISPLAY";
        return XkbLiveStatus::InactiveOverlayMissing;
    }
    if (!hasExecutableInPath("xkbcomp")) {
        if (missingDetails) *missingDetails = "Thiếu lệnh 'xkbcomp' (cài gói x11-xkb-utils / xorg-xkbcomp / xkbcomp)";
        return XkbLiveStatus::ToolMissing;
    }
    std::string dumped;
    int rc = -1;
    std::string cmd = "DISPLAY='" + disp + "' xkbcomp \"$DISPLAY\" - 2>/dev/null";
    if (!runPipeRead(cmd.c_str(), dumped, &rc) || dumped.empty()) {
        if (isWaylandSession()) {
            return XkbLiveStatus::WaylandUnverifiedNoXDisplay;
        }
        if (missingDetails) *missingDetails = "xkbcomp không đọc được keymap từ DISPLAY=" + disp;
        return XkbLiveStatus::InactiveOverlayMissing;
    }
    if (keymapTextHasVnsnowkeyOverlay(dumped, missingDetails)) {
        return XkbLiveStatus::VerifiedActive;
    }
    return XkbLiveStatus::InactiveOverlayMissing;
}

bool XkbOverlay::parseGnomeInputSources(const std::string& gsettingsOutput, std::string& outLayout, std::string& outVariant) {
    outLayout.clear();
    outVariant.clear();
    // Format example: "[('xkb', 'us')]" or "[('xkb', 'us+intl'), ('ibus', 'unikey')]"
    size_t pos = gsettingsOutput.find("'xkb'");
    if (pos == std::string::npos) {
        return false;
    }
    size_t comma = gsettingsOutput.find(',', pos);
    if (comma == std::string::npos) return false;
    size_t q1 = gsettingsOutput.find('\'', comma);
    if (q1 == std::string::npos) return false;
    size_t q2 = gsettingsOutput.find('\'', q1 + 1);
    if (q2 == std::string::npos) return false;
    std::string val = trimAscii(gsettingsOutput.substr(q1 + 1, q2 - q1 - 1));
    if (val.empty()) return false;
    size_t plus = val.find('+');
    if (plus == std::string::npos) {
        outLayout = val;
    } else {
        outLayout = val.substr(0, plus);
        outVariant = val.substr(plus + 1);
    }
    return true;
}

bool XkbOverlay::parseKxkbrcContent(const std::string& content, std::string& outLayout, std::string& outVariant) {
    outLayout.clear();
    outVariant.clear();
    std::istringstream iss(content);
    std::string line;
    bool inLayoutGroup = false;
    bool useCustom = false;
    std::string layoutList;
    std::string variantList;
    while (std::getline(iss, line)) {
        std::string t = trimAscii(line);
        if (t.empty() || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']') {
            inLayoutGroup = (t == "[Layout]");
            continue;
        }
        if (!inLayoutGroup) continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trimAscii(t.substr(0, eq));
        std::string v = trimAscii(t.substr(eq + 1));
        if (k == "Use") {
            std::string lo = toLowerAsciiStr(v);
            useCustom = (lo == "true" || lo == "1");
        } else if (k == "LayoutList") {
            layoutList = v;
        } else if (k == "VariantList") {
            variantList = v;
        }
    }
    if (!useCustom || layoutList.empty()) {
        outLayout = "us";
        return true;
    }
    size_t c1 = layoutList.find(',');
    outLayout = trimAscii(c1 == std::string::npos ? layoutList : layoutList.substr(0, c1));
    if (!variantList.empty()) {
        size_t c2 = variantList.find(',');
        outVariant = trimAscii(c2 == std::string::npos ? variantList : variantList.substr(0, c2));
    }
    return !outLayout.empty();
}

bool XkbOverlay::parseSetxkbmapQuery(const std::string& queryOutput, std::string& outLayout, std::string& outVariant) {
    outLayout.clear();
    outVariant.clear();
    std::istringstream iss(queryOutput);
    std::string line;
    while (std::getline(iss, line)) {
        std::string t = trimAscii(line);
        if (t.rfind("layout:", 0) == 0) {
            std::string val = trimAscii(t.substr(7));
            size_t c = val.find(',');
            outLayout = trimAscii(c == std::string::npos ? val : val.substr(0, c));
        } else if (t.rfind("variant:", 0) == 0) {
            std::string val = trimAscii(t.substr(8));
            size_t c = val.find(',');
            outVariant = trimAscii(c == std::string::npos ? val : val.substr(0, c));
        }
    }
    return !outLayout.empty();
}

bool XkbOverlay::detectActiveKeyboardLayout(std::string& outLayout, std::string& outVariant, XkbCompositorHint hint) {
    outLayout.clear();
    outVariant.clear();
    if (xwayland_env::isIsolatedTestEnv()) {
        outLayout = "us";
        return true;
    }

    XkbCompositorHint effHint = detectCompositorFromEnv(hint);
    if (effHint == XkbCompositorHint::GnomeShell && hasExecutableInPath("gsettings")) {
        std::string gsOut;
        if (runPipeRead("gsettings get org.gnome.desktop.input-sources sources 2>/dev/null", gsOut)) {
            if (parseGnomeInputSources(gsOut, outLayout, outVariant)) {
                return true;
            }
        }
    }

    if (effHint == XkbCompositorHint::KWin) {
        const char* cfgHome = std::getenv("XDG_CONFIG_HOME");
        const char* home = std::getenv("HOME");
        std::filesystem::path kxkbPath = (cfgHome && cfgHome[0])
            ? (std::filesystem::path(cfgHome) / "kxkbrc")
            : (std::filesystem::path(home ? home : "") / ".config/kxkbrc");
        std::ifstream kf(kxkbPath);
        if (kf.is_open()) {
            std::ostringstream buf;
            buf << kf.rdbuf();
            if (parseKxkbrcContent(buf.str(), outLayout, outVariant)) {
                return true;
            }
        } else {
            outLayout = "us";
            return true;
        }
    }

    std::string disp = resolveX11Display();
    if (!disp.empty() && hasExecutableInPath("setxkbmap")) {
        std::string queryOut;
        std::string cmd = "DISPLAY='" + disp + "' setxkbmap -query 2>/dev/null";
        if (runPipeRead(cmd.c_str(), queryOut) && parseSetxkbmapQuery(queryOut, outLayout, outVariant)) {
            return true;
        }
    }

    outLayout = "us";
    return false;
}

bool XkbOverlay::applyX11XkbOverlay(const std::string& xkbConfigDir) {
    if (xwayland_env::isIsolatedTestEnv()) {
        return true;
    }
    std::string disp = resolveX11Display();
    if (disp.empty() || !hasExecutableInPath("xkbcomp")) {
        return false;
    }
    std::string dir = xkbConfigDir.empty() ? defaultXkbConfigDir() : xkbConfigDir;
    ::setenv("VNSNOWKEY_XKB_DIR_TMP", dir.c_str(), 1);
    int rc = std::system(
        "sh -c '"
        "if command -v setxkbmap >/dev/null 2>&1; then "
        "  setxkbmap -print 2>/dev/null | xkbcomp -I\"$VNSNOWKEY_XKB_DIR_TMP\" - \"$DISPLAY\" >/dev/null 2>&1; "
        "else "
        "  printf \"xkb_keymap {\\n  xkb_keycodes { include \\\"evdev+aliases(qwerty)\\\" };\\n  xkb_types { include \\\"complete\\\" };\\n  xkb_compat { include \\\"complete\\\" };\\n  xkb_symbols { include \\\"pc+us+inet(evdev)\\\" };\\n};\\n\" | xkbcomp -I\"$VNSNOWKEY_XKB_DIR_TMP\" - \"$DISPLAY\" >/dev/null 2>&1; "
        "fi' >/dev/null 2>&1");
    ::unsetenv("VNSNOWKEY_XKB_DIR_TMP");
    return rc == 0;
}

bool XkbOverlay::reloadCompositorXkb(XkbCompositorHint hint) {
    if (xwayland_env::isIsolatedTestEnv()) {
        return true;
    }

    const bool wayland = isWaylandSession();
    const XkbCompositorHint effHint = detectCompositorFromEnv(hint);

    if (effHint == XkbCompositorHint::Hyprland) {
        int rc = std::system("hyprctl keyword input:kb_layout us >/dev/null 2>&1 || true");
        return rc == 0;
    }

    if (effHint == XkbCompositorHint::Sway) {
        int rc = std::system("swaymsg 'input type:keyboard xkb_layout us' >/dev/null 2>&1 || true");
        return rc == 0;
    }

    if (effHint == XkbCompositorHint::GnomeShell) {
        if (!wayland) {
            // Pure GNOME X11: apply directly with xkbcomp WITHOUT toggling gsettings xkb-options
            // (toggling xkb-options on X11 triggers gsd-keyboard asynchronously and wipes our xkbcomp keymap!).
            return applyX11XkbOverlay();
        }
        int rc = std::system(
            "sh -c '"
            "v=$(gsettings get org.gnome.desktop.input-sources xkb-options 2>/dev/null); "
            "if [ -n \"$v\" ]; then "
            "  if [ \"$v\" = \"@as []\" ] || [ \"$v\" = \"[]\" ]; then "
            "    gsettings set org.gnome.desktop.input-sources xkb-options \"['\\''vnsnowkey:reload'\\'']\"; "
            "  else "
            "    gsettings set org.gnome.desktop.input-sources xkb-options \"@as []\"; "
            "  fi; "
            "  sleep 0.25; "
            "  gsettings set org.gnome.desktop.input-sources xkb-options \"$v\"; "
            "fi' >/dev/null 2>&1 || true");
        return rc == 0;
    }

    if (effHint == XkbCompositorHint::KWin) {
        int rc1 = std::system(
            "gdbus emit --session --object-path /kxkbrc "
            "--signal org.kde.kconfig.notify.ConfigChanged "
            "'{\"Layout\": [b\"Use\", b\"LayoutList\", b\"VariantList\", b\"Options\"]}' >/dev/null 2>&1 || true");
        (void)rc1;

        int rc2 = std::system(
            "sh -c '"
            "if command -v qdbus6 >/dev/null 2>&1; then qdbus6 org.kde.KWin /KWin reconfigure; "
            "elif command -v qdbus-qt6 >/dev/null 2>&1; then qdbus-qt6 org.kde.KWin /KWin reconfigure; "
            "elif command -v qdbus >/dev/null 2>&1; then qdbus org.kde.KWin /KWin reconfigure; "
            "elif command -v gdbus >/dev/null 2>&1; then gdbus call --session --dest org.kde.KWin --object-path /KWin --method org.kde.KWin.reconfigure; "
            "fi' >/dev/null 2>&1 || true");
        if (!wayland) {
            return applyX11XkbOverlay() || (rc2 == 0);
        }
        return rc2 == 0;
    }

    if (!wayland) {
        return applyX11XkbOverlay();
    }
    return false;
}

bool XkbOverlay::reloadKWinXkb() {
    return reloadCompositorXkb(XkbCompositorHint::Auto);
}

} // namespace vnsnowkey
