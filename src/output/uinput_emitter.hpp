/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "../config/config.hpp"
#include "xkb_overlay.hpp"
#include <atomic>
#include <bitset>
#include <cstdint>
#include <functional>
#include <linux/input.h>
#include <mutex>
#include <string>
#include <vector>

namespace vnsnowkey {

class UinputEmitter {
public:
    using ClipboardGetFn = std::function<std::string()>;
    using ClipboardSetFn = std::function<void(const std::string&)>;

    UinputEmitter();
    ~UinputEmitter();

    UinputEmitter(const UinputEmitter&) = delete;
    UinputEmitter& operator=(const UinputEmitter&) = delete;

    // Create virtual keyboard on /dev/uinput
    bool initialize(std::string* errorMsg = nullptr);
    void shutdown();
    bool isInitialized() const { return uinputFd_ >= 0; }
    int fd() const { return uinputFd_; }

    // Find the /dev/input/eventX path corresponding to this virtual device (useful for test harness)
    std::string findSysInputEventPath() const;

    // Forward a raw physical key event (press=1, release=0, repeat=2)
    void forwardKeyEvent(uint16_t code, int32_t value);

    // Emit an atomic diff (Backspaces + replacement text) using the specified OutputMethod
    void emitDiff(
        int backspaces,
        std::u32string_view textToEmit,
        std::string_view utf8ToEmit,
        OutputMethod method = OutputMethod::XkbKeymap,
        int delayUs = 0,
        bool autocompleteFix = false,
        int clipboardRestoreDelayMs = 45,
        bool compositorCapsLock = false);

    // Emit an inert modifier tap (KEY_ISO) to cancel single-modifier launcher triggers (e.g. Kickoff/Overview)
    void emitCancelSuperTap();

    // Immediately release all keys currently depressed on the virtual keyboard
    void releaseAllKeys();

    // Inspect whether any key is currently depressed on the virtual keyboard
    bool hasAnyPressedKey() const;

    // Live compositor LED_CAPSL feedback received via EV_LED on the uinput device
    bool hasVirtualCapsLed() const;
    bool virtualCapsLockOn() const;

    // Custom clipboard hooks (defaults to Klipper D-Bus / wl-clipboard / xclip / xsel fallback)
    void setClipboardHooks(ClipboardGetFn getFn, ClipboardSetFn setFn);

    // Cross-desktop system clipboard helpers (Klipper D-Bus / wl-clipboard / xclip / xsel)
    static bool readSystemClipboard(std::string& outText);
    static bool writeSystemClipboard(const std::string& text);
    static bool hasClipboardBackend();

    const XkbOverlay& xkbOverlay() const { return xkbOverlay_; }

    // Vendor 0x0FAC is also recognized by low-level remappers (e.g., keyd) as a virtual
    // keyboard so keyd with [ids] * will not grab VNSnowKey's output device, while VNSnowKey
    // distinguishes its own device via Product 0x534B ("SK", "VNSnowKey Virtual Keyboard").
    static constexpr uint16_t kVNSnowKeyVendorId  = 0x0FAC;
    static constexpr uint16_t kVNSnowKeyProductId = 0x534B; // "SK"
    static constexpr const char* kVirtualDeviceName = "VNSnowKey Virtual Keyboard";

private:
    void appendKey(std::vector<input_event>& batch, uint16_t code, int32_t value);
    void appendSyn(std::vector<input_event>& batch);
    void flushBatch(const std::vector<input_event>& batch, int delayUs);
    void pollVirtualLedEventsLocked() const;

    void emitViaXkbKeymap(std::vector<input_event>& batch, std::u32string_view text, int delayUs, bool compositorCapsLock);
    void emitViaCtrlShiftU(std::vector<input_event>& batch, std::u32string_view text, int delayUs, bool compositorCapsLock);
    void emitViaClipboard(
        std::vector<input_event>& batch,
        std::string_view utf8Text,
        bool useCtrlShiftV,
        int delayUs,
        int restoreDelayMs);

    static std::string defaultGetClipboard();
    static void defaultSetClipboard(const std::string& text);

    int uinputFd_ = -1;
    mutable std::mutex mutex_;
    XkbOverlay xkbOverlay_;
    std::bitset<KEY_MAX + 1> activeKeys_;
    bool physShiftLeft_ = false;
    bool physShiftRight_ = false;
    mutable bool hasVirtualCapsLed_ = false;
    mutable bool virtualCapsLockOn_ = false;

    ClipboardGetFn clipboardGetFn_;
    ClipboardSetFn clipboardSetFn_;
    std::atomic<uint64_t> clipboardSeq_{0};
    std::string savedClipboardContent_;
    bool clipboardSavePending_ = false;
};

} // namespace vnsnowkey
