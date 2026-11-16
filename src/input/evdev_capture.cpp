/*
 * VNSnowKey - Vietnamese Input Method for Arch Linux + KDE Plasma (Wayland)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "evdev_capture.hpp"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace vnsnowkey {

namespace {

bool testBit(const unsigned long* bits, int bit) {
    return (bits[bit / (sizeof(unsigned long) * 8)] >> (bit % (sizeof(unsigned long) * 8))) & 1UL;
}

constexpr size_t kBitsPerLong = sizeof(unsigned long) * 8;
constexpr size_t kKeyLongs = (KEY_MAX + kBitsPerLong) / kBitsPerLong;
constexpr size_t kEvLongs  = (EV_MAX + kBitsPerLong) / kBitsPerLong;
constexpr size_t kLedLongs = (LED_MAX + kBitsPerLong) / kBitsPerLong;

} // namespace

EvdevCapture::EvdevCapture(bamboo::SessionProcessor& processor, UinputEmitter& emitter)
    : processor_(processor),
      emitter_(emitter),
      config_(Config::createDefault()),
      lastKeystrokeTime_(std::chrono::steady_clock::now()) {}

EvdevCapture::~EvdevCapture() {
    stop();
}

bool EvdevCapture::isModifierKey(uint16_t code) {
    switch (code) {
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT:
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL:
        case KEY_LEFTALT:
        case KEY_RIGHTALT:
        case KEY_LEFTMETA:
        case KEY_RIGHTMETA:
        case KEY_CAPSLOCK:
            return true;
        default:
            return false;
    }
}

bool EvdevCapture::isNavigationOrResetKey(uint16_t code) {
    switch (code) {
        case KEY_UP:
        case KEY_DOWN:
        case KEY_LEFT:
        case KEY_RIGHT:
        case KEY_HOME:
        case KEY_END:
        case KEY_PAGEUP:
        case KEY_PAGEDOWN:
        case KEY_INSERT:
        case KEY_DELETE:
        case KEY_TAB:
        case KEY_ENTER:
        case KEY_KPENTER:
        case KEY_ESC:
            return true;
        default:
            return false;
    }
}

char32_t EvdevCapture::keycodeToPrintable(uint16_t code, bool shift, bool capsLock) {
    bool upperAlpha = shift ^ capsLock;
    switch (code) {
        case KEY_A: return upperAlpha ? U'A' : U'a';
        case KEY_B: return upperAlpha ? U'B' : U'b';
        case KEY_C: return upperAlpha ? U'C' : U'c';
        case KEY_D: return upperAlpha ? U'D' : U'd';
        case KEY_E: return upperAlpha ? U'E' : U'e';
        case KEY_F: return upperAlpha ? U'F' : U'f';
        case KEY_G: return upperAlpha ? U'G' : U'g';
        case KEY_H: return upperAlpha ? U'H' : U'h';
        case KEY_I: return upperAlpha ? U'I' : U'i';
        case KEY_J: return upperAlpha ? U'J' : U'j';
        case KEY_K: return upperAlpha ? U'K' : U'k';
        case KEY_L: return upperAlpha ? U'L' : U'l';
        case KEY_M: return upperAlpha ? U'M' : U'm';
        case KEY_N: return upperAlpha ? U'N' : U'n';
        case KEY_O: return upperAlpha ? U'O' : U'o';
        case KEY_P: return upperAlpha ? U'P' : U'p';
        case KEY_Q: return upperAlpha ? U'Q' : U'q';
        case KEY_R: return upperAlpha ? U'R' : U'r';
        case KEY_S: return upperAlpha ? U'S' : U's';
        case KEY_T: return upperAlpha ? U'T' : U't';
        case KEY_U: return upperAlpha ? U'U' : U'u';
        case KEY_V: return upperAlpha ? U'V' : U'v';
        case KEY_W: return upperAlpha ? U'W' : U'w';
        case KEY_X: return upperAlpha ? U'X' : U'x';
        case KEY_Y: return upperAlpha ? U'Y' : U'y';
        case KEY_Z: return upperAlpha ? U'Z' : U'z';

        case KEY_1: return shift ? U'!' : U'1';
        case KEY_2: return shift ? U'@' : U'2';
        case KEY_3: return shift ? U'#' : U'3';
        case KEY_4: return shift ? U'$' : U'4';
        case KEY_5: return shift ? U'%' : U'5';
        case KEY_6: return shift ? U'^' : U'6';
        case KEY_7: return shift ? U'&' : U'7';
        case KEY_8: return shift ? U'*' : U'8';
        case KEY_9: return shift ? U'(' : U'9';
        case KEY_0: return shift ? U')' : U'0';

        case KEY_GRAVE:      return shift ? U'~' : U'`';
        case KEY_MINUS:      return shift ? U'_' : U'-';
        case KEY_EQUAL:      return shift ? U'+' : U'=';
        case KEY_LEFTBRACE:  return shift ? U'{' : U'[';
        case KEY_RIGHTBRACE: return shift ? U'}' : U']';
        case KEY_BACKSLASH:  return shift ? U'|' : U'\\';
        case KEY_SEMICOLON:  return shift ? U':' : U';';
        case KEY_APOSTROPHE: return shift ? U'"' : U'\'';
        case KEY_COMMA:      return shift ? U'<' : U',';
        case KEY_DOT:        return shift ? U'>' : U'.';
        case KEY_SLASH:      return shift ? U'?' : U'/';
        case KEY_SPACE:      return U' ';
        default:             return 0;
    }
}

bool EvdevCapture::waitAllKeysReleased(int fd, int maxWaitMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(maxWaitMs);
    while (std::chrono::steady_clock::now() < deadline) {
        unsigned long keyBits[kKeyLongs]{};
        if (::ioctl(fd, EVIOCGKEY(sizeof(keyBits)), keyBits) < 0) {
            return true;
        }
        bool anyPressed = false;
        for (size_t i = 0; i < kKeyLongs; ++i) {
            if (keyBits[i] != 0) {
                anyPressed = true;
                break;
            }
        }
        if (!anyPressed) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool EvdevCapture::queryCapsLockLed(int fd) {
    unsigned long ledBits[kLedLongs]{};
    if (::ioctl(fd, EVIOCGLED(sizeof(ledBits)), ledBits) >= 0) {
        return testBit(ledBits, LED_CAPSL);
    }
    return false;
}

bool EvdevCapture::tryAddDevice(const std::string& devPath, bool forceKeyboard) {
    // Check if already open
    for (const auto& [fd, info] : devicesByFd_) {
        if (info.path == devPath) {
            return true;
        }
    }

    int fd = -1;
    for (int attempt = 0; attempt < (forceKeyboard ? 30 : 3); ++attempt) {
        fd = ::open(devPath.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0 || errno != EACCES) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    if (fd < 0) {
        return false;
    }

    input_id id{};
    ::ioctl(fd, EVIOCGID, &id);
    char nameBuf[256] = "Unknown Input Device";
    ::ioctl(fd, EVIOCGNAME(sizeof(nameBuf)), nameBuf);
    std::string devName(nameBuf);

    // Never grab our own VNSnowKey Virtual Keyboard!
    if (!forceKeyboard &&
        ((id.vendor == UinputEmitter::kVNSnowKeyVendorId && id.product == UinputEmitter::kVNSnowKeyProductId) ||
         devName == UinputEmitter::kVirtualDeviceName)) {
        ::close(fd);
        return false;
    }

    unsigned long evBits[kEvLongs]{};
    unsigned long keyBits[kKeyLongs]{};
    if (::ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits) < 0 || !testBit(evBits, EV_KEY)) {
        ::close(fd);
        return false;
    }
    if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0) {
        ::close(fd);
        return false;
    }

    bool isKbd = (testBit(keyBits, KEY_A) && testBit(keyBits, KEY_Z) &&
                  testBit(keyBits, KEY_ENTER) && testBit(keyBits, KEY_SPACE));
    bool isMouse = testBit(keyBits, BTN_LEFT) || testBit(keyBits, BTN_RIGHT);

    if (!isKbd && !isMouse) {
        ::close(fd);
        return false;
    }

    GrabbedDeviceInfo info;
    info.fd = fd;
    info.path = devPath;
    info.name = devName;
    info.isKeyboard = isKbd;
    info.isMouse = isMouse;

    if (isKbd && !safeMode_.load()) {
        waitAllKeysReleased(fd, 1500);
        if (::ioctl(fd, EVIOCGRAB, 1) == 0) {
            info.isGrabbed = true;
            if (queryCapsLockLed(fd)) {
                capsLockOn_ = true;
            }
        }
    }

    devicesByFd_[fd] = std::move(info);
    return true;
}

void EvdevCapture::removeDeviceByPath(const std::string& devPath) {
    for (auto it = devicesByFd_.begin(); it != devicesByFd_.end(); ++it) {
        if (it->second.path == devPath) {
            if (it->second.isGrabbed) {
                ::ioctl(it->first, EVIOCGRAB, 0);
            }
            ::close(it->first);
            devicesByFd_.erase(it);
            return;
        }
    }
}

void EvdevCapture::removeDeviceByFd(int fd) {
    auto it = devicesByFd_.find(fd);
    if (it != devicesByFd_.end()) {
        if (it->second.isGrabbed) {
            ::ioctl(it->first, EVIOCGRAB, 0);
        }
        ::close(it->first);
        devicesByFd_.erase(it);
    }
    bool hasGrabbedKbd = false;
    for (const auto& [remFd, info] : devicesByFd_) {
        (void)remFd;
        if (info.isKeyboard && info.isGrabbed) {
            hasGrabbedKbd = true;
            break;
        }
    }
    if (!hasGrabbedKbd) {
        resetInputTrackingState();
        emitter_.releaseAllKeys();
    }
}

void EvdevCapture::closeAllDevices() {
    for (auto& [fd, info] : devicesByFd_) {
        if (info.isGrabbed) {
            ::ioctl(fd, EVIOCGRAB, 0);
            info.isGrabbed = false;
        }
        ::close(fd);
    }
    devicesByFd_.clear();
    resetInputTrackingState();
}

void EvdevCapture::scanInputDirectory() {
    try {
        for (const auto& entry : std::filesystem::directory_iterator("/dev/input")) {
            std::string fname = entry.path().filename().string();
            if (fname.rfind("event", 0) == 0) {
                tryAddDevice(entry.path().string(), false);
            }
        }
    } catch (...) {}
}

bool EvdevCapture::start(const std::vector<std::string>& explicitKeyboardPaths) {
    if (running_.load()) return true;

    wakeupEventFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeupEventFd_ < 0) {
        return false;
    }

    if (explicitKeyboardPaths.empty()) {
        inotifyFd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (inotifyFd_ >= 0) {
            inotifyWatch_ = ::inotify_add_watch(inotifyFd_, "/dev/input", IN_CREATE | IN_ATTRIB | IN_DELETE);
        }
    }

    running_.store(true);
    workerThread_ = std::thread(&EvdevCapture::workerLoop, this, explicitKeyboardPaths);
    return true;
}

void EvdevCapture::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (wakeupEventFd_ >= 0) {
        uint64_t one = 1;
        ssize_t unused = ::write(wakeupEventFd_, &one, sizeof(one));
        (void)unused;
    }
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    closeAllDevices();
    emitter_.releaseAllKeys();
    if (inotifyWatch_ >= 0 && inotifyFd_ >= 0) {
        ::inotify_rm_watch(inotifyFd_, inotifyWatch_);
        inotifyWatch_ = -1;
    }
    if (inotifyFd_ >= 0) {
        ::close(inotifyFd_);
        inotifyFd_ = -1;
    }
    if (wakeupEventFd_ >= 0) {
        ::close(wakeupEventFd_);
        wakeupEventFd_ = -1;
    }
}

void EvdevCapture::updateConfig(const Config& cfg) {
    bool enterSafe = cfg.safeMode;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = cfg;
        processor_.setOptions(cfg.toSessionOptions());
        processor_.macroTable().setMacros(cfg.macros);
    }
    if (enterSafe && !safeMode_.load()) {
        enterSafeMode();
    } else if (!enterSafe && safeMode_.load()) {
        exitSafeMode();
    }
}

void EvdevCapture::notifyFocusChanged(const std::string& appClass, bool isPasswordDialog) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (appClass != activeAppClass_) {
        if (config_.resetOnFocusChange) {
            processor_.reset();
        }
        gameChatActive_.store(false, std::memory_order_release);
        gameSignalControlled_.store(false, std::memory_order_release);
        pendingProcessorReset_.store(false, std::memory_order_relaxed);
    }
    activeAppClass_ = appClass;
    activeIsPasswordDialog_ = isPasswordDialog;
}

void EvdevCapture::setGameChatActive(bool chatActive, bool signalControlled) {
    gameSignalControlled_.store(signalControlled, std::memory_order_release);
    bool prev = gameChatActive_.exchange(chatActive, std::memory_order_acq_rel);
    if (prev != chatActive) {
        pendingProcessorReset_.store(true, std::memory_order_release);
    }
}

bool EvdevCapture::isGameChatActive() const {
    return gameChatActive_.load(std::memory_order_acquire);
}

void EvdevCapture::enterSafeMode() {
    safeMode_.store(true);
    std::lock_guard<std::mutex> lock(mutex_);
    processor_.reset();
    for (auto& [fd, info] : devicesByFd_) {
        if (info.isGrabbed) {
            ::ioctl(fd, EVIOCGRAB, 0);
            info.isGrabbed = false;
        }
    }
    resetInputTrackingState();
    emitter_.releaseAllKeys();
}

void EvdevCapture::exitSafeMode() {
    safeMode_.store(false);
    std::lock_guard<std::mutex> lock(mutex_);
    processor_.reset();
    for (auto& [fd, info] : devicesByFd_) {
        if (info.isKeyboard && !info.isGrabbed) {
            waitAllKeysReleased(fd, 500);
            if (::ioctl(fd, EVIOCGRAB, 1) == 0) {
                info.isGrabbed = true;
            }
        }
    }
}

std::vector<GrabbedDeviceInfo> EvdevCapture::getMonitoredDevices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<GrabbedDeviceInfo> list;
    for (const auto& [fd, info] : devicesByFd_) {
        list.push_back(info);
    }
    return list;
}

void EvdevCapture::workerLoop(std::vector<std::string> explicitPaths) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!explicitPaths.empty()) {
            for (const auto& p : explicitPaths) {
                tryAddDevice(p, true);
            }
        } else {
            scanInputDirectory();
        }
    }

    std::vector<pollfd> pfds;
    while (running_.load()) {
        pfds.clear();
        pfds.push_back(pollfd{wakeupEventFd_, POLLIN, 0});
        if (inotifyFd_ >= 0) {
            pfds.push_back(pollfd{inotifyFd_, POLLIN, 0});
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [fd, info] : devicesByFd_) {
                pfds.push_back(pollfd{fd, POLLIN, 0});
            }
        }

        int rc = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), 250);
        if (rc <= 0) {
            continue;
        }

        if (pfds[0].revents & POLLIN) {
            uint64_t val = 0;
            ssize_t unused = ::read(wakeupEventFd_, &val, sizeof(val));
            (void)unused;
            if (!running_.load()) break;
        }

        size_t devStartIdx = 1;
        if (inotifyFd_ >= 0) {
            devStartIdx = 2;
            if (pfds[1].revents & POLLIN) {
                alignas(inotify_event) char ibuf[4096];
                ssize_t len = ::read(inotifyFd_, ibuf, sizeof(ibuf));
                if (len > 0) {
                    // Brief pause for udev to apply permissions on newly created /dev/input/eventX
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                    std::lock_guard<std::mutex> lock(mutex_);
                    size_t offset = 0;
                    while (offset < static_cast<size_t>(len)) {
                        const auto* iev = reinterpret_cast<const inotify_event*>(ibuf + offset);
                        if (iev->len > 0) {
                            std::string fname(iev->name);
                            if (fname.rfind("event", 0) == 0) {
                                std::string fullPath = "/dev/input/" + fname;
                                if (iev->mask & IN_DELETE) {
                                    removeDeviceByPath(fullPath);
                                } else if (iev->mask & (IN_CREATE | IN_ATTRIB)) {
                                    tryAddDevice(fullPath, false);
                                }
                            }
                        }
                        offset += sizeof(inotify_event) + iev->len;
                    }
                }
            }
        }

        // Process input events from monitored devices
        for (size_t i = devStartIdx; i < pfds.size(); ++i) {
            int fd = pfds[i].fd;
            if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                std::lock_guard<std::mutex> lock(mutex_);
                removeDeviceByFd(fd);
                continue;
            }
            if (!(pfds[i].revents & POLLIN)) {
                continue;
            }

            input_event evBatch[64];
            while (true) {
                ssize_t rd = ::read(fd, evBatch, sizeof(evBatch));
                if (rd < 0) {
                    if (errno == EINTR) continue;
                    if (errno == ENODEV) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        removeDeviceByFd(fd);
                    }
                    break;
                }
                if (rd == 0) break;

                size_t count = static_cast<size_t>(rd) / sizeof(input_event);
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = devicesByFd_.find(fd);
                if (it == devicesByFd_.end()) break;
                bool isKbd = it->second.isKeyboard;
                bool isGrabbed = it->second.isGrabbed;
                bool isMouse = it->second.isMouse;

                for (size_t k = 0; k < count; ++k) {
                    const input_event& ev = evBatch[k];
                    if (isMouse && ev.type == EV_KEY &&
                        (ev.code == BTN_LEFT || ev.code == BTN_RIGHT || ev.code == BTN_MIDDLE)) {
                        handleMouseEvent(ev);
                    } else if (isKbd && isGrabbed) {
                        handleKeyboardEvent(ev);
                    } else if (isMouse) {
                        handleMouseEvent(ev);
                    }
                }
            }
        }
    }
}

void EvdevCapture::handleMouseEvent(const input_event& ev) {
    if (ev.type == EV_KEY && ev.value == 1) {
        if (ev.code == BTN_LEFT || ev.code == BTN_RIGHT || ev.code == BTN_MIDDLE) {
            armedModMask_ = 0;
            if (config_.resetOnMouseClick || ev.code == BTN_RIGHT || ev.code == BTN_MIDDLE) {
                processor_.reset();
            } else {
                processor_.onMouseClickWithoutReset();
            }
        }
    }
}

uint8_t EvdevCapture::currentModifierMask() const {
    uint8_t mask = 0;
    if (leftCtrl_ || rightCtrl_) mask |= kModCtrl;
    if (leftAlt_ || rightAlt_) mask |= kModAlt;
    if (leftShift_ || rightShift_) mask |= kModShift;
    if (leftMeta_ || rightMeta_) mask |= kModSuper;
    return mask;
}

uint8_t EvdevCapture::activeUinputModifierMask() const {
    uint8_t mask = 0;
    if ((leftCtrl_ && !suppressedModReleases_.test(KEY_LEFTCTRL)) ||
        (rightCtrl_ && !suppressedModReleases_.test(KEY_RIGHTCTRL))) {
        mask |= kModCtrl;
    }
    if ((leftAlt_ && !suppressedModReleases_.test(KEY_LEFTALT)) ||
        (rightAlt_ && !suppressedModReleases_.test(KEY_RIGHTALT))) {
        mask |= kModAlt;
    }
    if ((leftShift_ && !suppressedModReleases_.test(KEY_LEFTSHIFT)) ||
        (rightShift_ && !suppressedModReleases_.test(KEY_RIGHTSHIFT))) {
        mask |= kModShift;
    }
    if ((leftMeta_ && !suppressedModReleases_.test(KEY_LEFTMETA)) ||
        (rightMeta_ && !suppressedModReleases_.test(KEY_RIGHTMETA))) {
        mask |= kModSuper;
    }
    return mask;
}

void EvdevCapture::releaseHeldModifiersOnUinput() {
    const struct {
        bool& state;
        uint16_t code;
    } mods[] = {
        {leftShift_,  KEY_LEFTSHIFT},
        {rightShift_, KEY_RIGHTSHIFT},
        {leftCtrl_,   KEY_LEFTCTRL},
        {rightCtrl_,  KEY_RIGHTCTRL},
        {leftAlt_,    KEY_LEFTALT},
        {rightAlt_,   KEY_RIGHTALT},
        {leftMeta_,   KEY_LEFTMETA},
        {rightMeta_,  KEY_RIGHTMETA},
    };
    for (const auto& m : mods) {
        if (m.state && !suppressedModReleases_.test(m.code)) {
            emitter_.forwardKeyEvent(m.code, 0);
            suppressedModReleases_.set(m.code, true);
        }
    }
    armedModMask_ = 0;
}

void EvdevCapture::resetInputTrackingState() {
    leftShift_ = false;
    rightShift_ = false;
    leftCtrl_ = false;
    rightCtrl_ = false;
    leftAlt_ = false;
    rightAlt_ = false;
    leftMeta_ = false;
    rightMeta_ = false;
    armedModMask_ = 0;
    forwardedKeys_.reset();
    swallowedKeys_.reset();
    suppressedModReleases_.reset();
}

bool EvdevCapture::triggerModifierOnlyHotkey(uint8_t modMask) {
    if (modMask == 0) return false;

    auto matchModOnly = [modMask](const std::string& binding) -> bool {
        auto parsed = Config::parseHotkey(binding);
        return parsed.isValid() && parsed.keyCode == 0 && parsed.modifiers == modMask;
    };

    if (matchModOnly(config_.shortcuts.panicSafeMode)) {
        if (onPanic_) onPanic_();
        return true;
    }

    if (matchModOnly(config_.shortcuts.togglePrimary) ||
        matchModOnly(config_.shortcuts.toggleSecondary) ||
        matchModOnly(config_.shortcuts.toggleExtra)) {
        if (modMask & kModSuper) {
            emitter_.emitCancelSuperTap();
        }
        bool newMode = !config_.vietnameseEnabled;
        config_.vietnameseEnabled = newMode;
        processor_.setVietnameseEnabled(newMode);
        if (onLangToggled_) onLangToggled_(newMode);
        return true;
    }

    if (matchModOnly(config_.shortcuts.restoreWord) && !processor_.currentRawKeys().empty()) {
        auto diff = processor_.restoreCurrentWord();
        if (diff.wasTransformed) {
            emitter_.emitDiff(diff.backspaces, diff.textToEmit, diff.encodedToEmit,
                              config_.defaultOutputMethod, config_.defaultDelayUs, false,
                              config_.clipboardRestoreDelayMs);
            return true;
        }
    }

    return false;
}

bool EvdevCapture::triggerKeyHotkey(uint16_t code, uint8_t currentMods) {
    auto matchKey = [code, currentMods](const std::string& binding, bool allowSysRqForPause) -> bool {
        auto parsed = Config::parseHotkey(binding);
        if (!parsed.isValid() || parsed.keyCode == 0 || parsed.modifiers != currentMods) {
            return false;
        }
        if (parsed.keyCode == code) return true;
        if (allowSysRqForPause && parsed.keyCode == KEY_PAUSE && code == KEY_SYSRQ) return true;
        return false;
    };

    if (matchKey(config_.shortcuts.panicSafeMode, true)) {
        swallowedKeys_.set(code, true);
        releaseHeldModifiersOnUinput();
        if (onPanic_) onPanic_();
        return true;
    }

    auto toggleMatches = [&](const std::string& binding) -> bool {
        if (!matchKey(binding, false)) return false;
        auto parsed = Config::parseHotkey(binding);
        swallowedKeys_.set(code, true);
        if (parsed.modifiers & kModSuper) {
            emitter_.emitCancelSuperTap(); // Prevent KWin Kickoff launcher from opening on Super release
        }
        releaseHeldModifiersOnUinput();
        bool newMode = !config_.vietnameseEnabled;
        config_.vietnameseEnabled = newMode;
        processor_.setVietnameseEnabled(newMode);
        if (onLangToggled_) onLangToggled_(newMode);
        return true;
    };

    if (toggleMatches(config_.shortcuts.togglePrimary) ||
        toggleMatches(config_.shortcuts.toggleSecondary) ||
        toggleMatches(config_.shortcuts.toggleExtra)) {
        return true;
    }

    if (matchKey(config_.shortcuts.restoreWord, false) && !processor_.currentRawKeys().empty()) {
        auto diff = processor_.restoreCurrentWord();
        if (diff.wasTransformed) {
            swallowedKeys_.set(code, true);
            releaseHeldModifiersOnUinput();
            emitter_.emitDiff(diff.backspaces, diff.textToEmit, diff.encodedToEmit,
                              config_.defaultOutputMethod, config_.defaultDelayUs, false,
                              config_.clipboardRestoreDelayMs);
            return true;
        }
    }

    return false;
}

void EvdevCapture::handleKeyboardEvent(const input_event& ev) {
    if (ev.type != EV_KEY) {
        return;
    }

    const uint16_t code = ev.code;
    const int32_t value = ev.value; // 1 = press, 0 = release, 2 = autorepeat
    if (code > KEY_MAX) {
        return;
    }

    if (pendingProcessorReset_.exchange(false, std::memory_order_acq_rel)) {
        processor_.reset();
    }

    auto now = std::chrono::steady_clock::now();
    if (config_.bufferTimeoutMs > 0) {
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastKeystrokeTime_).count();
        if (elapsedMs > config_.bufferTimeoutMs) {
            processor_.reset();
        }
    }
    lastKeystrokeTime_ = now;

    // 1. Handle release (value == 0) of previously swallowed hotkey keys
    if (value == 0 && swallowedKeys_.test(code)) {
        swallowedKeys_.set(code, false);
        return;
    }

    // 2. Handle modifier keys
    if (isModifierKey(code)) {
        bool pressed = (value != 0);
        switch (code) {
            case KEY_LEFTSHIFT:  leftShift_  = pressed; break;
            case KEY_RIGHTSHIFT: rightShift_ = pressed; break;
            case KEY_LEFTCTRL:   leftCtrl_   = pressed; break;
            case KEY_RIGHTCTRL:  rightCtrl_  = pressed; break;
            case KEY_LEFTALT:    leftAlt_    = pressed; break;
            case KEY_RIGHTALT:   rightAlt_   = pressed; break;
            case KEY_LEFTMETA:   leftMeta_   = pressed; break;
            case KEY_RIGHTMETA:  rightMeta_  = pressed; break;
            case KEY_CAPSLOCK:
                if (value == 1) capsLockOn_ = !capsLockOn_;
                break;
        }

        if (value == 1) {
            suppressedModReleases_.set(code, false);
        } else if (suppressedModReleases_.test(code)) {
            if (value == 0) {
                suppressedModReleases_.set(code, false);
                armedModMask_ &= currentModifierMask();
            }
            return;
        }

        uint8_t mods = currentModifierMask();
        auto hasTwoMods = [](uint8_t m) {
            return (m != 0) && ((m & (m - 1)) != 0);
        };

        if (value == 1) {
            if (hasTwoMods(mods)) {
                armedModMask_ |= mods;
            }
        } else if (value == 0 && armedModMask_ != 0) {
            uint8_t candidate = armedModMask_;
            armedModMask_ = 0;
            if (triggerModifierOnlyHotkey(candidate)) {
                emitter_.forwardKeyEvent(code, value);
                releaseHeldModifiersOnUinput();
                return;
            }
        }

        emitter_.forwardKeyEvent(code, value);
        return;
    }

    // Any non-modifier key cancels modifier-only chord
    if (value == 1) {
        armedModMask_ = 0;
    }

    // 3. Check global hotkeys on key press (value == 1) using physical modifiers
    if (value == 1 && triggerKeyHotkey(code, currentModifierMask())) {
        return;
    }

    uint8_t activeUinputMods = activeUinputModifierMask();
    bool anyCtrl  = (activeUinputMods & kModCtrl) != 0;
    bool anyShift = (activeUinputMods & kModShift) != 0;
    bool anyAlt   = (activeUinputMods & kModAlt) != 0;
    bool anyMeta  = (activeUinputMods & kModSuper) != 0;

    // 4. Key release (value == 0) of normal keys
    if (value == 0) {
        if (forwardedKeys_.test(code)) {
            forwardedKeys_.set(code, false);
            emitter_.forwardKeyEvent(code, 0);
        }
        return;
    }

    // 5. If Ctrl, Alt, or Meta is held, reset buffer and forward shortcut key directly
    if (anyCtrl || anyAlt || anyMeta) {
        processor_.reset();
        forwardedKeys_.set(code, true);
        emitter_.forwardKeyEvent(code, value);
        return;
    }

    // 6. Determine per-app profile settings
    const AppProfile* appProf = config_.findProfileForApp(activeAppClass_);
    bool appExcluded = (appProf && appProf->mode == AppMode::Excluded) ||
                       (config_.bypassPasswordDialogs && activeIsPasswordDialog_);
    OutputMethod outMethod = (appProf && appProf->useCustomOutputMethod)
                                 ? appProf->outputMethod
                                 : config_.defaultOutputMethod;
    int delayUs = (appProf && appProf->delayUs > 0) ? appProf->delayUs : config_.defaultDelayUs;
    bool autocompleteFix = (appProf && appProf->autocompleteFix);

    const bool isGameMode = (appProf && appProf->mode == AppMode::GameMode);
    if (isGameMode && delayUs <= 0 &&
        (outMethod == OutputMethod::XkbKeymap || outMethod == OutputMethod::CtrlShiftU)) {
        // XWayland / GLFW / Java games (e.g. Minecraft) process X11 modifier transitions
        // asynchronously; pacing frames prevents dropped Backspaces or lost Level3/Level5 chords.
        delayUs = 2000;
    }
    const bool gameGameplayPassThrough = (isGameMode && !gameChatActive_.load(std::memory_order_acquire));

    if (appExcluded || !config_.vietnameseEnabled || safeMode_.load() || gameGameplayPassThrough) {
        if (!gameGameplayPassThrough) {
            processor_.reset();
        }
        if (gameGameplayPassThrough && !appExcluded && config_.vietnameseEnabled &&
            !safeMode_.load() && value == 1 && !anyCtrl && !anyAlt && !anyMeta && !anyShift &&
            (code == KEY_T || code == KEY_SLASH)) {
            if (!gameSignalControlled_.load(std::memory_order_acquire)) {
                gameChatActive_.store(true, std::memory_order_release);
                processor_.reset();
            } else if (onGameChatKey_) {
                onGameChatKey_(false, true);
            }
        }
        forwardedKeys_.set(code, true);
        emitter_.forwardKeyEvent(code, value);
        return;
    }

    // If a GameMode compositor cursor signal (e.g. Mutter/XWayland under QEMU/KVM) gets stuck visible
    // after closing a menu via mouse click, holding W/A/S/D into repeat (value == 2) signals 3D walking:
    // clear cursor-stuck chat mode cleanly without affecting real compositor text inputs (KWinTextInput).
    if (isGameMode && gameChatActive_.load(std::memory_order_acquire) && value == 2 &&
        !anyCtrl && !anyAlt && !anyMeta &&
        (code == KEY_W || code == KEY_A || code == KEY_S || code == KEY_D)) {
        if (!gameSignalControlled_.load(std::memory_order_acquire)) {
            gameChatActive_.store(false, std::memory_order_release);
        } else if (onGameChatKey_) {
            onGameChatKey_(true, false);
        }
        if (!gameChatActive_.load(std::memory_order_acquire)) {
            pendingProcessorReset_.store(false, std::memory_order_relaxed);
            processor_.reset();
            if (!forwardedKeys_.test(code)) {
                emitter_.forwardKeyEvent(code, 1);
                forwardedKeys_.set(code, true);
            }
            emitter_.forwardKeyEvent(code, 2);
            return;
        }
    }

    // 7. Handle Backspace
    if (code == KEY_BACKSPACE) {
        if (isGameMode && gameChatActive_.load(std::memory_order_acquire) && value == 1 && onGameChatKey_) {
            onGameChatKey_(false, false);
        }
        const bool hadBufferedWord = !processor_.currentRawKeys().empty();
        const bool hadHistory = processor_.hasUndoableHistory();
        auto diff = processor_.processBackspace();
        if (diff.passThrough) {
            if (autocompleteFix && hadBufferedWord) {
                emitter_.emitDiff(1, U"", "", outMethod, delayUs, true, config_.clipboardRestoreDelayMs);
            } else if (!hadBufferedWord && !hadHistory && !autocompleteFix && !anyShift) {
                // Buffer is empty: preserve native press/repeat/release lifecycle so compositor
                // key repeat & press duration behave identically to a physical keyboard.
                if (value == 2 && !forwardedKeys_.test(KEY_BACKSPACE)) {
                    // User began holding Backspace while inside a buffered word and crossed to empty buffer
                    emitter_.forwardKeyEvent(KEY_BACKSPACE, 1);
                    emitter_.forwardKeyEvent(KEY_BACKSPACE, 0);
                } else {
                    forwardedKeys_.set(KEY_BACKSPACE, true);
                    emitter_.forwardKeyEvent(KEY_BACKSPACE, value);
                }
            } else if (anyShift) {
                // Temporarily release Shift during fast uppercase rollover so Backspace isn't swallowed as Shift+Bksp
                emitter_.emitDiff(1, U"", "", outMethod, delayUs, autocompleteFix, config_.clipboardRestoreDelayMs);
            } else {
                emitter_.forwardKeyEvent(KEY_BACKSPACE, 1);
                emitter_.forwardKeyEvent(KEY_BACKSPACE, 0);
            }
        } else {
            emitter_.emitDiff(diff.backspaces, diff.textToEmit, diff.encodedToEmit,
                              outMethod, delayUs, autocompleteFix, config_.clipboardRestoreDelayMs);
        }
        return;
    }

    // 8. Handle Navigation / Reset keys (Enter, Tab, Arrows, Home, End, PageUp, PageDown, Esc, Delete)
    if (isNavigationOrResetKey(code)) {
        if (isGameMode && gameChatActive_.load(std::memory_order_acquire) && value == 1 &&
            (code == KEY_ENTER || code == KEY_KPENTER || code == KEY_ESC)) {
            if (!gameSignalControlled_.load(std::memory_order_acquire)) {
                gameChatActive_.store(false, std::memory_order_release);
                processor_.reset();
            } else if (onGameChatKey_) {
                onGameChatKey_(true, true);
            }
        } else if (!anyShift && !autocompleteFix &&
            (code == KEY_LEFT || code == KEY_RIGHT || code == KEY_DELETE)) {
            if (code == KEY_LEFT) {
                processor_.moveCursorLeft();
            } else if (code == KEY_RIGHT) {
                processor_.moveCursorRight();
            } else {
                processor_.processDelete();
            }
        } else if (config_.resetOnNavigationKeys) {
            processor_.reset();
        }
        forwardedKeys_.set(code, true);
        emitter_.forwardKeyEvent(code, value);
        return;
    }

    // 9. Handle Printable keys
    char32_t ch = keycodeToPrintable(code, anyShift, capsLockOn_);
    if (ch == 0) {
        forwardedKeys_.set(code, true);
        emitter_.forwardKeyEvent(code, value);
        return;
    }
    const bool isMovementKey = (code == KEY_W || code == KEY_A || code == KEY_S || code == KEY_D);
    if (isGameMode && gameChatActive_.load(std::memory_order_acquire) && value == 1 && !isMovementKey && onGameChatKey_) {
        onGameChatKey_(false, false);
    }

    auto diff = processor_.processPrintableKey(ch);
    if (diff.passThrough) {
        // Emit as an atomic tap (press + release) so N-key rollover never interleaves with future Backspaces
        emitter_.forwardKeyEvent(code, 1);
        emitter_.forwardKeyEvent(code, 0);
    } else {
        // If outputCharset is non-Unicode (e.g. VNI Windows / TCVN3) and contains non-XKB characters,
        // automatically use Clipboard fallback if outMethod is XkbKeymap and a char isn't in XkbOverlay
        emitter_.emitDiff(diff.backspaces, diff.textToEmit, diff.encodedToEmit,
                          outMethod, delayUs, autocompleteFix, config_.clipboardRestoreDelayMs);
    }
}

} // namespace vnsnowkey
