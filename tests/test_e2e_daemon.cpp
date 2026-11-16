/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * End-to-End Daemon, D-Bus CLI, Global Shortcut, Mouse/Nav Reset & Clean Exit Integration Test
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "../src/config/config.hpp"
#include "../src/engine/bamboo_utils.hpp"
#include "../src/output/uinput_emitter.hpp"
#include "../src/output/xkb_overlay.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <set>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <xkbcommon/xkbcommon.h>

using namespace vnsnowkey;
using namespace vnsnowkey::bamboo;

namespace {

int g_passed = 0;
int g_failed = 0;

void checkEq(const std::string& actual, const std::string& expected, const char* expr, int line) {
    if (actual == expected) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "[FAIL] Line " << line << " (" << expr << "): got [" << actual
                  << "], expected [" << expected << "]\n";
    }
}

void checkBool(bool actual, bool expected, const char* expr, int line) {
    if (actual == expected) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "[FAIL] Line " << line << " (" << expr << "): got ["
                  << (actual ? "true" : "false") << "], expected ["
                  << (expected ? "true" : "false") << "]\n";
    }
}

#define EXPECT_STR_EQ(a, b) checkEq((a), (b), #a, __LINE__)
#define EXPECT_BOOL_EQ(a, b) checkBool((a), (b), #a, __LINE__)

struct VirtualInputDevice {
    int uinputFd = -1;
    std::string eventPath;

    bool createKeyboard(const char* name) {
        uinputFd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (uinputFd < 0) return false;

        ::ioctl(uinputFd, UI_SET_EVBIT, EV_KEY);
        ::ioctl(uinputFd, UI_SET_EVBIT, EV_SYN);
        for (int code = 1; code < 248; ++code) {
            ::ioctl(uinputFd, UI_SET_KEYBIT, code);
        }

        uinput_setup usetup{};
        usetup.id.bustype = BUS_USB;
        usetup.id.vendor  = UinputEmitter::kVNSnowKeyVendorId;
        usetup.id.product = UinputEmitter::kVNSnowKeyProductId; // Ignored by any live desktop vnsnowkeyd unless VNSNOWKEY_ONLY_DEVICES
        usetup.id.version = 1;
        std::snprintf(usetup.name, UINPUT_MAX_NAME_SIZE, "%s", name);

        if (::ioctl(uinputFd, UI_DEV_SETUP, &usetup) < 0 ||
            ::ioctl(uinputFd, UI_DEV_CREATE) < 0) {
            ::close(uinputFd);
            uinputFd = -1;
            return false;
        }
        return waitForEventNode();
    }

    bool createMouse(const char* name) {
        uinputFd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (uinputFd < 0) return false;

        ::ioctl(uinputFd, UI_SET_EVBIT, EV_KEY);
        ::ioctl(uinputFd, UI_SET_EVBIT, EV_REL);
        ::ioctl(uinputFd, UI_SET_EVBIT, EV_SYN);
        ::ioctl(uinputFd, UI_SET_KEYBIT, BTN_LEFT);
        ::ioctl(uinputFd, UI_SET_KEYBIT, BTN_RIGHT);
        ::ioctl(uinputFd, UI_SET_KEYBIT, BTN_MIDDLE);
        ::ioctl(uinputFd, UI_SET_RELBIT, REL_X);
        ::ioctl(uinputFd, UI_SET_RELBIT, REL_Y);

        uinput_setup usetup{};
        usetup.id.bustype = BUS_USB;
        usetup.id.vendor  = UinputEmitter::kVNSnowKeyVendorId;
        usetup.id.product = UinputEmitter::kVNSnowKeyProductId; // Ignored by any live desktop vnsnowkeyd unless VNSNOWKEY_ONLY_DEVICES
        usetup.id.version = 1;
        std::snprintf(usetup.name, UINPUT_MAX_NAME_SIZE, "%s", name);

        if (::ioctl(uinputFd, UI_DEV_SETUP, &usetup) < 0 ||
            ::ioctl(uinputFd, UI_DEV_CREATE) < 0) {
            ::close(uinputFd);
            uinputFd = -1;
            return false;
        }
        return waitForEventNode();
    }

    bool waitForEventNode() {
        char sysname[64]{};
        if (::ioctl(uinputFd, UI_GET_SYSNAME(sizeof(sysname)), sysname) < 0) return false;
        std::string sysDir = std::string("/sys/devices/virtual/input/") + sysname;
        for (int retry = 0; retry < 100; ++retry) {
            try {
                for (const auto& entry : std::filesystem::directory_iterator(sysDir)) {
                    std::string fname = entry.path().filename().string();
                    if (fname.rfind("event", 0) == 0) {
                        std::string candidate = "/dev/input/" + fname;
                        int fd = ::open(candidate.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                        if (fd >= 0) {
                            ::close(fd);
                            eventPath = candidate;
                            return true;
                        }
                    }
                }
            } catch (...) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    void emitKey(uint16_t code, int32_t value) {
        if (uinputFd < 0) return;
        input_event ev{};
        ev.type = EV_KEY;
        ev.code = code;
        ev.value = value;
        ssize_t u1 = ::write(uinputFd, &ev, sizeof(ev));
        (void)u1;
        input_event syn{};
        syn.type = EV_SYN;
        syn.code = SYN_REPORT;
        syn.value = 0;
        ssize_t u2 = ::write(uinputFd, &syn, sizeof(syn));
        (void)u2;
    }

    void tapKey(uint16_t code) {
        emitKey(code, 1);
        emitKey(code, 0);
    }

    ~VirtualInputDevice() {
        if (uinputFd >= 0) {
            ::ioctl(uinputFd, UI_DEV_DESTROY);
            ::close(uinputFd);
        }
    }
};

struct XkbScreenSimulator {
    xkb_context* ctx = nullptr;
    xkb_keymap* keymap = nullptr;
    xkb_state* state = nullptr;
    std::u32string text;
    size_t cursorPos = 0;

    void clear() {
        text.clear();
        cursorPos = 0;
    }

    bool init(const std::string& xdgConfigHome) {
        ::setenv("XDG_CONFIG_HOME", xdgConfigHome.c_str(), 1);
        ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!ctx) return false;
        xkb_rule_names names{"evdev", "pc105", "us", "", ""};
        keymap = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!keymap) return false;
        state = xkb_state_new(keymap);
        return state != nullptr;
    }

    ~XkbScreenSimulator() {
        if (state) xkb_state_unref(state);
        if (keymap) xkb_keymap_unref(keymap);
        if (ctx) xkb_context_unref(ctx);
    }

    void onEvdevKey(uint16_t code, int32_t value) {
        xkb_keycode_t xkc = static_cast<xkb_keycode_t>(code + 8);
        if (value == 0) {
            xkb_state_update_key(state, xkc, XKB_KEY_UP);
            return;
        }
        if (cursorPos > text.size()) {
            cursorPos = text.size();
        }
        if (code == KEY_LEFT) {
            if (cursorPos > 0) --cursorPos;
            xkb_state_update_key(state, xkc, XKB_KEY_DOWN);
            return;
        }
        if (code == KEY_RIGHT) {
            if (cursorPos < text.size()) ++cursorPos;
            xkb_state_update_key(state, xkc, XKB_KEY_DOWN);
            return;
        }
        if (code == KEY_HOME) {
            cursorPos = 0;
            xkb_state_update_key(state, xkc, XKB_KEY_DOWN);
            return;
        }
        if (code == KEY_END) {
            cursorPos = text.size();
            xkb_state_update_key(state, xkc, XKB_KEY_DOWN);
            return;
        }
        if (code == KEY_DELETE) {
            if (cursorPos < text.size()) {
                text.erase(cursorPos, 1);
            }
            xkb_state_update_key(state, xkc, XKB_KEY_DOWN);
            return;
        }
        if (code == KEY_BACKSPACE) {
            if (cursorPos > 0) {
                text.erase(cursorPos - 1, 1);
                --cursorPos;
            }
            xkb_state_update_key(state, xkc, XKB_KEY_DOWN);
            return;
        }
        if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT ||
            code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL ||
            code == KEY_LEFTALT || code == KEY_RIGHTALT ||
            code == KEY_LEFTMETA || code == KEY_RIGHTMETA ||
            code == KEY_CAPSLOCK ||
            code == kVNSnowKeyLevel3Evdev || code == kVNSnowKeyLevel5Evdev) {
            xkb_state_update_key(state, xkc, XKB_KEY_DOWN);
            return;
        }
        char buf[64]{};
        int n = xkb_state_key_get_utf8(state, xkc, buf, sizeof(buf));
        if (n > 0) {
            std::u32string u32 = utf8ToU32(std::string_view(buf, static_cast<size_t>(n)));
            text.insert(cursorPos, u32);
            cursorPos += u32.size();
        }
        xkb_state_update_key(state, xkc, XKB_KEY_DOWN);
    }
};

std::string runCmd(const std::string& cmd) {
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) return {};
    std::string out;
    char buf[256];
    while (::fgets(buf, sizeof(buf), pipe)) {
        out += buf;
    }
    ::pclose(pipe);
    return out;
}

std::set<std::string> snapshotEventNodes() {
    std::set<std::string> nodes;
    for (const auto& entry : std::filesystem::directory_iterator("/dev/input")) {
        std::string fname = entry.path().filename().string();
        if (fname.rfind("event", 0) == 0) {
            nodes.insert(entry.path().string());
        }
    }
    return nodes;
}

std::string findVNSnowKeyVirtualKeyboardNode(const std::set<std::string>& ignoreNodes) {
    for (const auto& entry : std::filesystem::directory_iterator("/dev/input")) {
        std::string fname = entry.path().filename().string();
        if (fname.rfind("event", 0) != 0) continue;
        std::string devPath = entry.path().string();
        if (ignoreNodes.count(devPath)) continue;
        int fd = ::open(devPath.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        input_id id{};
        ::ioctl(fd, EVIOCGID, &id);
        char nameBuf[256]{};
        ::ioctl(fd, EVIOCGNAME(sizeof(nameBuf)), nameBuf);
        ::close(fd);
        if (id.vendor == UinputEmitter::kVNSnowKeyVendorId &&
            id.product == UinputEmitter::kVNSnowKeyProductId &&
            std::string(nameBuf) == UinputEmitter::kVirtualDeviceName) {
            return devPath;
        }
    }
    return {};
}

uint16_t asciiToKeycode(char c, bool& shift) {
    shift = false;
    if (c >= 'A' && c <= 'Z') {
        shift = true;
        c = static_cast<char>(c - 'A' + 'a');
    }
    switch (c) {
        case 'a': return KEY_A;
        case 'b': return KEY_B;
        case 'c': return KEY_C;
        case 'd': return KEY_D;
        case 'e': return KEY_E;
        case 'f': return KEY_F;
        case 'g': return KEY_G;
        case 'h': return KEY_H;
        case 'i': return KEY_I;
        case 'j': return KEY_J;
        case 'k': return KEY_K;
        case 'l': return KEY_L;
        case 'm': return KEY_M;
        case 'n': return KEY_N;
        case 'o': return KEY_O;
        case 'p': return KEY_P;
        case 'q': return KEY_Q;
        case 'r': return KEY_R;
        case 's': return KEY_S;
        case 't': return KEY_T;
        case 'u': return KEY_U;
        case 'v': return KEY_V;
        case 'w': return KEY_W;
        case 'x': return KEY_X;
        case 'y': return KEY_Y;
        case 'z': return KEY_Z;
        case '1': return KEY_1;
        case '2': return KEY_2;
        case '3': return KEY_3;
        case '4': return KEY_4;
        case '5': return KEY_5;
        case '6': return KEY_6;
        case '7': return KEY_7;
        case '8': return KEY_8;
        case '9': return KEY_9;
        case '0': return KEY_0;
        case ' ': return KEY_SPACE;
        default: return 0;
    }
}

void drainDaemonOutput(int outEvFd, XkbScreenSimulator& screen, int waitMs = 15) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
    while (true) {
        auto now = std::chrono::steady_clock::now();
        int remMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        if (remMs < 0) remMs = 0;
        pollfd pfd{outEvFd, POLLIN, 0};
        if (::poll(&pfd, 1, remMs) <= 0) break;
        input_event evs[128];
        ssize_t rd = ::read(outEvFd, evs, sizeof(evs));
        if (rd <= 0) break;
        size_t count = static_cast<size_t>(rd) / sizeof(input_event);
        for (size_t i = 0; i < count; ++i) {
            if (evs[i].type == EV_KEY) {
                screen.onEvdevKey(evs[i].code, evs[i].value);
            }
        }
        auto afterActivity = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(15, std::min(waitMs, 40)));
        if (afterActivity > deadline) {
            deadline = afterActivity;
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (!::getenv("VNSNOWKEY_ISOLATED_DBUS")) {
        ::setenv("VNSNOWKEY_ISOLATED_DBUS", "1", 1);
        ::execlp("dbus-run-session", "dbus-run-session", "--", argv[0], nullptr);
    }
    (void)argc;

    const std::string tmpDir = "/tmp/vnsnowkey_e2e_test_" + std::to_string(::getpid());
    std::filesystem::create_directories(tmpDir);

    XkbOverlay overlay;
    overlay.installOverlayFiles(tmpDir + "/xkb");

    Config cfg = Config::createDefault();
    cfg.osdEnabled = false; // Avoid popping OSD during automated test
    AppProfile ffProf;
    ffProf.appClass = "firefox";
    ffProf.mode = AppMode::Default;
    ffProf.autocompleteFix = true;
    cfg.appProfiles.push_back(ffProf);
    cfg.saveToFile(tmpDir + "/vnsnowkey/config.toml");

    VirtualInputDevice virtKbd;
    VirtualInputDevice virtMouse;
    if (!virtKbd.createKeyboard("VNSnowKey E2E Test Physical Keyboard") ||
        !virtMouse.createMouse("VNSnowKey E2E Test Physical Mouse")) {
        std::cerr << "[WARN] /dev/uinput not accessible; skipping E2E daemon test.\n";
        std::filesystem::remove_all(tmpDir);
        return 0;
    }

    const std::string onlyDevs = virtKbd.eventPath + ":" + virtMouse.eventPath;
    ::setenv("XDG_CONFIG_HOME", tmpDir.c_str(), 1);
    ::setenv("VNSNOWKEY_ONLY_DEVICES", onlyDevs.c_str(), 1);
    ::unsetenv("DISPLAY");

    const std::set<std::string> existingNodes = snapshotEventNodes();

    pid_t daemonPid = ::fork();
    if (daemonPid == 0) {
        ::execl("./build/vnsnowkeyd", "./build/vnsnowkeyd", nullptr);
        std::exit(127);
    }

    // Wait for vnsnowkeyd to register D-Bus and create VNSnowKey Virtual Keyboard
    std::string vnsnowkeyOutNode;
    for (int retry = 0; retry < 100; ++retry) {
        vnsnowkeyOutNode = findVNSnowKeyVirtualKeyboardNode(existingNodes);
        if (!vnsnowkeyOutNode.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_BOOL_EQ(!vnsnowkeyOutNode.empty(), true);

    int outEvFd = -1;
    for (int retry = 0; retry < 50; ++retry) {
        outEvFd = ::open(vnsnowkeyOutNode.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (outEvFd >= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_BOOL_EQ(outEvFd >= 0, true);

    if (outEvFd >= 0) {
        // Grab vnsnowkeyd's virtual keyboard output so test keystrokes never reach the desktop
        int grabRc = ::ioctl(outEvFd, EVIOCGRAB, 1);
        EXPECT_BOOL_EQ(grabRc == 0, true);
    }

    // Wait briefly for D-Bus registration
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // 1. Verify CLI D-Bus status
    std::string statusOut = runCmd("./build/vnsnowkey ctl status");
    EXPECT_BOOL_EQ(statusOut.find("\"vietnamese_enabled\":true") != std::string::npos, true);
    EXPECT_BOOL_EQ(statusOut.find("\"input_method\":\"Telex\"") != std::string::npos, true);

    XkbScreenSimulator screen;
    EXPECT_BOOL_EQ(screen.init(tmpDir), true);

    auto typePhysicalString = [&](const std::string& s) {
        for (char c : s) {
            bool shift = false;
            uint16_t kc = asciiToKeycode(c, shift);
            if (kc == 0) continue;
            if (shift) virtKbd.emitKey(KEY_LEFTSHIFT, 1);
            virtKbd.tapKey(kc);
            if (shift) virtKbd.emitKey(KEY_LEFTSHIFT, 0);
            drainDaemonOutput(outEvFd, screen, 10);
        }
        drainDaemonOutput(outEvFd, screen, 30);
    };

    // 2. Test full E2E pipeline: Virtual Physical Keyboard -> vnsnowkeyd (EvdevCapture -> BambooEngine -> UinputEmitter) -> Screen
    screen.text.clear();
    typePhysicalString("Tieengs Vieetj ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "Tiếng Việt ");

    // 3. Test Global Shortcut Ctrl+Shift via evdev (VI -> EN)
    virtKbd.emitKey(KEY_LEFTCTRL, 1);
    virtKbd.emitKey(KEY_LEFTSHIFT, 1);
    virtKbd.emitKey(KEY_LEFTSHIFT, 0);
    virtKbd.emitKey(KEY_LEFTCTRL, 0);
    drainDaemonOutput(outEvFd, screen, 40);

    statusOut = runCmd("./build/vnsnowkey ctl status");
    EXPECT_BOOL_EQ(statusOut.find("\"vietnamese_enabled\":false") != std::string::npos, true);

    screen.text.clear();
    typePhysicalString("Tieengs ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "Tieengs ");

    // 4. Test Global Shortcut Super+Space via evdev (EN -> VI)
    virtKbd.emitKey(KEY_LEFTMETA, 1);
    virtKbd.tapKey(KEY_SPACE);
    virtKbd.emitKey(KEY_LEFTMETA, 0);
    drainDaemonOutput(outEvFd, screen, 40);

    statusOut = runCmd("./build/vnsnowkey ctl status");
    EXPECT_BOOL_EQ(statusOut.find("\"vietnamese_enabled\":true") != std::string::npos, true);

    // 5. Test Mouse Click Buffer Reset via evdev
    screen.text.clear();
    typePhysicalString("hoa");
    virtMouse.emitKey(BTN_LEFT, 1);
    virtMouse.emitKey(BTN_LEFT, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    typePhysicalString("s");
    // Because mouse click reset the buffer after "hoa", 's' must NOT add sắc tone to "hoa"!
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "hoas");

    // 6. Test Navigation Key (Up Arrow) Buffer Reset via evdev
    screen.clear();
    typePhysicalString("hoa");
    virtKbd.tapKey(KEY_UP);
    drainDaemonOutput(outEvFd, screen, 15);
    typePhysicalString("f");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "hoaf");

    // 7. Test LỖI 2 E2E Backspace & Tone Editing via real evdev -> vnsnowkeyd -> uinput
    // Switch back to Telex first just in case
    runCmd("./build/vnsnowkey ctl im Telex");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    auto pressBackspace = [&](int count = 1, bool burst = false) {
        for (int i = 0; i < count; ++i) {
            virtKbd.tapKey(KEY_BACKSPACE);
            if (!burst) drainDaemonOutput(outEvFd, screen, 12);
        }
        if (!burst) drainDaemonOutput(outEvFd, screen, 20);
    };

    // Repro A: "toans" -> "toán", Backspace -> "toá", "f" -> "toà"
    screen.text.clear();
    typePhysicalString("toans");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "toán");
    pressBackspace(1);
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "toá");
    typePhysicalString("f");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "toà");
    typePhysicalString(" ");

    // Repro B: "vieets" -> "viết", Backspace -> "viế", "j" -> "việ"
    screen.text.clear();
    typePhysicalString("vieets");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "viết");
    pressBackspace(1);
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "viế");
    typePhysicalString("j");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "việ");
    typePhysicalString(" ");

    // Repro B2: "yeens" -> "yến", Backspace -> "yế", "f" -> "yề"
    screen.text.clear();
    typePhysicalString("yeens");
    pressBackspace(1);
    typePhysicalString("f");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "yề");
    typePhysicalString(" ");

    // Consecutive Backspaces + continue typing
    screen.text.clear();
    typePhysicalString("tieengs");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "tiếng");
    pressBackspace(3);
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "ti");
    typePhysicalString("nhj ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "tịnh ");

    // Backspace until word empty (+ extra Backspace), then type new word in fresh field
    virtKbd.tapKey(KEY_UP);
    drainDaemonOutput(outEvFd, screen, 15);
    screen.clear();
    typePhysicalString("toans");
    pressBackspace(5);
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "");
    typePhysicalString("mowis ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "mới ");

    // Backspace with uppercase / Shift
    screen.text.clear();
    typePhysicalString("TOANS");
    pressBackspace(1);
    typePhysicalString("F");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "TOÀ");
    typePhysicalString(" ");

    screen.text.clear();
    typePhysicalString("Vieets");
    pressBackspace(1);
    typePhysicalString("J");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "Việ");
    typePhysicalString(" ");

    // Fast burst typing (>= 25 keys/s; ~500 keys/s) + Backspace + tone change while keeping evdev out queue drained
    auto drainNow = [&]() {
        input_event evs[128];
        while (true) {
            ssize_t rd = ::read(outEvFd, evs, sizeof(evs));
            if (rd <= 0) break;
            size_t count = static_cast<size_t>(rd) / sizeof(input_event);
            for (size_t i = 0; i < count; ++i) {
                if (evs[i].type == EV_KEY) {
                    screen.onEvdevKey(evs[i].code, evs[i].value);
                }
            }
        }
    };
    auto tapAsciiFast = [&](char c) {
        bool shift = false;
        uint16_t kc = asciiToKeycode(c, shift);
        if (kc == 0) return;
        if (shift) virtKbd.emitKey(KEY_LEFTSHIFT, 1);
        virtKbd.tapKey(kc);
        if (shift) virtKbd.emitKey(KEY_LEFTSHIFT, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        drainNow();
    };
    screen.text.clear();
    for (char c : std::string("toans")) tapAsciiFast(c);
    pressBackspace(1, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    drainNow();
    tapAsciiFast('f');
    tapAsciiFast(' ');
    for (char c : std::string("vieets")) tapAsciiFast(c);
    pressBackspace(1, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    drainNow();
    tapAsciiFast('j');
    tapAsciiFast(' ');
    drainDaemonOutput(outEvFd, screen, 120);
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "toà việ ");

    // 7b. Test Browser URL bar autocompleteFix on Backspace via D-Bus Firefox focus + real uinput
    runCmd("dbus-send --session --type=method_call --dest=org.vnsnowkey.Daemon /org/vnsnowkey/Daemon "
           "org.vnsnowkey.Daemon.NotifyWindowFocus string:\"firefox\" string:\"firefox\" "
           "string:\"Mozilla Firefox\" string:\"w_firefox\"");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    screen.text.clear();
    typePhysicalString("vie");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "vie");
    // Now press physical Backspace and verify raw emitted keycodes start with SPACE + BACKSPACE + BACKSPACE!
    virtKbd.tapKey(KEY_BACKSPACE);
    std::vector<uint16_t> bsKeySequence;
    auto bsDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
    while (std::chrono::steady_clock::now() < bsDeadline) {
        pollfd pfd{outEvFd, POLLIN, 0};
        if (::poll(&pfd, 1, 20) <= 0) break;
        input_event evs[64];
        ssize_t rd = ::read(outEvFd, evs, sizeof(evs));
        if (rd <= 0) break;
        size_t count = static_cast<size_t>(rd) / sizeof(input_event);
        for (size_t i = 0; i < count; ++i) {
            if (evs[i].type == EV_KEY) {
                screen.onEvdevKey(evs[i].code, evs[i].value);
                if (evs[i].value == 1) {
                    bsKeySequence.push_back(evs[i].code);
                }
            }
        }
        bsDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(15);
    }
    EXPECT_BOOL_EQ(bsKeySequence.size() == 3 &&
                   bsKeySequence[0] == KEY_SPACE &&
                   bsKeySequence[1] == KEY_BACKSPACE &&
                   bsKeySequence[2] == KEY_BACKSPACE,
                   true);
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "vi");
    // Continue typing "eets" + Backspace + "j" in Firefox profile -> must yield "việ"!
    typePhysicalString("eets");
    pressBackspace(1);
    typePhysicalString("j ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "việ ");
    // Return focus to a neutral app
    runCmd("dbus-send --session --type=method_call --dest=org.vnsnowkey.Daemon /org/vnsnowkey/Daemon "
           "org.vnsnowkey.Daemon.NotifyWindowFocus string:\"org.kde.kate\" string:\"kate\" "
           "string:\"Kate\" string:\"w_kate\"");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // 7c. Test auto-restored English Backspace + hold-repeat (value == 2) + Shift rollover via evdev
    screen.text.clear();
    typePhysicalString("windows");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "windows");
    pressBackspace(3);
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "wind");
    typePhysicalString("ows ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "windows ");

    screen.text.clear();
    typePhysicalString("autocomplete");
    pressBackspace(1);
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "autocomplet");
    typePhysicalString(" ");

    // Hold Backspace (press -> repeat value=2 across buffered word into empty buffer -> release) in a fresh field
    virtKbd.tapKey(KEY_UP);
    drainDaemonOutput(outEvFd, screen, 15);
    screen.clear();
    typePhysicalString("abcde");
    virtKbd.emitKey(KEY_BACKSPACE, 1);
    drainDaemonOutput(outEvFd, screen, 15);
    virtKbd.emitKey(KEY_BACKSPACE, 2);
    drainDaemonOutput(outEvFd, screen, 15);
    virtKbd.emitKey(KEY_BACKSPACE, 2);
    drainDaemonOutput(outEvFd, screen, 15);
    virtKbd.emitKey(KEY_BACKSPACE, 2);
    drainDaemonOutput(outEvFd, screen, 15);
    virtKbd.emitKey(KEY_BACKSPACE, 0);
    drainDaemonOutput(outEvFd, screen, 20);
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "a");

    // Continue holding/crossing from buffered word ("a"+"xy" -> "ãy", 2 displayed chars) past empty buffer natively
    typePhysicalString("xy");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "ãy");
    virtKbd.emitKey(KEY_BACKSPACE, 1);
    drainDaemonOutput(outEvFd, screen, 15); // "ã" (1 displayed char left)
    virtKbd.emitKey(KEY_BACKSPACE, 2);
    drainDaemonOutput(outEvFd, screen, 15); // "" (buffer now empty)
    virtKbd.emitKey(KEY_BACKSPACE, 2);
    drainDaemonOutput(outEvFd, screen, 15); // "" (crossed into empty buffer cleanly with no history)
    virtKbd.emitKey(KEY_BACKSPACE, 0);
    drainDaemonOutput(outEvFd, screen, 15);
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "");

    // Shift held slightly overlapping Backspace (uppercase rollover)
    screen.text.clear();
    typePhysicalString("TOANS");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "TOÁN");
    virtKbd.emitKey(KEY_LEFTSHIFT, 1);
    virtKbd.tapKey(KEY_BACKSPACE);
    virtKbd.emitKey(KEY_LEFTSHIFT, 0);
    drainDaemonOutput(outEvFd, screen, 20);
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "TOÁ");
    typePhysicalString("F ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "TOÀ ");

    // 7d. E2E Cross-Space / Multi-word Backspace to re-tone previous word ("xoá lùi về sửa dấu")
    screen.text.clear();
    typePhysicalString("toans ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "toán ");
    pressBackspace(2); // delete ' ' then 'n' -> back to "toá" with active buffer restored!
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "toá");
    typePhysicalString("f ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "toà ");

    screen.text.clear();
    typePhysicalString("vieets hocj");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "viết học");
    pressBackspace(4); // delete 'c','ọ','h',' ', restoring "viết" into active buffer!
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "viết");
    typePhysicalString("j "); // immediately re-tone "viết" -> "việt " without even deleting 't'
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "việt ");

    // Verify Up Arrow Key STILL resets history completely (Left/Right navigate inside tracked words)
    screen.clear();
    typePhysicalString("toans ");
    virtKbd.tapKey(KEY_UP);
    drainDaemonOutput(outEvFd, screen, 15);
    pressBackspace(2);
    typePhysicalString("f");
    EXPECT_BOOL_EQ(u32ToUtf8(screen.text) != "toà", true);
    typePhysicalString(" ");

    // 7e. Arrow-key Left/Right/Delete editing + Custom Hotkeys via evdev & D-Bus ReloadConfig
    screen.clear();
    typePhysicalString("toans ");
    virtKbd.tapKey(KEY_LEFT); // Move left before Space ("toán| ")
    drainDaemonOutput(outEvFd, screen, 15);
    pressBackspace(1); // "toá| "
    typePhysicalString("f"); // "toà| "
    virtKbd.tapKey(KEY_RIGHT); // "toà |"
    drainDaemonOutput(outEvFd, screen, 15);
    typePhysicalString("nay ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "toà nay ");

    screen.clear();
    typePhysicalString("toans hocj");
    for (int i = 0; i < 4; ++i) {
        virtKbd.tapKey(KEY_LEFT);
        drainDaemonOutput(outEvFd, screen, 12);
    }
    pressBackspace(1); // delete 'n' of "toán" before " học"
    typePhysicalString("f");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "toà học");

    // Mid-word Left + Delete + re-tone
    virtKbd.tapKey(KEY_UP);
    drainDaemonOutput(outEvFd, screen, 15);
    screen.clear();
    typePhysicalString("toans");
    virtKbd.tapKey(KEY_LEFT); // "toá|n"
    drainDaemonOutput(outEvFd, screen, 15);
    virtKbd.tapKey(KEY_DELETE); // deletes 'n' -> "toá|"
    drainDaemonOutput(outEvFd, screen, 15);
    typePhysicalString("f ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "toà ");

    // Custom Hotkeys: update config to Alt+Shift, F8, Ctrl+Alt+F12 and ReloadConfig via CLI/D-Bus
    Config customHkCfg = Config::loadFromFile(tmpDir + "/vnsnowkey/config.toml");
    customHkCfg.shortcuts.togglePrimary = "Alt+Shift";
    customHkCfg.shortcuts.toggleSecondary = "F8";
    customHkCfg.shortcuts.panicSafeMode = "Ctrl+Alt+F12";
    customHkCfg.saveToFile(tmpDir + "/vnsnowkey/config.toml");
    runCmd("./build/vnsnowkey ctl reload");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Tap Alt+Shift modifier-only chord -> must toggle VI -> EN
    virtKbd.emitKey(KEY_LEFTALT, 1);
    virtKbd.emitKey(KEY_LEFTSHIFT, 1);
    virtKbd.emitKey(KEY_LEFTSHIFT, 0);
    virtKbd.emitKey(KEY_LEFTALT, 0);
    drainDaemonOutput(outEvFd, screen, 30);
    EXPECT_BOOL_EQ(runCmd("./build/vnsnowkey ctl status").find("\"vietnamese_enabled\":false") != std::string::npos, true);

    // Tap custom F8 -> must toggle EN -> VI
    virtKbd.tapKey(KEY_F8);
    drainDaemonOutput(outEvFd, screen, 30);
    EXPECT_BOOL_EQ(runCmd("./build/vnsnowkey ctl status").find("\"vietnamese_enabled\":true") != std::string::npos, true);

    // Tap custom Ctrl+Alt+F12 -> must activate Safe Mode
    virtKbd.emitKey(KEY_LEFTCTRL, 1);
    virtKbd.emitKey(KEY_LEFTALT, 1);
    virtKbd.tapKey(KEY_F12);
    virtKbd.emitKey(KEY_LEFTALT, 0);
    virtKbd.emitKey(KEY_LEFTCTRL, 0);
    drainDaemonOutput(outEvFd, screen, 30);
    EXPECT_BOOL_EQ(runCmd("./build/vnsnowkey ctl status").find("\"safe_mode\":true") != std::string::npos, true);
    runCmd("./build/vnsnowkey ctl resume");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // 7f. E2E mistyped extra 's' on "tấn" ("taasns" + Backspace -> "tấn") & moder/shader/shift
    screen.clear();
    typePhysicalString("taasns");
    pressBackspace(1);
    typePhysicalString(" ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "tấn ");

    screen.clear();
    typePhysicalString("taasns ");
    pressBackspace(2);
    typePhysicalString(" ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "tấn ");

    screen.clear();
    typePhysicalString("moder shader shift ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "moder shader shift ");

    // Even with auto_restore_english = false (spellcheck still ON), moder/shader/shift must not get bogus tones
    customHkCfg.autoRestoreEnglish = false;
    customHkCfg.saveToFile(tmpDir + "/vnsnowkey/config.toml");
    runCmd("./build/vnsnowkey ctl reload");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    screen.clear();
    typePhysicalString("moder shader shift ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "moder shader shift ");
    customHkCfg.autoRestoreEnglish = true;
    customHkCfg.saveToFile(tmpDir + "/vnsnowkey/config.toml");
    runCmd("./build/vnsnowkey ctl reload");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // 7g. Test Ctrl never gets stuck on Ctrl+Alt+Super or Ctrl+Space hotkeys & Simple Telex w->w
    customHkCfg.inputMethod = "SimpleTelex"; // Legacy/GUI alias must normalize cleanly to "Simple Telex"
    customHkCfg.shortcuts.togglePrimary = "Ctrl+Alt+Super";
    customHkCfg.shortcuts.toggleSecondary = "Ctrl+Space";
    customHkCfg.saveToFile(tmpDir + "/vnsnowkey/config.toml");
    runCmd("./build/vnsnowkey ctl reload");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Simple Telex in VI mode: standalone 'w' stays 'w', "uow" -> "ươ"
    screen.clear();
    typePhysicalString("w window uow ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "w window ươ ");

    // Hold Ctrl + Alt + Super, release Super first -> must toggle VI->EN AND immediately release Ctrl/Alt on uinput
    virtKbd.emitKey(KEY_LEFTCTRL, 1);
    virtKbd.emitKey(KEY_LEFTALT, 1);
    virtKbd.emitKey(KEY_LEFTMETA, 1);
    virtKbd.emitKey(KEY_LEFTMETA, 0);
    drainDaemonOutput(outEvFd, screen, 30);
    EXPECT_BOOL_EQ(runCmd("./build/vnsnowkey ctl status").find("\"vietnamese_enabled\":false") != std::string::npos, true);
    EXPECT_BOOL_EQ(xkb_state_mod_name_is_active(screen.state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) == 0, true);
    EXPECT_BOOL_EQ(xkb_state_mod_name_is_active(screen.state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) == 0, true);
    virtKbd.emitKey(KEY_LEFTALT, 0);
    virtKbd.emitKey(KEY_LEFTCTRL, 0);
    drainDaemonOutput(outEvFd, screen, 15);
    EXPECT_BOOL_EQ(xkb_state_mod_name_is_active(screen.state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) == 0, true);

    // Hold Ctrl, tap Space (Ctrl+Space hotkey) -> toggles EN->VI AND releases Ctrl immediately BEFORE Ctrl UP arrives!
    virtKbd.emitKey(KEY_LEFTCTRL, 1);
    virtKbd.tapKey(KEY_SPACE);
    drainDaemonOutput(outEvFd, screen, 30);
    EXPECT_BOOL_EQ(runCmd("./build/vnsnowkey ctl status").find("\"vietnamese_enabled\":true") != std::string::npos, true);
    EXPECT_BOOL_EQ(xkb_state_mod_name_is_active(screen.state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) == 0, true);
    screen.clear();
    virtKbd.tapKey(KEY_A);
    drainDaemonOutput(outEvFd, screen, 15);
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "a");
    virtKbd.emitKey(KEY_LEFTCTRL, 0);
    drainDaemonOutput(outEvFd, screen, 15);

    // Normal Ctrl press after releasing must still activate Ctrl modifier as usual
    virtKbd.emitKey(KEY_LEFTCTRL, 1);
    drainDaemonOutput(outEvFd, screen, 15);
    EXPECT_BOOL_EQ(xkb_state_mod_name_is_active(screen.state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) != 0, true);
    virtKbd.emitKey(KEY_LEFTCTRL, 0);
    drainDaemonOutput(outEvFd, screen, 15);
    EXPECT_BOOL_EQ(xkb_state_mod_name_is_active(screen.state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) == 0, true);

    customHkCfg.inputMethod = "Telex";
    customHkCfg.autoRestoreEnglish = true;
    customHkCfg.saveToFile(tmpDir + "/vnsnowkey/config.toml");
    runCmd("./build/vnsnowkey ctl reload");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Verify default config already enables Firefox autocompleteFix
    Config defCfg = Config::createDefault();
    const AppProfile* defFf = defCfg.findProfileForApp("org.mozilla.firefox");
    EXPECT_BOOL_EQ(defFf != nullptr && defFf->autocompleteFix, true);

    // 8. Test switching Input Method to VNI via CLI D-Bus
    runCmd("./build/vnsnowkey ctl im VNI");
    screen.text.clear();
    typePhysicalString("Viet65 Nam ");
    EXPECT_STR_EQ(u32ToUtf8(screen.text), "Việt Nam ");

    // 8. Test Panic Stop / Safe Mode & Resume via CLI D-Bus
    runCmd("./build/vnsnowkey ctl panic");
    statusOut = runCmd("./build/vnsnowkey ctl status");
    EXPECT_BOOL_EQ(statusOut.find("\"safe_mode\":true") != std::string::npos, true);
    runCmd("./build/vnsnowkey ctl resume");
    statusOut = runCmd("./build/vnsnowkey ctl status");
    EXPECT_BOOL_EQ(statusOut.find("\"safe_mode\":false") != std::string::npos, true);

    // 8b. Test AppMode::GameMode E2E: raw hold pass-through (W press/repeat/release) for allowed game only
    {
        AppProfile mcProf;
        mcProf.appClass = "minecraft";
        mcProf.mode = AppMode::GameMode;
        customHkCfg.appProfiles.push_back(mcProf);
        customHkCfg.vietnameseEnabled = true;
        customHkCfg.inputMethod = "Telex";
        customHkCfg.saveToFile(tmpDir + "/vnsnowkey/config.toml");
        runCmd("./build/vnsnowkey ctl reload");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        runCmd("dbus-send --session --type=method_call --dest=org.vnsnowkey.Daemon /org/vnsnowkey/Daemon "
               "org.vnsnowkey.Daemon.NotifyWindowFocus string:\"minecraft\" string:\"minecraft\" "
               "string:\"Minecraft* 1.21\" string:\"w_mc\"");
        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        std::string mcStatus = runCmd("./build/vnsnowkey ctl status");
        EXPECT_BOOL_EQ(mcStatus.find("\"game_mode_active\":true") != std::string::npos, true);
        EXPECT_BOOL_EQ(mcStatus.find("\"game_chat_active\":false") != std::string::npos, true);

        // Drain any stale output, then hold KEY_W: press(1), repeat(2), release(0)
        drainDaemonOutput(outEvFd, screen, 15);
        virtKbd.emitKey(KEY_W, 1);
        virtKbd.emitKey(KEY_W, 2);
        virtKbd.emitKey(KEY_W, 0);

        std::vector<int32_t> wValues;
        auto mcDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
        while (std::chrono::steady_clock::now() < mcDeadline) {
            pollfd pfd{outEvFd, POLLIN, 0};
            if (::poll(&pfd, 1, 20) <= 0) break;
            input_event evs[64];
            ssize_t rd = ::read(outEvFd, evs, sizeof(evs));
            if (rd <= 0) break;
            size_t count = static_cast<size_t>(rd) / sizeof(input_event);
            for (size_t i = 0; i < count; ++i) {
                if (evs[i].type == EV_KEY && evs[i].code == KEY_W) {
                    wValues.push_back(evs[i].value);
                }
            }
        }
        EXPECT_BOOL_EQ(wValues.size() == 3, true);
        if (wValues.size() == 3) {
            EXPECT_BOOL_EQ(wValues[0] == 1 && wValues[1] == 2 && wValues[2] == 0, true);
        }

        // Wayland-native fallback: 't' enters Vietnamese chat -> "taasn truyeefn" -> "tấn truyền" -> Enter exits back to raw movement
        customHkCfg.inputMethod = "Simple Telex";
        customHkCfg.saveToFile(tmpDir + "/vnsnowkey/config.toml");
        runCmd("./build/vnsnowkey ctl reload");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        screen.clear();
        virtKbd.tapKey(KEY_T);
        drainDaemonOutput(outEvFd, screen, 20);
        EXPECT_BOOL_EQ(runCmd("./build/vnsnowkey ctl status").find("\"game_chat_active\":true") != std::string::npos, true);
        screen.clear();
        typePhysicalString("taasn truyeefn");
        EXPECT_STR_EQ(u32ToUtf8(screen.text), "tấn truyền");

        virtKbd.tapKey(KEY_ENTER);
        drainDaemonOutput(outEvFd, screen, 20);
        EXPECT_BOOL_EQ(runCmd("./build/vnsnowkey ctl status").find("\"game_chat_active\":false") != std::string::npos, true);

        // Stuck-visible DesktopCursor (Mutter/QEMU/mouse-click menu close): Esc or held KEY_W repeat (value == 2)
        // must never deadlock and must restore raw WASD pass-through.
        runCmd("dbus-send --session --type=method_call --dest=org.vnsnowkey.Daemon /org/vnsnowkey/Daemon "
               "org.vnsnowkey.Daemon.NotifyDesktopCursorVisible boolean:false");
        runCmd("dbus-send --session --type=method_call --dest=org.vnsnowkey.Daemon /org/vnsnowkey/Daemon "
               "org.vnsnowkey.Daemon.NotifyDesktopCursorVisible boolean:true");
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        EXPECT_BOOL_EQ(runCmd("./build/vnsnowkey ctl status").find("\"game_chat_active\":true") != std::string::npos, true);

        // 1) Pressing Esc while cursor stays visible overrides stuck cursor cleanly without deadlocking
        virtKbd.tapKey(KEY_ESC);
        drainDaemonOutput(outEvFd, screen, 20);
        EXPECT_BOOL_EQ(runCmd("./build/vnsnowkey ctl status").find("\"game_chat_active\":false") != std::string::npos, true);

        // 2) Re-arm chat via '/' (stuck cursor true) -> holding KEY_W repeat (value == 2) rescues movement
        virtKbd.tapKey(KEY_SLASH);
        drainDaemonOutput(outEvFd, screen, 20);
        EXPECT_BOOL_EQ(runCmd("./build/vnsnowkey ctl status").find("\"game_chat_active\":true") != std::string::npos, true);
        drainDaemonOutput(outEvFd, screen, 10);
        virtKbd.emitKey(KEY_W, 1);
        virtKbd.emitKey(KEY_W, 2);
        virtKbd.emitKey(KEY_W, 0);
        drainDaemonOutput(outEvFd, screen, 25);
        EXPECT_BOOL_EQ(runCmd("./build/vnsnowkey ctl status").find("\"game_chat_active\":false") != std::string::npos, true);

        // 3) Sneaking (Shift+W repeat) also rescues out of stuck-cursor chat mode, and Flatpak PrismLauncher matches Minecraft
        virtKbd.tapKey(KEY_T);
        drainDaemonOutput(outEvFd, screen, 20);
        EXPECT_BOOL_EQ(runCmd("./build/vnsnowkey ctl status").find("\"game_chat_active\":true") != std::string::npos, true);
        virtKbd.emitKey(KEY_LEFTSHIFT, 1);
        virtKbd.emitKey(KEY_W, 1);
        virtKbd.emitKey(KEY_W, 2);
        virtKbd.emitKey(KEY_W, 0);
        virtKbd.emitKey(KEY_LEFTSHIFT, 0);
        drainDaemonOutput(outEvFd, screen, 25);
        EXPECT_BOOL_EQ(runCmd("./build/vnsnowkey ctl status").find("\"game_chat_active\":false") != std::string::npos, true);

        runCmd("dbus-send --session --type=method_call --dest=org.vnsnowkey.Daemon /org/vnsnowkey/Daemon "
               "org.vnsnowkey.Daemon.NotifyWindowFocus string:\"org.prismlauncher.PrismLauncher\" "
               "string:\"org.prismlauncher.PrismLauncher\" string:\"Minecraft* 26.3 - Multiplayer\" string:\"99\"");
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        EXPECT_BOOL_EQ(runCmd("./build/vnsnowkey ctl status").find("\"game_mode_active\":true") != std::string::npos, true);

        // Switch back to Telex before kate test
        customHkCfg.inputMethod = "Telex";
        customHkCfg.saveToFile(tmpDir + "/vnsnowkey/config.toml");
        runCmd("./build/vnsnowkey ctl reload");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Focus a regular app -> GameMode turns off and Vietnamese typing resumes immediately
        runCmd("dbus-send --session --type=method_call --dest=org.vnsnowkey.Daemon /org/vnsnowkey/Daemon "
               "org.vnsnowkey.Daemon.NotifyWindowFocus string:\"org.kde.kate\" string:\"kate\" "
               "string:\"Notes.txt\" string:\"w_kate\"");
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        std::string kateStatus = runCmd("./build/vnsnowkey ctl status");
        EXPECT_BOOL_EQ(kateStatus.find("\"game_mode_active\":false") != std::string::npos, true);
        screen.clear();
        typePhysicalString("tooi ");
        EXPECT_STR_EQ(u32ToUtf8(screen.text), "tôi ");

        // 8b. Test resetOnMouseClick = false via real evdev mouse + kbd (no lost Vietnamese, no "eejtnam" jump)
        customHkCfg.resetOnMouseClick = false;
        customHkCfg.saveToFile(tmpDir + "/vnsnowkey/config.toml");
        runCmd("./build/vnsnowkey ctl reload");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // 8b-1. English/invalid word before click must NOT lock Vietnamese in new field
        screen.clear();
        typePhysicalString("fedora");
        virtMouse.emitKey(BTN_LEFT, 1);
        virtMouse.emitKey(BTN_LEFT, 0);
        drainDaemonOutput(outEvFd, screen, 15);
        screen.clear();
        typePhysicalString("tieengs vieejt ");
        EXPECT_STR_EQ(u32ToUtf8(screen.text), "tiếng việt ");

        // 8b-2. Uncommitted Vietnamese word before click + diverging new word must NOT concatenate or jump
        screen.clear();
        typePhysicalString("vieejt");
        virtMouse.emitKey(BTN_LEFT, 1);
        virtMouse.emitKey(BTN_LEFT, 0);
        drainDaemonOutput(outEvFd, screen, 15);
        screen.clear();
        typePhysicalString("nam ");
        EXPECT_STR_EQ(u32ToUtf8(screen.text), "nam ");

        // 8b-3. Clicking in place still allows continuing the active word ("hoa" + click + "s" => "hoá")
        screen.clear();
        typePhysicalString("hoa");
        virtMouse.emitKey(BTN_LEFT, 1);
        virtMouse.emitKey(BTN_LEFT, 0);
        drainDaemonOutput(outEvFd, screen, 15);
        typePhysicalString("s ");
        EXPECT_STR_EQ(u32ToUtf8(screen.text), "hoá ");

        // 8b-4. BTN_RIGHT resets safely even with resetOnMouseClick = false
        screen.clear();
        typePhysicalString("hoa");
        virtMouse.emitKey(BTN_RIGHT, 1);
        virtMouse.emitKey(BTN_RIGHT, 0);
        drainDaemonOutput(outEvFd, screen, 15);
        typePhysicalString("s ");
        EXPECT_STR_EQ(u32ToUtf8(screen.text), "hoas ");
    }

    // 9. Test SIGTERM Clean Exit (daemon must exit cleanly and release EVIOCGRAB on virtKbd)
    ::kill(daemonPid, SIGTERM);
    int wstatus = 0;
    ::waitpid(daemonPid, &wstatus, 0);
    EXPECT_BOOL_EQ(WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0, true);

    // Verify virtKbd is no longer grabbed after vnsnowkeyd exited!
    int checkKbdFd = ::open(virtKbd.eventPath.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    EXPECT_BOOL_EQ(checkKbdFd >= 0, true);
    if (checkKbdFd >= 0) {
        int reGrab = ::ioctl(checkKbdFd, EVIOCGRAB, 1);
        EXPECT_BOOL_EQ(reGrab == 0, true);
        ::ioctl(checkKbdFd, EVIOCGRAB, 0);
        ::close(checkKbdFd);
    }

    if (outEvFd >= 0) {
        ::ioctl(outEvFd, EVIOCGRAB, 0);
        ::close(outEvFd);
    }
    std::filesystem::remove_all(tmpDir);

    std::cout << "E2E Daemon & D-Bus Integration Tests Summary: "
              << g_passed << " passed, " << g_failed << " failed.\n";
    return g_failed == 0 ? 0 : 1;
}
