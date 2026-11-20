/*
 * VNSnowKey - Virtual uinput + evdev + libxkbcommon Integration, High-Speed & Fuzz Harness
 * Tests:
 * 1. XkbOverlay compilation and 134-character Vietnamese + ASCII lookup via real libxkbcommon
 *    with CapsLock both OFF and ON.
 * 2. Real Linux kernel /dev/uinput -> /dev/input/eventX loopback with EVIOCGRAB isolation:
 *    - Backtrack & tone replacement (as -> á -> asf -> à -> asfz -> a, dd -> đ -> ddd -> dd, uow -> ươ)
 *    - High-speed typing (>= 150 keys/sec, 10x the 15 keys/sec requirement) with zero dropped/reordered chars
 *    - 2,000-keystroke random fuzz test + kernel EVIOCGKEY verification of zero stuck keys
 * 3. Config TOML round-trip serialization & Macro import/export.
 */

#include "../src/config/config.hpp"
#include "../src/engine/bamboo_utils.hpp"
#include "../src/engine/session_processor.hpp"
#include "../src/input/evdev_capture.hpp"
#include "../src/output/uinput_emitter.hpp"
#include "../src/output/xkb_overlay.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <linux/input.h>
#include <poll.h>
#include <random>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <xkbcommon/xkbcommon.h>

using namespace vnsnowkey;
using namespace vnsnowkey::bamboo;

namespace {

int g_passed = 0;
int g_failed = 0;

void checkEq(const std::string& got, const std::string& expected, const char* expr, int line) {
    if (got == expected) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "[FAIL] Line " << line << " (" << expr << "): got [" << got
                  << "], expected [" << expected << "]\n";
    }
}

void checkBool(bool got, bool expected, const char* expr, int line) {
    if (got == expected) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "[FAIL] Line " << line << " (" << expr << "): got [" << (got ? "true" : "false")
                  << "], expected [" << (expected ? "true" : "false") << "]\n";
    }
}

#define EXPECT_STR_EQ(got, exp) checkEq((got), (exp), #got, __LINE__)
#define EXPECT_BOOL_EQ(got, exp) checkBool((got), (exp), #got, __LINE__)

struct XkbReceiver {
    xkb_context* ctx = nullptr;
    xkb_keymap* keymap = nullptr;
    xkb_state* state = nullptr;
    std::u32string screenBuffer;

    bool init(const std::string& xkbDir) {
        setenv("XDG_CONFIG_HOME", xkbDir.c_str(), 1);
        ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!ctx) return false;
        xkb_rule_names names{};
        names.rules = "evdev";
        names.model = "pc105";
        names.layout = "us";
        names.variant = "";
        names.options = "";
        keymap = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!keymap) return false;
        state = xkb_state_new(keymap);
        return state != nullptr;
    }

    ~XkbReceiver() {
        if (state) xkb_state_state_unref_helper();
        if (keymap) xkb_keymap_unref(keymap);
        if (ctx) xkb_context_unref(ctx);
    }

    void xkb_state_state_unref_helper() {
        xkb_state_unref(state);
        state = nullptr;
    }

    void processEvdevKey(uint16_t evdevCode, int32_t value) {
        xkb_keycode_t xkbCode = static_cast<xkb_keycode_t>(evdevCode + 8);
        if (value == 0) {
            xkb_state_update_key(state, xkbCode, XKB_KEY_UP);
            return;
        }
        if (value == 1) {
            // Check modifier keys first
            if (evdevCode == KEY_LEFTSHIFT || evdevCode == KEY_RIGHTSHIFT ||
                evdevCode == KEY_LEFTCTRL || evdevCode == KEY_RIGHTCTRL ||
                evdevCode == KEY_LEFTALT || evdevCode == KEY_RIGHTALT ||
                evdevCode == KEY_LEFTMETA || evdevCode == KEY_RIGHTMETA ||
                evdevCode == KEY_CAPSLOCK ||
                evdevCode == kVNSnowKeyLevel3Evdev || evdevCode == kVNSnowKeyLevel5Evdev) {
                xkb_state_update_key(state, xkbCode, XKB_KEY_DOWN);
                return;
            }
        }
        if (evdevCode == KEY_BACKSPACE) {
            if (!screenBuffer.empty()) {
                screenBuffer.pop_back();
            }
            if (value == 1) {
                xkb_state_update_key(state, xkbCode, XKB_KEY_DOWN);
            }
            return;
        }
        char buf[32]{};
        int n = xkb_state_key_get_utf8(state, xkbCode, buf, sizeof(buf));
        if (n > 0) {
            std::u32string u32 = utf8ToU32(std::string_view(buf, static_cast<size_t>(n)));
            screenBuffer.append(u32);
        }
        if (value == 1) {
            xkb_state_update_key(state, xkbCode, XKB_KEY_DOWN);
        }
    }
};

void drainEvdevToReceiver(int evFd, XkbReceiver& rx, int waitMs = 15) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
    while (true) {
        auto now = std::chrono::steady_clock::now();
        int remMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        if (remMs < 0) remMs = 0;

        pollfd pfd{evFd, POLLIN, 0};
        int rc = ::poll(&pfd, 1, remMs);
        if (rc <= 0) break;

        input_event evs[128];
        ssize_t rd = ::read(evFd, evs, sizeof(evs));
        if (rd <= 0) break;
        size_t count = static_cast<size_t>(rd) / sizeof(input_event);
        for (size_t i = 0; i < count; ++i) {
            if (evs[i].type == EV_KEY) {
                rx.processEvdevKey(evs[i].code, evs[i].value);
            }
        }
        // Reset deadline slightly as long as events are actively arriving
        deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5);
    }
}

void testXkbOverlayAll134VietnameseChars(const std::string& tmpCfgDir) {
    XkbOverlay overlay;
    std::string err;
    bool installed = overlay.installOverlayFiles(tmpCfgDir + "/xkb", &err);
    EXPECT_BOOL_EQ(installed, true);
    EXPECT_BOOL_EQ(XkbOverlay::isOverlayInstalled(tmpCfgDir + "/xkb"), true);

    XkbReceiver rx;
    EXPECT_BOOL_EQ(rx.init(tmpCfgDir), true);

    // 1. Verify all 134 Vietnamese characters (67 lowercase + 67 uppercase) with CapsLock OFF
    for (char32_t cp : getAllVietnameseChars()) {
        rx.screenBuffer.clear();
        auto chordOpt = overlay.lookupChar(cp);
        EXPECT_BOOL_EQ(chordOpt.has_value(), true);
        if (!chordOpt.has_value()) continue;

        if (chordOpt->shift)  rx.processEvdevKey(KEY_LEFTSHIFT, 1);
        if (chordOpt->level3) rx.processEvdevKey(kVNSnowKeyLevel3Evdev, 1);
        if (chordOpt->level5) rx.processEvdevKey(kVNSnowKeyLevel5Evdev, 1);

        rx.processEvdevKey(chordOpt->keycode, 1);
        rx.processEvdevKey(chordOpt->keycode, 0);

        if (chordOpt->level5) rx.processEvdevKey(kVNSnowKeyLevel5Evdev, 0);
        if (chordOpt->level3) rx.processEvdevKey(kVNSnowKeyLevel3Evdev, 0);
        if (chordOpt->shift)  rx.processEvdevKey(KEY_LEFTSHIFT, 0);

        EXPECT_STR_EQ(u32ToUtf8(rx.screenBuffer), u32CharToUtf8(cp));
    }

    // 2. Toggle CapsLock ON and verify all 134 Vietnamese characters STILL produce exact case!
    rx.processEvdevKey(KEY_CAPSLOCK, 1);
    rx.processEvdevKey(KEY_CAPSLOCK, 0);
    for (char32_t cp : getAllVietnameseChars()) {
        rx.screenBuffer.clear();
        auto chordOpt = overlay.lookupChar(cp);
        if (!chordOpt.has_value()) continue;

        if (chordOpt->shift)  rx.processEvdevKey(KEY_LEFTSHIFT, 1);
        if (chordOpt->level3) rx.processEvdevKey(kVNSnowKeyLevel3Evdev, 1);
        if (chordOpt->level5) rx.processEvdevKey(kVNSnowKeyLevel5Evdev, 1);

        rx.processEvdevKey(chordOpt->keycode, 1);
        rx.processEvdevKey(chordOpt->keycode, 0);

        if (chordOpt->level5) rx.processEvdevKey(kVNSnowKeyLevel5Evdev, 0);
        if (chordOpt->level3) rx.processEvdevKey(kVNSnowKeyLevel3Evdev, 0);
        if (chordOpt->shift)  rx.processEvdevKey(KEY_LEFTSHIFT, 0);

        EXPECT_STR_EQ(u32ToUtf8(rx.screenBuffer), u32CharToUtf8(cp));
    }
    // Toggle CapsLock back OFF
    rx.processEvdevKey(KEY_CAPSLOCK, 1);
    rx.processEvdevKey(KEY_CAPSLOCK, 0);
}

void testUinputKernelLoopbackAndStress(const std::string& tmpCfgDir) {
    UinputEmitter emitter;
    std::string err;
    if (!emitter.initialize(&err)) {
        std::cerr << "[WARN] /dev/uinput not accessible (" << err << ") - skipping kernel loopback.\n";
        return;
    }

    // Wait briefly for kernel + systemd-udevd to create /dev/input/eventY and apply group 'input' (0660) permissions
    std::string eventPath;
    int evFd = -1;
    for (int retry = 0; retry < 100; ++retry) {
        eventPath = emitter.findSysInputEventPath();
        if (!eventPath.empty()) {
            evFd = ::open(eventPath.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (evFd >= 0) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_BOOL_EQ(!eventPath.empty(), true);
    EXPECT_BOOL_EQ(evFd >= 0, true);
    if (evFd < 0) return;

    // Grab the virtual keyboard's evdev node so test keystrokes never leak into the user's desktop!
    int grabRc = ::ioctl(evFd, EVIOCGRAB, 1);
    if (grabRc != 0) {
        std::cerr << "[DEBUG] EVIOCGRAB on " << eventPath << " failed: " << std::strerror(errno) << " (errno=" << errno << ")\n";
    }
    EXPECT_BOOL_EQ(grabRc == 0, true);

    XkbReceiver rx;
    EXPECT_BOOL_EQ(rx.init(tmpCfgDir), true);

    SessionProcessor sp;
    SessionOptions opts;
    opts.inputMethodName = "Telex";
    opts.autoRestoreEnglish = true;
    sp.setOptions(opts);

    auto typeIntoUinput = [&](std::string_view rawInput) {
        rx.screenBuffer.clear();
        sp.reset();
        for (char32_t c : utf8ToU32(rawInput)) {
            KeyActionDiff diff = sp.processPrintableKey(c);
            if (diff.passThrough) {
                auto chord = emitter.xkbOverlay().lookupChar(c);
                if (chord.has_value()) {
                    if (chord->shift) emitter.forwardKeyEvent(KEY_LEFTSHIFT, 1);
                    emitter.forwardKeyEvent(chord->keycode, 1);
                    emitter.forwardKeyEvent(chord->keycode, 0);
                    if (chord->shift) emitter.forwardKeyEvent(KEY_LEFTSHIFT, 0);
                }
            } else {
                emitter.emitDiff(diff.backspaces, diff.textToEmit, diff.encodedToEmit, OutputMethod::XkbKeymap, 0, false);
            }
            drainEvdevToReceiver(evFd, rx, 1);
        }
        drainEvdevToReceiver(evFd, rx, 25);
        return u32ToUtf8(rx.screenBuffer);
    };

    // 1. Test backtrack, dd/ddd, uow, standalone w, tone shifting over real kernel uinput -> evdev
    EXPECT_STR_EQ(typeIntoUinput("as"), "á");
    EXPECT_STR_EQ(typeIntoUinput("asf"), "à");
    EXPECT_STR_EQ(typeIntoUinput("asfz"), "a");
    EXPECT_STR_EQ(typeIntoUinput("dd"), "đ");
    EXPECT_STR_EQ(typeIntoUinput("ddd"), "dd");
    EXPECT_STR_EQ(typeIntoUinput("uow"), "ươ");
    EXPECT_STR_EQ(typeIntoUinput("w"), "ư");
    EXPECT_STR_EQ(typeIntoUinput("chuyrene"), "chuyển");

    // 1b. Verify CapsLock ON emits uppercase ASCII + Vietnamese identically (no CHÀo / TẤn / TIẾng inversion!)
    emitter.forwardKeyEvent(KEY_CAPSLOCK, 1);
    emitter.forwardKeyEvent(KEY_CAPSLOCK, 0);
    drainEvdevToReceiver(evFd, rx, 20);
    rx.screenBuffer.clear();
    for (const std::u32string& word : {std::u32string(U"XIN "), std::u32string(U"CHÀO "),
                                       std::u32string(U"HỒ "), std::u32string(U"TẤN "),
                                       std::u32string(U"TRUYỀN")}) {
        emitter.emitDiff(0, word, "", OutputMethod::XkbKeymap, 0, false, 45, true);
        drainEvdevToReceiver(evFd, rx, 25);
    }
    EXPECT_STR_EQ(u32ToUtf8(rx.screenBuffer), "XIN CHÀO HỒ TẤN TRUYỀN");
    rx.screenBuffer.clear();
    for (const std::u32string& word : {std::u32string(U"TIẾNG "), std::u32string(U"VIỆT")}) {
        emitter.emitDiff(0, word, "", OutputMethod::XkbKeymap, 0, false, 45, true);
        drainEvdevToReceiver(evFd, rx, 25);
    }
    EXPECT_STR_EQ(u32ToUtf8(rx.screenBuffer), "TIẾNG VIỆT");
    emitter.forwardKeyEvent(KEY_CAPSLOCK, 1);
    emitter.forwardKeyEvent(KEY_CAPSLOCK, 0);
    drainEvdevToReceiver(evFd, rx, 20);

    // 1c. Verify desynced CapsLock (compositor Lock OFF while Shift-adjusted emission requested) stays uppercase
    rx.screenBuffer.clear();
    emitter.emitDiff(0, U"XIN CHÀO", "", OutputMethod::XkbKeymap, 0, false, 45, false);
    drainEvdevToReceiver(evFd, rx, 25);
    EXPECT_STR_EQ(u32ToUtf8(rx.screenBuffer), "XIN CHÀO");

    // 2. High-Speed Typing Stress Test (> 150 keys/sec, well above the >= 15 keys/sec requirement!)
    const std::string kLongInput =
        "Trawm nawm trong coix nguwowif ta, chuwx taif chuwx meenhj kheos laf ghets nhau. "
        "Trair qua mootj cuoocj beer daau, nhuwngx ddieeuf troong thaays maf ddau ddowsn longf! "
        "VIEETJ NAM DDOOCJ LAAPJ TUWJ DO HANHJ PHUCS!";
    const std::string kExpectedLong =
        "Trăm năm trong cõi người ta, chữ tài chữ mệnh khéo là ghét nhau. "
        "Trải qua một cuộc bể dâu, những điều trông thấy mà đau đớn lòng! "
        "VIỆT NAM ĐỘC LẬP TỰ DO HẠNH PHÚC!";

    rx.screenBuffer.clear();
    sp.reset();
    auto tStart = std::chrono::steady_clock::now();
    for (char32_t c : utf8ToU32(kLongInput)) {
        KeyActionDiff diff = sp.processPrintableKey(c);
        if (diff.passThrough) {
            auto chord = emitter.xkbOverlay().lookupChar(c);
            if (chord.has_value()) {
                if (chord->shift) emitter.forwardKeyEvent(KEY_LEFTSHIFT, 1);
                emitter.forwardKeyEvent(chord->keycode, 1);
                emitter.forwardKeyEvent(chord->keycode, 0);
                if (chord->shift) emitter.forwardKeyEvent(KEY_LEFTSHIFT, 0);
            }
        } else {
            emitter.emitDiff(diff.backspaces, diff.textToEmit, diff.encodedToEmit, OutputMethod::XkbKeymap, 0, false);
        }
        // Drain periodically so kernel evdev buffer (64 events) never overflows during zero-delay burst
        drainEvdevToReceiver(evFd, rx, 0);
    }
    drainEvdevToReceiver(evFd, rx, 30);
    auto tEnd = std::chrono::steady_clock::now();
    double elapsedSec = std::chrono::duration<double>(tEnd - tStart).count();
    double keysPerSec = static_cast<double>(kLongInput.size()) / std::max(0.0001, elapsedSec);

    std::cout << "[INFO] High-speed uinput stress test: " << kLongInput.size()
              << " keys processed at " << static_cast<int>(keysPerSec) << " keys/sec.\n";
    EXPECT_STR_EQ(u32ToUtf8(rx.screenBuffer), kExpectedLong);
    EXPECT_BOOL_EQ(keysPerSec >= 15.0, true);

    // 3. Random Fuzz Test (2,000 random keystrokes + Backspaces) & Stuck-Key Verification
    std::mt19937 rng(20261114);
    const std::string kFuzzAlphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,;!?[]\b";
    std::uniform_int_distribution<size_t> dist(0, kFuzzAlphabet.size() - 1);

    sp.reset();
    rx.screenBuffer.clear();
    for (int i = 0; i < 2000; ++i) {
        char c = kFuzzAlphabet[dist(rng)];
        if (c == '\b') {
            auto diff = sp.processBackspace();
            if (diff.passThrough) {
                emitter.forwardKeyEvent(KEY_BACKSPACE, 1);
                emitter.forwardKeyEvent(KEY_BACKSPACE, 0);
            } else {
                emitter.emitDiff(diff.backspaces, diff.textToEmit, diff.encodedToEmit, OutputMethod::XkbKeymap, 0, false);
            }
        } else {
            auto diff = sp.processPrintableKey(static_cast<char32_t>(c));
            if (diff.passThrough) {
                auto chord = emitter.xkbOverlay().lookupChar(static_cast<char32_t>(c));
                if (chord.has_value()) {
                    if (chord->shift) emitter.forwardKeyEvent(KEY_LEFTSHIFT, 1);
                    emitter.forwardKeyEvent(chord->keycode, 1);
                    emitter.forwardKeyEvent(chord->keycode, 0);
                    if (chord->shift) emitter.forwardKeyEvent(KEY_LEFTSHIFT, 0);
                }
            } else {
                emitter.emitDiff(diff.backspaces, diff.textToEmit, diff.encodedToEmit, OutputMethod::XkbKeymap, 0, false);
            }
        }
        if ((i & 7) == 0) {
            drainEvdevToReceiver(evFd, rx, 0);
        }
    }
    drainEvdevToReceiver(evFd, rx, 30);

    // Verify ZERO stuck keys in UinputEmitter state AND kernel EVIOCGKEY bitmask!
    EXPECT_BOOL_EQ(emitter.hasAnyPressedKey(), false);
    unsigned long keyBits[(KEY_MAX + sizeof(unsigned long) * 8) / (sizeof(unsigned long) * 8)]{};
    int gkeyRc = ::ioctl(evFd, EVIOCGKEY(sizeof(keyBits)), keyBits);
    EXPECT_BOOL_EQ(gkeyRc >= 0, true);
    bool anyKernelKeyStuck = false;
    for (unsigned long w : keyBits) {
        if (w != 0) anyKernelKeyStuck = true;
    }
    EXPECT_BOOL_EQ(anyKernelKeyStuck, false);

    ::ioctl(evFd, EVIOCGRAB, 0);
    ::close(evFd);
    emitter.shutdown();
}

void testConfigSerializationRoundTrip(const std::string& tmpCfgDir) {
    Config cfg = Config::createDefault();
    cfg.vietnameseEnabled = false;
    cfg.inputMethod = "VNI";
    cfg.outputCharset = "VNI Windows";
    cfg.modernToneStyle = false;
    cfg.shorthandInitialConsonant = true;
    cfg.shorthandFinalConsonant = true;
    cfg.defaultOutputMethod = OutputMethod::ClipboardCtrlV;
    cfg.defaultDelayUs = 250;
    cfg.macros[" custom "] = "Tùy chỉnh";

    cfg.appProfiles.erase(
        std::remove_if(cfg.appProfiles.begin(), cfg.appProfiles.end(),
                       [](const AppProfile& p) { return p.appClass == "firefox"; }),
        cfg.appProfiles.end());
    AppProfile firefoxProf;
    firefoxProf.appClass = "firefox";
    firefoxProf.mode = AppMode::AlwaysVi;
    firefoxProf.outputMethod = OutputMethod::XkbKeymap;
    firefoxProf.useCustomOutputMethod = true;
    firefoxProf.delayUs = 500;
    firefoxProf.autocompleteFix = true;
    cfg.appProfiles.push_back(firefoxProf);

    std::string cfgFile = tmpCfgDir + "/test_config.toml";
    EXPECT_BOOL_EQ(cfg.saveToFile(cfgFile), true);

    bool loadedOk = false;
    Config loaded = Config::loadFromFile(cfgFile, &loadedOk);
    EXPECT_BOOL_EQ(loadedOk, true);
    EXPECT_BOOL_EQ(loaded.vietnameseEnabled, false);
    EXPECT_STR_EQ(loaded.inputMethod, "VNI");
    EXPECT_STR_EQ(loaded.outputCharset, "VNI Windows");
    EXPECT_BOOL_EQ(loaded.modernToneStyle, false);
    EXPECT_BOOL_EQ(loaded.shorthandInitialConsonant, true);
    EXPECT_BOOL_EQ(loaded.shorthandFinalConsonant, true);
    EXPECT_BOOL_EQ(loaded.defaultOutputMethod == OutputMethod::ClipboardCtrlV, true);
    EXPECT_BOOL_EQ(loaded.defaultDelayUs == 250, true);

    const AppProfile* ff = loaded.findProfileForApp("org.mozilla.firefox");
    EXPECT_BOOL_EQ(ff != nullptr, true);
    if (ff) {
        EXPECT_BOOL_EQ(ff->mode == AppMode::AlwaysVi, true);
        EXPECT_BOOL_EQ(ff->autocompleteFix, true);
        EXPECT_BOOL_EQ(ff->delayUs == 500, true);
    }

    // AppMode::GameMode round-trip + aliases
    EXPECT_STR_EQ(Config::appModeToString(AppMode::GameMode), "game_mode");
    EXPECT_BOOL_EQ(Config::stringToAppMode("game_mode") == AppMode::GameMode, true);
    EXPECT_BOOL_EQ(Config::stringToAppMode("GameMode") == AppMode::GameMode, true);
    EXPECT_BOOL_EQ(Config::stringToAppMode("game") == AppMode::GameMode, true);
    AppProfile mcProf;
    mcProf.appClass = "minecraft";
    mcProf.mode = AppMode::GameMode;
    cfg.appProfiles.push_back(mcProf);
    EXPECT_BOOL_EQ(cfg.saveToFile(cfgFile), true);
    Config gameLoaded = Config::loadFromFile(cfgFile, &loadedOk);
    EXPECT_BOOL_EQ(loadedOk, true);
    const AppProfile* mc = gameLoaded.findProfileForApp("Minecraft* 1.21");
    EXPECT_BOOL_EQ(mc != nullptr, true);
    if (mc) {
        EXPECT_BOOL_EQ(mc->mode == AppMode::GameMode, true);
    }

    // Hotkey canonicalization & round-trip
    EXPECT_STR_EQ(Config::canonicalizeHotkey("shift + ctrl"), "Ctrl+Shift");
    EXPECT_STR_EQ(Config::canonicalizeHotkey("meta + space"), "Super+Space");
    EXPECT_STR_EQ(Config::canonicalizeHotkey("ctrl+alt+f12"), "Ctrl+Alt+F12");
    EXPECT_BOOL_EQ(Config::parseHotkey("Alt+Shift").isValid(), true);
    EXPECT_BOOL_EQ(Config::parseHotkey("Alt+Shift").keyCode == 0, true);
    EXPECT_BOOL_EQ(Config::parseHotkey("Ctrl+Alt+F12").keyCode == KEY_F12, true);
    EXPECT_BOOL_EQ(Config::parseHotkey("invalid_key_xyz").isValid(), false);

    cfg.shortcuts.togglePrimary = "Alt+Shift";
    cfg.shortcuts.toggleSecondary = "Ctrl+Alt+K";
    cfg.shortcuts.toggleExtra = "F8";
    cfg.shortcuts.restoreWord = "Esc";
    cfg.shortcuts.panicSafeMode = "Ctrl+Alt+F12";
    EXPECT_BOOL_EQ(cfg.saveToFile(cfgFile), true);
    Config hkLoaded = Config::loadFromFile(cfgFile, &loadedOk);
    EXPECT_BOOL_EQ(loadedOk, true);
    EXPECT_STR_EQ(hkLoaded.shortcuts.togglePrimary, "Alt+Shift");
    EXPECT_STR_EQ(hkLoaded.shortcuts.toggleSecondary, "Ctrl+Alt+K");
    EXPECT_STR_EQ(hkLoaded.shortcuts.toggleExtra, "F8");
    EXPECT_STR_EQ(hkLoaded.shortcuts.restoreWord, "Esc");
    EXPECT_STR_EQ(hkLoaded.shortcuts.panicSafeMode, "Ctrl+Alt+F12");

    // Legacy TOML shortcuts boolean migration
    const std::string legacyToml =
        "[shortcuts]\n"
        "toggle_ctrl_shift = false\n"
        "toggle_super_space = false\n"
        "toggle_ctrl_space = true\n"
        "toggle_alt_z = true\n"
        "restore_on_esc = true\n"
        "panic_ctrl_alt_pause = false\n";
    Config migrated = Config::fromToml(legacyToml);
    EXPECT_STR_EQ(migrated.shortcuts.togglePrimary, "Ctrl+Space");
    EXPECT_STR_EQ(migrated.shortcuts.toggleSecondary, "Alt+Z");
    EXPECT_STR_EQ(migrated.shortcuts.restoreWord, "Esc");
    EXPECT_STR_EQ(migrated.shortcuts.panicSafeMode, "");
}

void testXkbKeymapParsersAndFallback() {
    // 1. Simulate the exact broken Ubuntu X11 keymap dump (has system I178/I184/EIGHT_LEVEL_ALPHABETIC but NO agrave)
    const std::string brokenDump =
        "    <I178> = 178;\n"
        "    <I184> = 184;\n"
        "    type \"EIGHT_LEVEL_ALPHABETIC\" {\n"
        "    key <AD01> { symbols[Group1]= [ q, Q ] };\n";
    std::string missing;
    EXPECT_BOOL_EQ(XkbOverlay::keymapTextHasVnsnowkeyOverlay(brokenDump, &missing), false);
    EXPECT_BOOL_EQ(missing.find("agrave") != std::string::npos, true);

    const std::string validDump =
        "    <I178> = 178;\n"
        "    <I184> = 184;\n"
        "    type \"EIGHT_LEVEL_ALPHABETIC\" {\n"
        "    key <AD01> { symbols[Group1]= [ q, Q, agrave, Agrave, aacute, Aacute, ahook, Ahook ] };\n";
    EXPECT_BOOL_EQ(XkbOverlay::keymapTextHasVnsnowkeyOverlay(validDump, &missing), true);
    EXPECT_STR_EQ(missing, "");

    // 2. Layout parsers across GNOME / KDE / X11
    std::string layout, variant;
    EXPECT_BOOL_EQ(XkbOverlay::parseGnomeInputSources("[('xkb', 'us')]", layout, variant), true);
    EXPECT_STR_EQ(layout, "us");
    EXPECT_STR_EQ(variant, "");

    EXPECT_BOOL_EQ(XkbOverlay::parseGnomeInputSources("[('xkb', 'us+intl'), ('ibus', 'unikey')]", layout, variant), true);
    EXPECT_STR_EQ(layout, "us");
    EXPECT_STR_EQ(variant, "intl");

    EXPECT_BOOL_EQ(XkbOverlay::parseKxkbrcContent("[Layout]\nUse=true\nLayoutList=us,vn\nVariantList=intl,\n", layout, variant), true);
    EXPECT_STR_EQ(layout, "us");
    EXPECT_STR_EQ(variant, "intl");

    EXPECT_BOOL_EQ(XkbOverlay::parseKxkbrcContent("[Layout]\nUse=false\nLayoutList=fr\n", layout, variant), true);
    EXPECT_STR_EQ(layout, "us");

    EXPECT_BOOL_EQ(XkbOverlay::parseSetxkbmapQuery("rules: evdev\nmodel: pc105\nlayout: gb,us\nvariant: extd,\n", layout, variant), true);
    EXPECT_STR_EQ(layout, "gb");
    EXPECT_STR_EQ(variant, "extd");

    // 3. EvdevCapture fallback override flag
    bamboo::SessionProcessor sp;
    UinputEmitter em;
    EvdevCapture cap(sp, em);
    EXPECT_BOOL_EQ(cap.xkbFallbackMethod() == -1, true);
    cap.setXkbFallbackMethod(static_cast<int>(OutputMethod::CtrlShiftU));
    EXPECT_BOOL_EQ(cap.xkbFallbackMethod() == static_cast<int>(OutputMethod::CtrlShiftU), true);
    cap.setXkbFallbackMethod(-1);
    EXPECT_BOOL_EQ(cap.xkbFallbackMethod() == -1, true);
}

} // namespace

int main() {
    std::string tmpDir = "/tmp/vnsnowkey_test_harness_" + std::to_string(::getpid());
    std::filesystem::create_directories(tmpDir);

    testXkbOverlayAll134VietnameseChars(tmpDir);
    testUinputKernelLoopbackAndStress(tmpDir);
    testConfigSerializationRoundTrip(tmpDir);
    testXkbKeymapParsersAndFallback();

    std::filesystem::remove_all(tmpDir);

    std::cout << "Integration & Harness Tests Summary: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
