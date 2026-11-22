/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
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

struct ModBitMapping {
    uint16_t code;
    uint16_t bit;
};

constexpr ModBitMapping kDeviceModMap[] = {
    {KEY_LEFTSHIFT,  1u << 0},
    {KEY_RIGHTSHIFT, 1u << 1},
    {KEY_LEFTCTRL,   1u << 2},
    {KEY_RIGHTCTRL,  1u << 3},
    {KEY_LEFTALT,    1u << 4},
    {KEY_RIGHTALT,   1u << 5},
    {KEY_LEFTMETA,   1u << 6},
    {KEY_RIGHTMETA,  1u << 7},
};

uint16_t modifierBitForKeycode(uint16_t code) {
    for (const auto& m : kDeviceModMap) {
        if (m.code == code) return m.bit;
    }
    return 0;
}

bool engineConfigChanged(const Config& a, const Config& b) {
    return a.inputMethod != b.inputMethod ||
           a.outputCharset != b.outputCharset ||
           a.freeToneMarking != b.freeToneMarking ||
           a.modernToneStyle != b.modernToneStyle ||
           a.spellCheckEnabled != b.spellCheckEnabled ||
           a.autoRestoreEnglish != b.autoRestoreEnglish ||
           a.shorthandInitialConsonant != b.shorthandInitialConsonant ||
           a.shorthandFinalConsonant != b.shorthandFinalConsonant ||
           a.macroEnabled != b.macroEnabled ||
           a.macroAutoCase != b.macroAutoCase ||
           a.macros != b.macros;
}

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
    // Isolated test socketpair / pipe mode: attach directly without EVIOCGRAB or scanning
    if (devPath.rfind("fd:", 0) == 0) {
        int devFd = -1;
        try {
            devFd = std::stoi(devPath.substr(3));
        } catch (...) {
            return false;
        }
        if (devFd < 0) return false;
        GrabbedDeviceInfo info;
        info.fd = devFd;
        info.path = devPath;
        info.name = "VNSnowKey Isolated Test Device";
        info.isKeyboard = true;
        info.isMouse = true;
        info.isGrabbed = true;
        devicesByFd_[devFd] = std::move(info);
        return true;
    }

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
            removeDeviceByFd(it->first);
            return;
        }
    }
}

void EvdevCapture::removeDeviceByFd(int fd) {
    uint16_t removedMods = 0;
    auto it = devicesByFd_.find(fd);
    if (it != devicesByFd_.end()) {
        removedMods = it->second.heldModMask;
        if (it->second.isGrabbed) {
            ::ioctl(it->first, EVIOCGRAB, 0);
        }
        ::close(it->first);
        devicesByFd_.erase(it);
    }
    if (removedMods != 0) {
        recomputeModifiersFromDevices();
        uint16_t stillHeld = 0;
        for (const auto& [remFd, info] : devicesByFd_) {
            (void)remFd;
            stillHeld |= info.heldModMask;
        }
        uint16_t dropped = removedMods & static_cast<uint16_t>(~stillHeld);
        for (const auto& m : kDeviceModMap) {
            if (dropped & m.bit) {
                enqueueRawKey(m.code, 0);
                suppressedModReleases_.set(m.code, false);
            }
        }
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
        OutputTask relTask;
        relTask.type = OutputTask::Type::ReleaseAll;
        enqueueOutput(std::move(relTask));
    }
}

void EvdevCapture::closeAllDevices() {
    for (auto& [fd, info] : devicesByFd_) {
        if (info.isGrabbed && info.path.rfind("fd:", 0) != 0) {
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

    startOutputWorker();
    running_.store(true);
    workerThread_ = std::thread(&EvdevCapture::workerLoop, this, explicitKeyboardPaths);
    return true;
}

void EvdevCapture::stop() {
    if (!running_.exchange(false)) {
        stopOutputWorker();
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
    drainOutputQueue();
    stopOutputWorker();
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
        const bool engChanged = engineConfigChanged(config_, cfg);
        config_ = cfg;
        if (engChanged) {
            processor_.setOptions(cfg.toSessionOptions());
            processor_.macroTable().setMacros(cfg.macros);
        } else {
            processor_.setVietnameseEnabled(cfg.vietnameseEnabled && !cfg.safeMode);
        }
    }
    if (enterSafe && !safeMode_.load()) {
        enterSafeMode();
    } else if (!enterSafe && safeMode_.load()) {
        exitSafeMode();
    }
}

void EvdevCapture::setVietnameseEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.vietnameseEnabled = enabled;
    processor_.setVietnameseEnabled(enabled && !safeMode_.load());
}

bool EvdevCapture::setVietnameseEnabledFromFocus(bool enabled, uint64_t expectedEpoch) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (langToggleEpoch_.load(std::memory_order_acquire) != expectedEpoch) {
        return false;
    }
    config_.vietnameseEnabled = enabled;
    processor_.setVietnameseEnabled(enabled && !safeMode_.load());
    return true;
}

bool EvdevCapture::vietnameseEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.vietnameseEnabled;
}

void EvdevCapture::setExternalCapsLockHint(std::optional<bool> lockOn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lockOn.has_value()) {
        hasExternalCapsLockHint_ = true;
        externalCapsLockOn_ = *lockOn;
    } else {
        hasExternalCapsLockHint_ = false;
    }
}

void EvdevCapture::notifyFocusChanged(const std::string& appClass, bool isPasswordDialog) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!appClass.empty() && appClass != activeAppClass_) {
        if (config_.resetOnFocusChange) {
            processor_.reset();
        }
        gameChatActive_.store(false, std::memory_order_release);
        gameSignalControlled_.store(false, std::memory_order_release);
        pendingProcessorReset_.store(false, std::memory_order_relaxed);
    }
    if (!appClass.empty()) {
        activeAppClass_ = appClass;
    }
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
            if (info.path.rfind("fd:", 0) != 0) {
                ::ioctl(fd, EVIOCGRAB, 0);
            }
            info.isGrabbed = false;
        }
    }
    resetInputTrackingState();
    drainOutputQueue();
    emitter_.releaseAllKeys();
}

void EvdevCapture::exitSafeMode() {
    safeMode_.store(false);
    std::lock_guard<std::mutex> lock(mutex_);
    processor_.reset();
    bool anyCapsLed = false;
    for (auto& [fd, info] : devicesByFd_) {
        if (info.path.rfind("fd:", 0) == 0) {
            info.isGrabbed = true;
            continue;
        }
        if (info.isKeyboard && !info.isGrabbed) {
            waitAllKeysReleased(fd, 500);
            if (::ioctl(fd, EVIOCGRAB, 1) == 0) {
                info.isGrabbed = true;
            }
        }
        if (info.isKeyboard && info.isGrabbed && queryCapsLockLed(fd)) {
            anyCapsLed = true;
        }
    }
    capsLockOn_ = anyCapsLed;
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
                    bool addedAnyKeyboard = false;
                    {
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
                                        if (tryAddDevice(fullPath, false)) {
                                            for (const auto& [addedFd, info] : devicesByFd_) {
                                                (void)addedFd;
                                                if (info.path == fullPath && info.isKeyboard) {
                                                    addedAnyKeyboard = true;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            offset += sizeof(inotify_event) + iev->len;
                        }
                    }
                    if (addedAnyKeyboard && onKeyboardHotplug_) {
                        onKeyboardHotplug_();
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
                    if (isMouse &&
                        ((ev.type == EV_KEY && ev.code >= BTN_MOUSE && ev.code <= BTN_TASK) ||
                         (ev.type == EV_REL && (ev.code == REL_WHEEL || ev.code == REL_HWHEEL ||
                                                ev.code == REL_WHEEL_HI_RES || ev.code == REL_HWHEEL_HI_RES)))) {
                        handleMouseEvent(ev);
                    } else if (isKbd && isGrabbed) {
                        handleKeyboardEvent(fd, ev, (k + 1) < count);
                    } else if (isMouse) {
                        handleMouseEvent(ev);
                    }
                }
            }
        }
    }
}

void EvdevCapture::handleMouseEvent(const input_event& ev) {
    if (ev.type == EV_REL &&
        (ev.code == REL_WHEEL || ev.code == REL_HWHEEL ||
         ev.code == REL_WHEEL_HI_RES || ev.code == REL_HWHEEL_HI_RES)) {
        // Scrolling while holding Ctrl+Alt (e.g. zoom) must cancel pending modifier-only hotkey chords.
        armedModMask_ = 0;
        return;
    }
    if (ev.type == EV_KEY && ev.value == 1) {
        if (ev.code >= BTN_MOUSE && ev.code <= BTN_TASK) {
            armedModMask_ = 0;
        }
        if (ev.code == BTN_LEFT || ev.code == BTN_RIGHT || ev.code == BTN_MIDDLE) {
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
            enqueueRawKey(m.code, 0);
            suppressedModReleases_.set(m.code, true);
        }
    }
    armedModMask_ = 0;
}

void EvdevCapture::startOutputWorker() {
    std::lock_guard<std::mutex> lk(outputMutex_);
    if (outputRunning_) {
        return;
    }
    outputRunning_ = true;
    outputThread_ = std::thread(&EvdevCapture::outputLoop, this);
}

void EvdevCapture::stopOutputWorker() {
    {
        std::lock_guard<std::mutex> lk(outputMutex_);
        outputRunning_ = false;
        outputCv_.notify_all();
        outputDoneCv_.notify_all();
    }
    if (outputThread_.joinable()) {
        outputThread_.join();
    }
}

void EvdevCapture::outputLoop() {
    while (true) {
        OutputTask task;
        {
            std::unique_lock<std::mutex> lk(outputMutex_);
            outputCv_.wait(lk, [this]() {
                return !outputRunning_ || !outputQueue_.empty();
            });
            if (!outputRunning_ && outputQueue_.empty()) {
                break;
            }
            task = std::move(outputQueue_.front());
            outputQueue_.pop_front();
        }

        switch (task.type) {
            case OutputTask::Type::RawKey:
                emitter_.forwardKeyEvent(task.code, task.value);
                break;
            case OutputTask::Type::PassTap:
                emitter_.forwardTapEvent(task.code, task.tapHoldUs);
                break;
            case OutputTask::Type::EmitDiff:
                emitter_.emitDiff(
                    task.backspaces,
                    task.textToEmit,
                    task.encodedToEmit,
                    task.method,
                    task.delayUs,
                    task.autocompleteFix,
                    task.clipboardRestoreDelayMs,
                    task.compositorCapsLock);
                break;
            case OutputTask::Type::CancelSuperTap:
                emitter_.emitCancelSuperTap();
                break;
            case OutputTask::Type::ReleaseAll:
                emitter_.releaseAllKeys();
                break;
        }

        {
            std::lock_guard<std::mutex> lk(outputMutex_);
            ++completedOutputSeq_;
            if (outputQueue_.empty()) {
                outputDoneCv_.notify_all();
            }
        }
    }
}

void EvdevCapture::enqueueOutput(OutputTask task) {
    bool runningNow = false;
    {
        std::lock_guard<std::mutex> lk(outputMutex_);
        runningNow = outputRunning_;
        if (runningNow) {
            ++nextOutputSeq_;
            outputQueue_.push_back(std::move(task));
            outputCv_.notify_one();
            return;
        }
    }
    // Synchronous fallback if called before start() or in direct unit tests without worker threads
    switch (task.type) {
        case OutputTask::Type::RawKey:
            emitter_.forwardKeyEvent(task.code, task.value);
            break;
        case OutputTask::Type::PassTap:
            emitter_.forwardTapEvent(task.code, task.tapHoldUs);
            break;
        case OutputTask::Type::EmitDiff:
            emitter_.emitDiff(
                task.backspaces,
                task.textToEmit,
                task.encodedToEmit,
                task.method,
                task.delayUs,
                task.autocompleteFix,
                task.clipboardRestoreDelayMs,
                task.compositorCapsLock);
            break;
        case OutputTask::Type::CancelSuperTap:
            emitter_.emitCancelSuperTap();
            break;
        case OutputTask::Type::ReleaseAll:
            emitter_.releaseAllKeys();
            break;
    }
}

void EvdevCapture::enqueueRawKey(uint16_t code, int32_t value) {
    OutputTask task;
    task.type = OutputTask::Type::RawKey;
    task.code = code;
    task.value = value;
    enqueueOutput(std::move(task));
}

void EvdevCapture::enqueuePassTap(uint16_t code, int tapHoldUs) {
    OutputTask task;
    task.type = OutputTask::Type::PassTap;
    task.code = code;
    task.tapHoldUs = tapHoldUs;
    enqueueOutput(std::move(task));
}

void EvdevCapture::enqueueEmitDiff(
    int backspaces,
    std::u32string textToEmit,
    std::string encodedToEmit,
    OutputMethod method,
    int delayUs,
    bool autocompleteFix,
    int clipboardRestoreDelayMs,
    bool compositorCapsLock) {
    OutputTask task;
    task.type = OutputTask::Type::EmitDiff;
    task.backspaces = backspaces;
    task.textToEmit = std::move(textToEmit);
    task.encodedToEmit = std::move(encodedToEmit);
    task.method = method;
    task.delayUs = delayUs;
    task.autocompleteFix = autocompleteFix;
    task.clipboardRestoreDelayMs = clipboardRestoreDelayMs;
    task.compositorCapsLock = compositorCapsLock;
    enqueueOutput(std::move(task));
}

void EvdevCapture::enqueueCancelSuperTap() {
    OutputTask task;
    task.type = OutputTask::Type::CancelSuperTap;
    enqueueOutput(std::move(task));
}

void EvdevCapture::drainOutputQueue() {
    std::unique_lock<std::mutex> lk(outputMutex_);
    if (!outputRunning_) {
        return;
    }
    const uint64_t targetSeq = nextOutputSeq_;
    outputDoneCv_.wait(lk, [this, targetSeq]() {
        return !outputRunning_ || completedOutputSeq_ >= targetSeq;
    });
}

void EvdevCapture::flushPendingOutput() {
    drainOutputQueue();
}

bool EvdevCapture::capsLockOn() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capsLockOn_;
}

void EvdevCapture::resyncDeviceModifiersLocked(int fd, bool emitReleasedToUinput) {
    auto devIt = devicesByFd_.find(fd);
    if (devIt == devicesByFd_.end()) {
        return;
    }
    unsigned long keyBits[kKeyLongs]{};
    if (::ioctl(fd, EVIOCGKEY(sizeof(keyBits)), keyBits) < 0) {
        return;
    }
    const uint16_t prevMods = devIt->second.heldModMask;
    uint16_t liveMods = 0;
    for (const auto& m : kDeviceModMap) {
        if (testBit(keyBits, m.code)) {
            liveMods |= m.bit;
        }
    }
    devIt->second.heldModMask = liveMods;
    if (prevMods != liveMods) {
        recomputeModifiersFromDevices();
        if (emitReleasedToUinput) {
            uint16_t stillHeld = 0;
            for (const auto& [remFd, info] : devicesByFd_) {
                (void)remFd;
                stillHeld |= info.heldModMask;
            }
            const uint16_t dropped = prevMods & static_cast<uint16_t>(~stillHeld);
            for (const auto& m : kDeviceModMap) {
                if (dropped & m.bit) {
                    enqueueRawKey(m.code, 0);
                    suppressedModReleases_.set(m.code, false);
                    stuckModRecoveries_.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    }
}

void EvdevCapture::resyncStuckModifiersBeforeKeyLocked() {
    if (currentModifierMask() == 0) {
        return;
    }
    std::vector<int> modFds;
    for (const auto& [devFd, info] : devicesByFd_) {
        if (info.isKeyboard && info.heldModMask != 0) {
            modFds.push_back(devFd);
        }
    }
    bool anyHealed = false;
    for (int modFd : modFds) {
        const uint8_t before = currentModifierMask();
        resyncDeviceModifiersLocked(modFd, true);
        if (currentModifierMask() != before) {
            anyHealed = true;
        }
    }
    if (anyHealed) {
        armedModMask_ &= currentModifierMask();
    }
}

void EvdevCapture::resyncAllModifiersFromKernel() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> fds;
    fds.reserve(devicesByFd_.size());
    bool anyCapsLed = false;
    bool hasAnyKbd = false;
    for (const auto& [devFd, info] : devicesByFd_) {
        if (info.isKeyboard && info.isGrabbed) {
            fds.push_back(devFd);
            hasAnyKbd = true;
            if (queryCapsLockLed(devFd)) {
                anyCapsLed = true;
            }
        }
    }
    for (int devFd : fds) {
        resyncDeviceModifiersLocked(devFd, true);
    }
    if (hasAnyKbd) {
        capsLockOn_ = anyCapsLed;
    }
    armedModMask_ &= currentModifierMask();
}

void EvdevCapture::recomputeModifiersFromDevices() {
    uint16_t agg = 0;
    for (const auto& [devFd, info] : devicesByFd_) {
        (void)devFd;
        agg |= info.heldModMask;
    }
    leftShift_  = (agg & (1u << 0)) != 0;
    rightShift_ = (agg & (1u << 1)) != 0;
    leftCtrl_   = (agg & (1u << 2)) != 0;
    rightCtrl_  = (agg & (1u << 3)) != 0;
    leftAlt_    = (agg & (1u << 4)) != 0;
    rightAlt_   = (agg & (1u << 5)) != 0;
    leftMeta_   = (agg & (1u << 6)) != 0;
    rightMeta_  = (agg & (1u << 7)) != 0;
}

bool EvdevCapture::effectiveCompositorCapsLock() const {
    if (emitter_.hasVirtualCapsLed()) {
        return emitter_.virtualCapsLockOn();
    }
    if (hasExternalCapsLockHint_) {
        return externalCapsLockOn_;
    }
    return capsLockOn_;
}

void EvdevCapture::resetInputTrackingState() {
    for (auto& [devFd, info] : devicesByFd_) {
        (void)devFd;
        info.heldModMask = 0;
    }
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
            enqueueCancelSuperTap();
        }
        bool newMode = !config_.vietnameseEnabled;
        config_.vietnameseEnabled = newMode;
        processor_.setVietnameseEnabled(newMode);
        langToggleEpoch_.fetch_add(1, std::memory_order_acq_rel);
        if (onLangToggled_) onLangToggled_(newMode, activeAppClass_);
        return true;
    }

    if (matchModOnly(config_.shortcuts.restoreWord) && !processor_.currentRawKeys().empty()) {
        auto diff = processor_.restoreCurrentWord();
        if (diff.wasTransformed) {
            enqueueEmitDiff(diff.backspaces, diff.textToEmit, diff.encodedToEmit,
                            config_.defaultOutputMethod, config_.defaultDelayUs, false,
                            config_.clipboardRestoreDelayMs, effectiveCompositorCapsLock());
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
            enqueueCancelSuperTap(); // Prevent KWin Kickoff launcher from opening on Super release
        }
        releaseHeldModifiersOnUinput();
        bool newMode = !config_.vietnameseEnabled;
        config_.vietnameseEnabled = newMode;
        processor_.setVietnameseEnabled(newMode);
        langToggleEpoch_.fetch_add(1, std::memory_order_acq_rel);
        if (onLangToggled_) onLangToggled_(newMode, activeAppClass_);
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
            enqueueEmitDiff(diff.backspaces, diff.textToEmit, diff.encodedToEmit,
                            config_.defaultOutputMethod, config_.defaultDelayUs, false,
                            config_.clipboardRestoreDelayMs, effectiveCompositorCapsLock());
            return true;
        }
    }

    return false;
}

void EvdevCapture::handleKeyboardEvent(int sourceFd, const input_event& ev, bool hasMoreInBatch) {
    if (ev.type == EV_SYN && ev.code == SYN_DROPPED) {
        // Resync held modifier state with kernel EVIOCGKEY and clear partial buffer so
        // kernel ring drops never leave stuck Ctrl/Alt/Shift or half-dropped syllables.
        synDropRecoveries_.fetch_add(1, std::memory_order_relaxed);
        resyncDeviceModifiersLocked(sourceFd, true);
        processor_.reset();
        forwardedKeys_.reset();
        armedModMask_ = 0;
        return;
    }

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
        if (code == KEY_CAPSLOCK) {
            // Only process press (1) and release (0); ignore CapsLock autorepeat (2) to prevent erratic toggles.
            if (value == 2) {
                return;
            }
            if (value == 1) {
                capsLockOn_ = !capsLockOn_;
                if (hasExternalCapsLockHint_) {
                    externalCapsLockOn_ = !externalCapsLockOn_;
                }
                drainOutputQueue();
                emitter_.forwardKeyEvent(code, value);
                return;
            }
            enqueueRawKey(code, value);
            return;
        }

        const uint16_t modBit = modifierBitForKeycode(code);
        auto devIt = devicesByFd_.find(sourceFd);
        if (modBit != 0 && devIt != devicesByFd_.end()) {
            if (pressed) {
                devIt->second.heldModMask |= modBit;
            } else {
                devIt->second.heldModMask &= static_cast<uint16_t>(~modBit);
            }
            recomputeModifiersFromDevices();
        } else {
            switch (code) {
                case KEY_LEFTSHIFT:  leftShift_  = pressed; break;
                case KEY_RIGHTSHIFT: rightShift_ = pressed; break;
                case KEY_LEFTCTRL:   leftCtrl_   = pressed; break;
                case KEY_RIGHTCTRL:  rightCtrl_  = pressed; break;
                case KEY_LEFTALT:    leftAlt_    = pressed; break;
                case KEY_RIGHTALT:   rightAlt_   = pressed; break;
                case KEY_LEFTMETA:   leftMeta_   = pressed; break;
                case KEY_RIGHTMETA:  rightMeta_  = pressed; break;
            }
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
                enqueueRawKey(code, value);
                releaseHeldModifiersOnUinput();
                return;
            }
        }

        enqueueRawKey(code, value);
        return;
    }

    // Any non-modifier key cancels modifier-only chord
    if (value == 1) {
        armedModMask_ = 0;
        if (!hasMoreInBatch) {
            // Self-heal any phantom modifier whose release packet was lost (e.g. 2.4G wireless idle/RF drop).
            resyncStuckModifiersBeforeKeyLocked();
        }
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
            enqueueRawKey(code, 0);
        }
        return;
    }

    // 5. If Ctrl, Alt, or Meta is held, reset buffer and forward shortcut key directly
    if (anyCtrl || anyAlt || anyMeta) {
        processor_.reset();
        forwardedKeys_.set(code, true);
        enqueueRawKey(code, value);
        return;
    }

    // 6. Determine per-app profile settings
    const AppProfile* appProf = config_.findProfileForApp(activeAppClass_);
    bool appExcluded = (appProf && appProf->mode == AppMode::Excluded) ||
                       (config_.bypassPasswordDialogs && activeIsPasswordDialog_);
    OutputMethod outMethod = (appProf && appProf->useCustomOutputMethod)
                                 ? appProf->outputMethod
                                 : config_.defaultOutputMethod;
    if (outMethod == OutputMethod::XkbKeymap) {
        int fb = xkbFallbackMethod_.load(std::memory_order_acquire);
        if (fb >= 0 && fb <= static_cast<int>(OutputMethod::ClipboardCtrlShiftV)) {
            outMethod = static_cast<OutputMethod>(fb);
        }
    }
    int delayUs = (appProf && appProf->delayUs > 0) ? appProf->delayUs : config_.defaultDelayUs;
    bool autocompleteFix = (appProf && appProf->autocompleteFix);
    if (!autocompleteFix && !activeAppClass_.empty()) {
        std::string lowerApp = activeAppClass_;
        for (char& c : lowerApp) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const char* b : {"firefox", "chrome", "chromium", "brave", "vivaldi", "edge", "opera", "zen", "waterfox", "librewolf", "tor-browser"}) {
            if (lowerApp.find(b) != std::string::npos) {
                autocompleteFix = true;
                break;
            }
        }
    }

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
        forwardedKeys_.set(code, true);
        enqueueRawKey(code, value);
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
            onGameChatKey_(true);
        }
        if (!gameChatActive_.load(std::memory_order_acquire)) {
            pendingProcessorReset_.store(false, std::memory_order_relaxed);
            processor_.reset();
            if (!forwardedKeys_.test(code)) {
                enqueueRawKey(code, 1);
                forwardedKeys_.set(code, true);
            }
            enqueueRawKey(code, 2);
            return;
        }
    }

    const bool compLock = effectiveCompositorCapsLock();
    const int tapHoldUs = (delayUs > 0) ? std::clamp(delayUs / 3, 200, 350) : 0;

    // 7. Handle Backspace
    if (code == KEY_BACKSPACE) {
        if (isGameMode && gameChatActive_.load(std::memory_order_acquire) && value == 1 && onGameChatKey_) {
            onGameChatKey_(false);
        }
        const bool hadBufferedWord = !processor_.currentRawKeys().empty();
        const bool hadHistory = processor_.hasUndoableHistory();
        auto diff = processor_.processBackspace();
        if (diff.passThrough) {
            if (autocompleteFix && hadBufferedWord) {
                enqueueEmitDiff(1, U"", "", outMethod, delayUs, true, config_.clipboardRestoreDelayMs, compLock);
            } else if (!hadBufferedWord && !hadHistory && !autocompleteFix && !anyShift) {
                // Buffer is empty: preserve native press/repeat/release lifecycle so compositor
                // key repeat & press duration behave identically to a physical keyboard.
                if (value == 2 && !forwardedKeys_.test(KEY_BACKSPACE)) {
                    // User began holding Backspace while inside a buffered word and crossed to empty buffer
                    enqueuePassTap(KEY_BACKSPACE, tapHoldUs);
                } else {
                    forwardedKeys_.set(KEY_BACKSPACE, true);
                    enqueueRawKey(KEY_BACKSPACE, value);
                }
            } else if (anyShift) {
                // Temporarily release Shift during fast uppercase rollover so Backspace isn't swallowed as Shift+Bksp
                enqueueEmitDiff(1, U"", "", outMethod, delayUs, autocompleteFix, config_.clipboardRestoreDelayMs, compLock);
            } else {
                enqueuePassTap(KEY_BACKSPACE, tapHoldUs);
            }
        } else {
            const bool isAtEndOfWord = (processor_.cursorInWord() >= processor_.currentDisplayedWord().size());
            const bool effectiveAutocompleteFix = (autocompleteFix && isAtEndOfWord);
            enqueueEmitDiff(diff.backspaces, diff.textToEmit, diff.encodedToEmit,
                            outMethod, delayUs, effectiveAutocompleteFix, config_.clipboardRestoreDelayMs, compLock);
        }
        return;
    }

    // 8. Handle Navigation / Reset keys (Enter, Tab, Arrows, Home, End, PageUp, PageDown, Esc, Delete)
    if (isNavigationOrResetKey(code)) {
        const bool isHardLineOrFieldEnd =
            (code == KEY_ENTER || code == KEY_KPENTER || code == KEY_TAB || code == KEY_ESC || code == KEY_INSERT);
        if (isGameMode && gameChatActive_.load(std::memory_order_acquire) && value == 1 &&
            (code == KEY_ENTER || code == KEY_KPENTER || code == KEY_ESC)) {
            if (!gameSignalControlled_.load(std::memory_order_acquire)) {
                gameChatActive_.store(false, std::memory_order_release);
                processor_.reset();
            } else if (onGameChatKey_) {
                onGameChatKey_(true);
            }
        } else if (isHardLineOrFieldEnd) {
            // Enter/Tab/Esc/Insert ALWAYS finish the current line/field regardless of resetOnNavigationKeys or autocompleteFix.
            processor_.reset();
        } else if (!anyShift && !autocompleteFix &&
            (code == KEY_LEFT || code == KEY_RIGHT || code == KEY_DELETE)) {
            if (code == KEY_LEFT) {
                processor_.moveCursorLeft();
            } else if (code == KEY_RIGHT) {
                processor_.moveCursorRight();
            } else {
                processor_.processDelete();
            }
        } else if (config_.resetOnNavigationKeys || autocompleteFix || anyShift) {
            processor_.reset();
        }
        forwardedKeys_.set(code, true);
        enqueueRawKey(code, value);
        return;
    }

    // 9. Handle Printable keys
    char32_t ch = keycodeToPrintable(code, anyShift, capsLockOn_);
    if (ch == 0) {
        forwardedKeys_.set(code, true);
        enqueueRawKey(code, value);
        return;
    }
    const bool isMovementKey = (code == KEY_W || code == KEY_A || code == KEY_S || code == KEY_D);
    if (isGameMode && gameChatActive_.load(std::memory_order_acquire) && value == 1 && !isMovementKey && onGameChatKey_) {
        onGameChatKey_(false);
    }

    auto diff = processor_.processPrintableKey(ch);
    const bool isAsciiAlpha = (ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z');
    if (diff.passThrough) {
        // If compositor Lock state disagrees with physical CapsLock (e.g. virtual keyboard created while
        // CapsLock LED was already ON, or compositor ignored CapsLock), actively emit Shift-adjusted
        // character instead of raw unshifted forwardKeyEvent so ASCII letters keep the exact same case
        // as Vietnamese diacritic letters.
        if (isAsciiAlpha && compLock != capsLockOn_) {
            std::u32string singleChar(1, ch);
            enqueueEmitDiff(0, singleChar, std::string(1, static_cast<char>(ch)),
                            outMethod, delayUs, false, config_.clipboardRestoreDelayMs, compLock);
        } else {
            // Emit as an atomic tap with micro-hold pacing so N-key rollover and fast bursts never drop or
            // coalesce zero-duration characters ahead of subsequent Backspaces.
            enqueuePassTap(code, tapHoldUs);
        }
    } else {
        const bool isAtEndOfWord = (processor_.cursorInWord() >= processor_.currentDisplayedWord().size());
        const bool effectiveAutocompleteFix = (autocompleteFix && isAtEndOfWord);
        enqueueEmitDiff(diff.backspaces, diff.textToEmit, diff.encodedToEmit,
                        outMethod, delayUs, effectiveAutocompleteFix, config_.clipboardRestoreDelayMs, compLock);
    }
}

} // namespace vnsnowkey
