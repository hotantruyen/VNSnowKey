/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "uinput_emitter.hpp"
#include "../engine/bamboo_utils.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <linux/uinput.h>
#include <sstream>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

namespace vnsnowkey {

UinputEmitter::UinputEmitter()
    : clipboardGetFn_(defaultGetClipboard),
      clipboardSetFn_(defaultSetClipboard) {}

UinputEmitter::~UinputEmitter() {
    shutdown();
}

bool UinputEmitter::initialize(std::string* errorMsg) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (uinputFd_ >= 0) {
        return true;
    }

    int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (errorMsg) {
            *errorMsg = std::string("Cannot open /dev/uinput: ") + std::strerror(errno);
        }
        return false;
    }

    ::ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ::ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ::ioctl(fd, UI_SET_EVBIT, EV_MSC);
    ::ioctl(fd, UI_SET_MSCBIT, MSC_SCAN);

    // Enable all standard keyboard keys including F13..F24
    for (int code = 1; code < 248; ++code) {
        ::ioctl(fd, UI_SET_KEYBIT, code);
    }

    struct uinput_setup usetup{};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = kVNSnowKeyVendorId;
    usetup.id.product = kVNSnowKeyProductId;
    usetup.id.version = 1;
    std::snprintf(usetup.name, UINPUT_MAX_NAME_SIZE, "%s", kVirtualDeviceName);

    if (::ioctl(fd, UI_DEV_SETUP, &usetup) < 0) {
        if (errorMsg) {
            *errorMsg = std::string("UI_DEV_SETUP failed: ") + std::strerror(errno);
        }
        ::close(fd);
        return false;
    }

    if (::ioctl(fd, UI_DEV_CREATE) < 0) {
        if (errorMsg) {
            *errorMsg = std::string("UI_DEV_CREATE failed: ") + std::strerror(errno);
        }
        ::close(fd);
        return false;
    }

    uinputFd_ = fd;
    activeKeys_.reset();
    physShiftLeft_ = false;
    physShiftRight_ = false;
    return true;
}

void UinputEmitter::shutdown() {
    releaseAllKeys();
    std::lock_guard<std::mutex> lock(mutex_);
    if (uinputFd_ >= 0) {
        ::ioctl(uinputFd_, UI_DEV_DESTROY);
        ::close(uinputFd_);
        uinputFd_ = -1;
    }
}

std::string UinputEmitter::findSysInputEventPath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (uinputFd_ < 0) return {};

    char sysname[64]{};
    if (::ioctl(uinputFd_, UI_GET_SYSNAME(sizeof(sysname)), sysname) >= 0 && sysname[0] != '\0') {
        std::string sysDir = std::string("/sys/devices/virtual/input/") + sysname;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(sysDir)) {
                std::string fname = entry.path().filename().string();
                if (fname.rfind("event", 0) == 0) {
                    return "/dev/input/" + fname;
                }
            }
        } catch (...) {}
    }
    return {};
}

void UinputEmitter::appendKey(std::vector<input_event>& batch, uint16_t code, int32_t value) {
    if (code <= KEY_MAX) {
        if (value == 1) {
            activeKeys_.set(code, true);
        } else if (value == 0) {
            activeKeys_.set(code, false);
        }
    }
    input_event ev{};
    ev.type = EV_KEY;
    ev.code = code;
    ev.value = value;
    batch.push_back(ev);
}

void UinputEmitter::appendSyn(std::vector<input_event>& batch) {
    input_event ev{};
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    batch.push_back(ev);
}

void UinputEmitter::flushBatch(const std::vector<input_event>& batch, int delayUs) {
    if (uinputFd_ < 0 || batch.empty()) return;

    // Note: Linux kernel drivers/input/misc/uinput.c (uinput_write) requires each write()
    // call to be exactly sizeof(struct input_event). However, the kernel buffers all EV_KEY
    // events inside input_dev->vals[] and only wakes up evdev readers when EV_SYN (SYN_REPORT)
    // is written. Writing all events back-to-back with delayUs == 0 delivers them with zero
    // sleep delay.
    for (const auto& ev : batch) {
        while (true) {
            ssize_t written = ::write(uinputFd_, &ev, sizeof(input_event));
            if (written < 0 && (errno == EINTR || errno == EAGAIN)) {
                continue;
            }
            break;
        }
        if (delayUs > 0 && ev.type == EV_SYN && ev.code == SYN_REPORT) {
            std::this_thread::sleep_for(std::chrono::microseconds(delayUs));
        }
    }
}

void UinputEmitter::forwardKeyEvent(uint16_t code, int32_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (uinputFd_ < 0) return;

    if (code == KEY_LEFTSHIFT) {
        physShiftLeft_ = (value != 0);
    } else if (code == KEY_RIGHTSHIFT) {
        physShiftRight_ = (value != 0);
    }

    std::vector<input_event> batch;
    batch.reserve(2);
    appendKey(batch, code, value);
    appendSyn(batch);
    flushBatch(batch, 0);
}

void UinputEmitter::emitCancelSuperTap() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (uinputFd_ < 0) return;
    std::vector<input_event> batch;
    batch.reserve(4);
    appendKey(batch, kVNSnowKeyLevel3Evdev, 1);
    appendSyn(batch);
    appendKey(batch, kVNSnowKeyLevel3Evdev, 0);
    appendSyn(batch);
    flushBatch(batch, 0);
}

void UinputEmitter::releaseAllKeys() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (uinputFd_ < 0) return;

    std::vector<input_event> batch;
    for (size_t code = 1; code <= KEY_MAX; ++code) {
        if (activeKeys_.test(code)) {
            appendKey(batch, static_cast<uint16_t>(code), 0);
        }
    }
    physShiftLeft_ = false;
    physShiftRight_ = false;
    if (!batch.empty()) {
        appendSyn(batch);
        flushBatch(batch, 0);
    }
}

bool UinputEmitter::hasAnyPressedKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeKeys_.any();
}

void UinputEmitter::emitViaXkbKeymap(std::vector<input_event>& batch, std::u32string_view text, int delayUs) {
    bool curShift = false;
    bool curL3 = false;
    bool curL5 = false;

    for (char32_t cp : text) {
        auto chordOpt = xkbOverlay_.lookupChar(cp);
        if (!chordOpt.has_value()) {
            // Fallback for any character outside the 134 Vietnamese + 95 ASCII set (e.g. emoji in Macro)
            flushBatch(batch, delayUs);
            batch.clear();
            std::string singleUtf8 = bamboo::u32CharToUtf8(cp);
            emitViaClipboard(batch, singleUtf8, false, delayUs, 45);
            continue;
        }
        const KeyChord& chord = *chordOpt;
        bool modChanged = false;
        if (curShift != chord.shift) {
            appendKey(batch, KEY_LEFTSHIFT, chord.shift ? 1 : 0);
            curShift = chord.shift;
            modChanged = true;
        }
        if (curL3 != chord.level3) {
            appendKey(batch, kVNSnowKeyLevel3Evdev, chord.level3 ? 1 : 0);
            curL3 = chord.level3;
            modChanged = true;
        }
        if (curL5 != chord.level5) {
            appendKey(batch, kVNSnowKeyLevel5Evdev, chord.level5 ? 1 : 0);
            curL5 = chord.level5;
            modChanged = true;
        }
        if (modChanged) {
            appendSyn(batch);
        }

        appendKey(batch, chord.keycode, 1);
        appendSyn(batch);
        appendKey(batch, chord.keycode, 0);
        appendSyn(batch);
    }

    bool cleanupMod = false;
    if (curL3) {
        appendKey(batch, kVNSnowKeyLevel3Evdev, 0);
        cleanupMod = true;
    }
    if (curL5) {
        appendKey(batch, kVNSnowKeyLevel5Evdev, 0);
        cleanupMod = true;
    }
    if (curShift != physShiftLeft_) {
        appendKey(batch, KEY_LEFTSHIFT, physShiftLeft_ ? 1 : 0);
        cleanupMod = true;
    }
    if (physShiftRight_ && !activeKeys_.test(KEY_RIGHTSHIFT)) {
        appendKey(batch, KEY_RIGHTSHIFT, 1);
        cleanupMod = true;
    }
    if (cleanupMod) {
        appendSyn(batch);
    }

    flushBatch(batch, delayUs);
}

void UinputEmitter::emitViaCtrlShiftU(std::vector<input_event>& batch, std::u32string_view text, int delayUs) {
    static const uint16_t kHexKeycodes[16] = {
        KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7,
        KEY_8, KEY_9, KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F
    };

    for (char32_t cp : text) {
        if (cp < 0x80) {
            auto chordOpt = xkbOverlay_.lookupChar(cp);
            if (chordOpt.has_value()) {
                if (chordOpt->shift) {
                    appendKey(batch, KEY_LEFTSHIFT, 1);
                    appendSyn(batch);
                }
                appendKey(batch, chordOpt->keycode, 1);
                appendSyn(batch);
                appendKey(batch, chordOpt->keycode, 0);
                appendSyn(batch);
                if (chordOpt->shift) {
                    appendKey(batch, KEY_LEFTSHIFT, 0);
                    appendSyn(batch);
                }
            }
            continue;
        }

        // Ctrl+Shift+U
        appendKey(batch, KEY_LEFTCTRL, 1);
        appendKey(batch, KEY_LEFTSHIFT, 1);
        appendSyn(batch);
        appendKey(batch, KEY_U, 1);
        appendSyn(batch);
        appendKey(batch, KEY_U, 0);
        appendKey(batch, KEY_LEFTSHIFT, 0);
        appendKey(batch, KEY_LEFTCTRL, 0);
        appendSyn(batch);

        // Hex digits of cp
        std::ostringstream hexOss;
        hexOss << std::hex << static_cast<uint32_t>(cp);
        std::string hexStr = hexOss.str();
        for (char hc : hexStr) {
            int digit = (hc >= '0' && hc <= '9') ? (hc - '0') : (hc - 'a' + 10);
            if (digit >= 0 && digit < 16) {
                appendKey(batch, kHexKeycodes[digit], 1);
                appendSyn(batch);
                appendKey(batch, kHexKeycodes[digit], 0);
                appendSyn(batch);
            }
        }

        // Commit with Enter
        appendKey(batch, KEY_ENTER, 1);
        appendSyn(batch);
        appendKey(batch, KEY_ENTER, 0);
        appendSyn(batch);
    }

    // Restore physical Shift state
    if (physShiftLeft_ && !activeKeys_.test(KEY_LEFTSHIFT)) {
        appendKey(batch, KEY_LEFTSHIFT, 1);
        appendSyn(batch);
    }
    if (physShiftRight_ && !activeKeys_.test(KEY_RIGHTSHIFT)) {
        appendKey(batch, KEY_RIGHTSHIFT, 1);
        appendSyn(batch);
    }

    flushBatch(batch, delayUs);
}

void UinputEmitter::emitViaClipboard(
    std::vector<input_event>& batch,
    std::string_view utf8Text,
    bool useCtrlShiftV,
    int delayUs,
    int restoreDelayMs) {
    // 1. Flush any pending Backspaces first
    if (!batch.empty()) {
        flushBatch(batch, delayUs);
        batch.clear();
    }
    if (utf8Text.empty()) {
        std::vector<input_event> restoreBatch;
        if (physShiftLeft_ && !activeKeys_.test(KEY_LEFTSHIFT)) {
            appendKey(restoreBatch, KEY_LEFTSHIFT, 1);
        }
        if (physShiftRight_ && !activeKeys_.test(KEY_RIGHTSHIFT)) {
            appendKey(restoreBatch, KEY_RIGHTSHIFT, 1);
        }
        if (!restoreBatch.empty()) {
            appendSyn(restoreBatch);
            flushBatch(restoreBatch, delayUs);
        }
        return;
    }

    // 2. Save user's original clipboard only if we are not already mid-restore (prevents race condition!)
    if (!clipboardSavePending_ && clipboardGetFn_) {
        savedClipboardContent_ = clipboardGetFn_();
        clipboardSavePending_ = true;
    }

    // 3. Set clipboard to the target UTF-8 text
    if (clipboardSetFn_) {
        clipboardSetFn_(std::string(utf8Text));
    }

    // Brief sync pause so Klipper / Wayland data-control updates clipboard offer before Ctrl+V arrives
    std::this_thread::sleep_for(std::chrono::milliseconds(6));

    // 4. Emit Ctrl+V (or Ctrl+Shift+V for terminals)
    appendKey(batch, KEY_LEFTCTRL, 1);
    if (useCtrlShiftV) {
        appendKey(batch, KEY_LEFTSHIFT, 1);
    }
    appendSyn(batch);
    appendKey(batch, KEY_V, 1);
    appendSyn(batch);
    appendKey(batch, KEY_V, 0);
    if (useCtrlShiftV) {
        appendKey(batch, KEY_LEFTSHIFT, 0);
    }
    appendKey(batch, KEY_LEFTCTRL, 0);
    if (physShiftLeft_) appendKey(batch, KEY_LEFTSHIFT, 1);
    if (physShiftRight_) appendKey(batch, KEY_RIGHTSHIFT, 1);
    appendSyn(batch);
    flushBatch(batch, delayUs);
    batch.clear();

    // 5. Asynchronously restore user's previous clipboard after restoreDelayMs if no newer paste occurred
    uint64_t seq = ++clipboardSeq_;
    std::string toRestore = savedClipboardContent_;
    auto setFn = clipboardSetFn_;
    int waitMs = std::max(15, restoreDelayMs);
    std::thread([this, seq, toRestore, setFn, waitMs]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
        std::lock_guard<std::mutex> lk(mutex_);
        if (clipboardSeq_.load() == seq && clipboardSavePending_) {
            if (setFn) {
                setFn(toRestore);
            }
            clipboardSavePending_ = false;
        }
    }).detach();
}

void UinputEmitter::emitDiff(
    int backspaces,
    std::u32string_view textToEmit,
    std::string_view utf8ToEmit,
    OutputMethod method,
    int delayUs,
    bool autocompleteFix,
    int clipboardRestoreDelayMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (uinputFd_ < 0) return;

    std::vector<input_event> batch;
    batch.reserve(static_cast<size_t>(backspaces * 3 + textToEmit.size() * 6 + 12));

    // Temporarily release physical Shift so Backspaces and chord modifiers are clean
    bool releasedShift = false;
    if (activeKeys_.test(KEY_LEFTSHIFT)) {
        appendKey(batch, KEY_LEFTSHIFT, 0);
        releasedShift = true;
    }
    if (activeKeys_.test(KEY_RIGHTSHIFT)) {
        appendKey(batch, KEY_RIGHTSHIFT, 0);
        releasedShift = true;
    }
    if (releasedShift) {
        appendSyn(batch);
    }

    // Browser URL bar inline-autocomplete workaround:
    // When inline autocomplete is highlighted after the cursor (e.g. "gi[thub.com]"),
    // sending [Space, Backspace] overwrites the highlighted selection and deletes the space,
    // leaving the cursor right after "gi" with no selection so the subsequent Backspaces delete "i" accurately!
    if (autocompleteFix && backspaces > 0) {
        appendKey(batch, KEY_SPACE, 1);
        appendSyn(batch);
        appendKey(batch, KEY_SPACE, 0);
        appendSyn(batch);
        appendKey(batch, KEY_BACKSPACE, 1);
        appendSyn(batch);
        appendKey(batch, KEY_BACKSPACE, 0);
        appendSyn(batch);
    }

    for (int i = 0; i < backspaces; ++i) {
        appendKey(batch, KEY_BACKSPACE, 1);
        appendSyn(batch);
        appendKey(batch, KEY_BACKSPACE, 0);
        appendSyn(batch);
    }

    switch (method) {
        case OutputMethod::XkbKeymap:
            emitViaXkbKeymap(batch, textToEmit, delayUs);
            break;
        case OutputMethod::CtrlShiftU:
            emitViaCtrlShiftU(batch, textToEmit, delayUs);
            break;
        case OutputMethod::ClipboardCtrlV:
            emitViaClipboard(batch, utf8ToEmit, false, delayUs, clipboardRestoreDelayMs);
            break;
        case OutputMethod::ClipboardCtrlShiftV:
            emitViaClipboard(batch, utf8ToEmit, true, delayUs, clipboardRestoreDelayMs);
            break;
    }
}

void UinputEmitter::setClipboardHooks(ClipboardGetFn getFn, ClipboardSetFn setFn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (getFn) clipboardGetFn_ = std::move(getFn);
    if (setFn) clipboardSetFn_ = std::move(setFn);
}

bool UinputEmitter::hasClipboardBackend() {
    if (std::system(
            "sh -c '"
            "if [ -n \"$WAYLAND_DISPLAY\" ] && command -v wl-copy >/dev/null 2>&1; then exit 0; fi; "
            "if command -v qdbus6 >/dev/null 2>&1 && qdbus6 org.kde.klipper /klipper >/dev/null 2>&1; then exit 0; fi; "
            "if command -v qdbus-qt6 >/dev/null 2>&1 && qdbus-qt6 org.kde.klipper /klipper >/dev/null 2>&1; then exit 0; fi; "
            "if command -v qdbus >/dev/null 2>&1 && qdbus org.kde.klipper /klipper >/dev/null 2>&1; then exit 0; fi; "
            "if command -v gdbus >/dev/null 2>&1 && gdbus call --session --dest org.kde.klipper --object-path /klipper --method org.freedesktop.DBus.Peer.Ping >/dev/null 2>&1; then exit 0; fi; "
            "if [ -n \"$DISPLAY\" ] && (command -v xclip >/dev/null 2>&1 || command -v xsel >/dev/null 2>&1); then exit 0; fi; "
            "exit 1' >/dev/null 2>&1") == 0) {
        return true;
    }
    return false;
}

bool UinputEmitter::readSystemClipboard(std::string& outText) {
    outText.clear();
    auto readPipe = [](const char* cmd, bool trimTrailingNewline) -> std::pair<bool, std::string> {
        FILE* pipe = ::popen(cmd, "r");
        if (!pipe) return {false, {}};
        std::string result;
        char buf[512];
        while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
            result += buf;
        }
        int status = ::pclose(pipe);
        if (status != 0) return {false, {}};
        if (trimTrailingNewline && !result.empty() && result.back() == '\n') {
            result.pop_back();
        }
        return {true, result};
    };

    for (const char* cmd : {
             "qdbus6 org.kde.klipper /klipper org.kde.klipper.klipper.getClipboardContents 2>/dev/null",
             "qdbus-qt6 org.kde.klipper /klipper org.kde.klipper.klipper.getClipboardContents 2>/dev/null",
             "qdbus org.kde.klipper /klipper org.kde.klipper.klipper.getClipboardContents 2>/dev/null",
         }) {
        if (auto [ok, val] = readPipe(cmd, true); ok) {
            outText = std::move(val);
            return true;
        }
    }
    if (XkbOverlay::isWaylandSession()) {
        if (auto [ok, val] = readPipe("wl-paste --no-newline 2>/dev/null", false); ok) {
            outText = std::move(val);
            return true;
        }
    }
    std::string x11Disp = XkbOverlay::resolveX11Display();
    if (!x11Disp.empty()) {
        if (auto [ok, val] = readPipe("xclip -o -selection clipboard 2>/dev/null", false); ok) {
            outText = std::move(val);
            return true;
        }
        if (auto [ok, val] = readPipe("xsel -ob 2>/dev/null", false); ok) {
            outText = std::move(val);
            return true;
        }
    }
    return false;
}

bool UinputEmitter::writeSystemClipboard(const std::string& text) {
    // Ensure DISPLAY/XAUTHORITY are populated when running under systemd --user
    (void)XkbOverlay::resolveX11Display();
    // Pass arbitrary UTF-8 safely via environment variable or stdin without shell-escaping hazards
    setenv("VNSNOWKEY_CLIP_TMP", text.c_str(), 1);
    int rc = std::system(
        "sh -c '"
        "for q in qdbus6 qdbus-qt6 qdbus; do "
        "  if command -v \"$q\" >/dev/null 2>&1 && "
        "     \"$q\" org.kde.klipper /klipper org.kde.klipper.klipper.setClipboardContents \"$VNSNOWKEY_CLIP_TMP\" >/dev/null 2>&1; then exit 0; fi; "
        "done; "
        "if [ -n \"$WAYLAND_DISPLAY\" ] && command -v wl-copy >/dev/null 2>&1; then "
        "printf \"%s\" \"$VNSNOWKEY_CLIP_TMP\" | wl-copy >/dev/null 2>&1 && exit 0; fi; "
        "if [ -n \"$DISPLAY\" ] && command -v xclip >/dev/null 2>&1; then "
        "printf \"%s\" \"$VNSNOWKEY_CLIP_TMP\" | xclip -i -selection clipboard >/dev/null 2>&1 && exit 0; fi; "
        "if [ -n \"$DISPLAY\" ] && command -v xsel >/dev/null 2>&1; then "
        "printf \"%s\" \"$VNSNOWKEY_CLIP_TMP\" | xsel -ib >/dev/null 2>&1 && exit 0; fi; "
        "exit 1"
        "' >/dev/null 2>&1");
    unsetenv("VNSNOWKEY_CLIP_TMP");
    return rc == 0;
}

std::string UinputEmitter::defaultGetClipboard() {
    std::string val;
    readSystemClipboard(val);
    return val;
}

void UinputEmitter::defaultSetClipboard(const std::string& text) {
    (void)writeSystemClipboard(text);
}

} // namespace vnsnowkey
