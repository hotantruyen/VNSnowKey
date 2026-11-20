/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "../config/config.hpp"
#include "../engine/session_processor.hpp"
#include "../output/uinput_emitter.hpp"
#include <atomic>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <functional>
#include <linux/input.h>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace vnsnowkey {

struct GrabbedDeviceInfo {
    int fd = -1;
    std::string path;
    std::string name;
    bool isKeyboard = false;
    bool isMouse = false;
    bool isGrabbed = false;
    uint16_t heldModMask = 0;
};

class EvdevCapture {
public:
    using LanguageToggleCallback = std::function<void(bool newVietnameseEnabled)>;
    using PanicModeCallback = std::function<void()>;

    EvdevCapture(bamboo::SessionProcessor& processor, UinputEmitter& emitter);
    ~EvdevCapture();

    EvdevCapture(const EvdevCapture&) = delete;
    EvdevCapture& operator=(const EvdevCapture&) = delete;

    // Start background evdev poll + hotplug thread (or attach to specific device paths for test harness)
    bool start(const std::vector<std::string>& explicitKeyboardPaths = {});
    void stop();
    bool isRunning() const { return running_.load(); }

    // Update active configuration & active window profile
    void updateConfig(const Config& cfg);
    void setVietnameseEnabled(bool enabled);
    bool vietnameseEnabled() const;
    uint64_t langToggleEpoch() const { return langToggleEpoch_.load(std::memory_order_acquire); }
    void setExternalCapsLockHint(std::optional<bool> lockOn);
    void notifyFocusChanged(const std::string& appClass, bool isPasswordDialog);
    void setGameChatActive(bool chatActive, bool signalControlled = false);
    bool isGameChatActive() const;

    // Immediately ungrab all physical keyboards (Safe / Panic mode)
    void enterSafeMode();
    void exitSafeMode();
    bool isInSafeMode() const { return safeMode_.load(); }

    // Callbacks when user triggers hotkey, GameMode chat activity, or keyboard hotplug from physical keyboard
    using GameChatKeyCallback = std::function<void(bool isExitKey)>;
    using KeyboardHotplugCallback = std::function<void()>;
    void setOnLanguageToggled(LanguageToggleCallback cb) { onLangToggled_ = std::move(cb); }
    void setOnPanicTriggered(PanicModeCallback cb) { onPanic_ = std::move(cb); }
    void setOnGameChatKey(GameChatKeyCallback cb) { onGameChatKey_ = std::move(cb); }
    void setOnKeyboardHotplug(KeyboardHotplugCallback cb) { onKeyboardHotplug_ = std::move(cb); }

    // Runtime XKB fallback override (only engaged when XkbKeymap overlay is not active in live display)
    void setXkbFallbackMethod(int outputMethodOrMinusOne) {
        xkbFallbackMethod_.store(outputMethodOrMinusOne, std::memory_order_release);
    }
    int xkbFallbackMethod() const { return xkbFallbackMethod_.load(std::memory_order_acquire); }

    // Diagnostics: list currently monitored devices
    std::vector<GrabbedDeviceInfo> getMonitoredDevices() const;

    // Map evdev keycode + shift + capslock to printable ASCII char32_t (0 if non-printable)
    static char32_t keycodeToPrintable(uint16_t code, bool shift, bool capsLock);
    static bool isNavigationOrResetKey(uint16_t code);
    static bool isModifierKey(uint16_t code);

private:
    void workerLoop(std::vector<std::string> explicitPaths);
    void scanInputDirectory();
    bool tryAddDevice(const std::string& devPath, bool forceKeyboard = false);
    void removeDeviceByPath(const std::string& devPath);
    void removeDeviceByFd(int fd);
    void closeAllDevices();

    static bool waitAllKeysReleased(int fd, int maxWaitMs = 1500);
    static bool queryCapsLockLed(int fd);

    void handleKeyboardEvent(int sourceFd, const input_event& ev);
    void handleMouseEvent(const input_event& ev);
    void recomputeModifiersFromDevices();
    bool effectiveCompositorCapsLock() const;

    bamboo::SessionProcessor& processor_;
    UinputEmitter& emitter_;

    mutable std::mutex mutex_;
    Config config_;
    std::string activeAppClass_;
    bool activeIsPasswordDialog_ = false;
    std::atomic<bool> gameChatActive_{false};
    std::atomic<bool> gameSignalControlled_{false};
    std::atomic<bool> pendingProcessorReset_{false};

    std::atomic<bool> running_{false};
    std::atomic<bool> safeMode_{false};
    int wakeupEventFd_ = -1;
    int inotifyFd_ = -1;
    int inotifyWatch_ = -1;
    std::thread workerThread_;

    std::map<int, GrabbedDeviceInfo> devicesByFd_;

    // Modifier & key state tracking
    bool leftShift_ = false;
    bool rightShift_ = false;
    bool leftCtrl_ = false;
    bool rightCtrl_ = false;
    bool leftAlt_ = false;
    bool rightAlt_ = false;
    bool leftMeta_ = false;
    bool rightMeta_ = false;
    bool capsLockOn_ = false;
    bool hasExternalCapsLockHint_ = false;
    bool externalCapsLockOn_ = false;
    uint8_t armedModMask_ = 0;
    std::atomic<uint64_t> langToggleEpoch_{0};

    uint8_t currentModifierMask() const;
    uint8_t activeUinputModifierMask() const;
    void releaseHeldModifiersOnUinput();
    void resetInputTrackingState();
    bool triggerModifierOnlyHotkey(uint8_t modMask);
    bool triggerKeyHotkey(uint16_t code, uint8_t currentMods);

    std::bitset<KEY_MAX + 1> forwardedKeys_;
    std::bitset<KEY_MAX + 1> swallowedKeys_;
    std::bitset<KEY_MAX + 1> suppressedModReleases_;
    std::chrono::steady_clock::time_point lastKeystrokeTime_;

    LanguageToggleCallback onLangToggled_;
    PanicModeCallback onPanic_;
    GameChatKeyCallback onGameChatKey_;
    KeyboardHotplugCallback onKeyboardHotplug_;
    std::atomic<int> xkbFallbackMethod_{-1};
};

} // namespace vnsnowkey
