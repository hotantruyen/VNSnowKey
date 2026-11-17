/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "game_mode_monitor.hpp"
#include "xwayland_env.hpp"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace vnsnowkey {

namespace {

constexpr long kXFixesDisplayCursorNotifyMask = (1L << 0);
constexpr int kXFixesDisplayCursorNotify = 0;
constexpr int64_t kChatTypingGraceMs = 1500;

int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct XFixesCursorImageAbi {
    short x, y;
    unsigned short width, height;
    unsigned short xhot, yhot;
    unsigned long cursor_serial;
    unsigned long* pixels;
    unsigned long atom;
    const char* name;
};

struct XEventStub {
    int type;
    char pad[192];
};

std::atomic<bool> g_x11TransientError{false};

int safeXErrorHandler(void* /*dpy*/, void* /*err*/) {
    g_x11TransientError.store(true, std::memory_order_relaxed);
    return 0;
}

bool isCursorImageVisible(const XFixesCursorImageAbi* img) {
    if (!img || img->width == 0 || img->height == 0 || !img->pixels) {
        return false;
    }
    const size_t total = static_cast<size_t>(img->width) * static_cast<size_t>(img->height);
    for (size_t i = 0; i < total; ++i) {
        uint32_t argb = static_cast<uint32_t>(img->pixels[i]);
        if ((argb & 0xFF000000u) != 0) {
            return true;
        }
    }
    return false;
}

} // namespace

GameChatDecision evaluateGameChatDecision(const GameSignalSnapshot& snap) {
    if (!snap.gameModeActive) {
        return {false, GameSignalSource::None};
    }
    if (snap.hasDesktopCursorSignal) {
        // When an XWayland game (e.g. Minecraft) hides its X11 cursor for 3D camera lock,
        // honour XWaylandCursor(false) even if GNOME Meta.CursorTracker still reports true.
        if (snap.desktopCursorVisible && snap.hasX11WindowFocus && !snap.x11CursorVisible) {
            return {false, GameSignalSource::XWaylandCursor};
        }
        return {snap.desktopCursorVisible && !snap.cursorExitOverride, GameSignalSource::DesktopCursor};
    }
    if (snap.hasX11WindowFocus) {
        return {snap.x11CursorVisible && !snap.cursorExitOverride, GameSignalSource::XWaylandCursor};
    }
    if (snap.hasWaylandImBridge) {
        return {snap.waylandImTextInputActive, GameSignalSource::WaylandInputMethod};
    }
    if (snap.hasKWinTextInputTransition) {
        return {snap.kwinTextInputSupported, GameSignalSource::KWinTextInput};
    }
    return {false, GameSignalSource::None};
}

const char* gameSignalSourceToString(GameSignalSource src) {
    switch (src) {
        case GameSignalSource::None: return "none";
        case GameSignalSource::XWaylandCursor: return "xwayland_cursor";
        case GameSignalSource::KWinTextInput: return "kwin_text_input";
        case GameSignalSource::WaylandInputMethod: return "wayland_im_bridge";
        case GameSignalSource::DesktopCursor: return "desktop_cursor";
    }
    return "none";
}

struct GameModeMonitor::X11Handle {
    void* libX11 = nullptr;
    void* libXfixes = nullptr;
    void* dpy = nullptr;
    unsigned long root = 0;
    unsigned long atomNetActiveWindow = 0;
    unsigned long atomWmClass = 0;
    int fixesEventBase = -1;
    bool hasFixes = false;

    void* (*pXOpenDisplay)(const char*) = nullptr;
    int (*pXCloseDisplay)(void*) = nullptr;
    unsigned long (*pXDefaultRootWindow)(void*) = nullptr;
    int (*pXConnectionNumber)(void*) = nullptr;
    unsigned long (*pXInternAtom)(void*, const char*, int) = nullptr;
    int (*pXGetWindowProperty)(
        void*, unsigned long, unsigned long, long, long, int, unsigned long,
        unsigned long*, int*, unsigned long*, unsigned long*, unsigned char**) = nullptr;
    int (*pXGetInputFocus)(void*, unsigned long*, int*) = nullptr;
    int (*pXQueryTree)(void*, unsigned long, unsigned long*, unsigned long*, unsigned long**, unsigned int*) = nullptr;
    int (*pXFree)(void*) = nullptr;
    int (*pXPending)(void*) = nullptr;
    int (*pXNextEvent)(void*, void*) = nullptr;
    int (*pXFlush)(void*) = nullptr;
    void* (*pXSetErrorHandler)(int (*)(void*, void*)) = nullptr;

    int (*pXFixesQueryExtension)(void*, int*, int*) = nullptr;
    void (*pXFixesSelectCursorInput)(void*, unsigned long, unsigned long) = nullptr;
    XFixesCursorImageAbi* (*pXFixesGetCursorImage)(void*) = nullptr;
};

GameModeMonitor::GameModeMonitor() {
    wakeFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

GameModeMonitor::~GameModeMonitor() {
    stop();
    if (wakeFd_ >= 0) {
        ::close(wakeFd_);
        wakeFd_ = -1;
    }
}

void GameModeMonitor::start() {
    if (running_.exchange(true)) {
        return;
    }
    workerThread_ = std::thread(&GameModeMonitor::workerLoop, this);
}

void GameModeMonitor::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    signalWorkerWakeup();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void GameModeMonitor::setOnStateChanged(StateCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    onStateChanged_ = std::move(cb);
}

void GameModeMonitor::setGameModeActive(bool active, const std::string& appClass) {
    std::unique_lock<std::mutex> lock(mutex_);
    bool appChanged = (activeAppClass_ != appClass);
    bool modeChanged = (snapshot_.gameModeActive != active);

    snapshot_.gameModeActive = active;
    activeAppClass_ = appClass;

    if (!active || appChanged) {
        snapshot_.hasDesktopCursorSignal = false;
        snapshot_.desktopCursorVisible = false;
        snapshot_.hasX11WindowFocus = false;
        snapshot_.x11CursorVisible = false;
        snapshot_.hasKWinTextInputTransition = false;
        snapshot_.cursorExitOverride = false;
        desktopEverSawHiddenCursor_ = false;
        x11EverSawHiddenCursor_ = false;
        sawCursorNotifyInSession_ = false;
        desktopHiddenSinceMs_ = 0;
        x11HiddenSinceMs_ = 0;
        lastChatTypingMs_.store(0, std::memory_order_relaxed);
    }

    if (modeChanged || appChanged) {
        notifyIfChangedLocked(lock);
        cv_.notify_all();
        signalWorkerWakeup();
    }
}

void GameModeMonitor::setKWinTextInputState(bool supported, bool isTransition) {
    std::unique_lock<std::mutex> lock(mutex_);
    bool prev = snapshot_.kwinTextInputSupported;
    snapshot_.kwinTextInputSupported = supported;
    if (isTransition && snapshot_.gameModeActive && prev != supported) {
        snapshot_.hasKWinTextInputTransition = true;
    }
    notifyIfChangedLocked(lock);
}

void GameModeMonitor::setWaylandImState(bool textInputActive, bool bridgeAlive) {
    std::unique_lock<std::mutex> lock(mutex_);
    snapshot_.hasWaylandImBridge = bridgeAlive;
    snapshot_.waylandImTextInputActive = bridgeAlive && textInputActive;
    notifyIfChangedLocked(lock);
}

void GameModeMonitor::setDesktopCursorState(bool visible, bool hasSignal) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!hasSignal) {
        snapshot_.hasDesktopCursorSignal = false;
        snapshot_.desktopCursorVisible = false;
        snapshot_.cursorExitOverride = false;
        desktopEverSawHiddenCursor_ = false;
        desktopHiddenSinceMs_ = 0;
    } else {
        const int64_t nowMs = steadyNowMs();
        if (!visible) {
            if (desktopHiddenSinceMs_ == 0) {
                desktopHiddenSinceMs_ = nowMs;
            }
            desktopEverSawHiddenCursor_ = true;
            if (nowMs - desktopHiddenSinceMs_ >= 300) {
                snapshot_.cursorExitOverride = false;
            }
        } else {
            if (desktopHiddenSinceMs_ != 0 && (nowMs - desktopHiddenSinceMs_ >= 300)) {
                snapshot_.cursorExitOverride = false;
            }
            desktopHiddenSinceMs_ = 0;
        }
        // Do not let DesktopCursor(true) freeze a GameMode session in chat/UI
        // before a hidden cursor (visible == false) is observed at least once.
        if (!visible || desktopEverSawHiddenCursor_) {
            snapshot_.hasDesktopCursorSignal = true;
            snapshot_.desktopCursorVisible = visible;
        } else {
            snapshot_.hasDesktopCursorSignal = false;
            snapshot_.desktopCursorVisible = false;
        }
    }
    notifyIfChangedLocked(lock);
}

void GameModeMonitor::setCompositorIsGnome(bool isGnome) {
    std::lock_guard<std::mutex> lock(mutex_);
    isGnomeCompositor_ = isGnome;
}

void GameModeMonitor::notifyChatKeystroke(bool isExitKey) {
    if (isExitKey) {
        lastChatTypingMs_.store(0, std::memory_order_relaxed);
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (snapshot_.gameModeActive &&
                !snapshot_.hasWaylandImBridge &&
                !(snapshot_.hasKWinTextInputTransition && snapshot_.kwinTextInputSupported) &&
                ((snapshot_.hasDesktopCursorSignal && snapshot_.desktopCursorVisible) ||
                 (snapshot_.hasX11WindowFocus && snapshot_.x11CursorVisible))) {
                snapshot_.cursorExitOverride = true;
            }
            notifyIfChangedLocked(lock);
        }
        cv_.notify_all();
        signalWorkerWakeup();
        return;
    }
    int64_t nowMs = steadyNowMs();
    lastChatTypingMs_.store(nowMs > 0 ? nowMs : 1, std::memory_order_relaxed);
}

bool GameModeMonitor::isGameModeActive() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_.gameModeActive;
}

bool GameModeMonitor::isChatActive() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastEmittedChatActive_;
}

bool GameModeMonitor::isWaylandImBridgeAlive() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_.hasWaylandImBridge;
}

GameSignalSource GameModeMonitor::currentSource() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return evaluateGameChatDecision(snapshot_).source;
}

void GameModeMonitor::signalWorkerWakeup() {
    if (wakeFd_ >= 0) {
        uint64_t one = 1;
        ssize_t n = ::write(wakeFd_, &one, sizeof(one));
        (void)n;
    }
}

void GameModeMonitor::notifyIfChangedLocked(std::unique_lock<std::mutex>& lock) {
    GameChatDecision dec = evaluateGameChatDecision(snapshot_);
    // Prevent transient pointer auto-hide during active typing (e.g. X11/GNOME cursor-hide
    // while typing) from dropping Vietnamese chat mode mid-word.
    if (snapshot_.gameModeActive && lastEmittedChatActive_ && !dec.chatActive &&
        (dec.source == GameSignalSource::DesktopCursor ||
         dec.source == GameSignalSource::XWaylandCursor)) {
        const int64_t lastType = lastChatTypingMs_.load(std::memory_order_relaxed);
        if (lastType > 0 && (steadyNowMs() - lastType) < kChatTypingGraceMs) {
            return;
        }
    }
    if (!dec.chatActive) {
        lastChatTypingMs_.store(0, std::memory_order_relaxed);
    }
    if (dec.chatActive == lastEmittedChatActive_ && dec.source == lastEmittedSource_) {
        return;
    }
    lastEmittedChatActive_ = dec.chatActive;
    lastEmittedSource_ = dec.source;
    StateCallback cb = onStateChanged_;
    lock.unlock();
    if (cb) {
        cb(dec.chatActive, dec.source);
    }
    lock.lock();
}

bool GameModeMonitor::ensureX11Connected(X11Handle& x11) {
    if (x11.dpy) return true;
    std::string dispStr = xwayland_env::resolveLiveX11Display();
    if (dispStr.empty()) return false;

    if (!x11.libX11) {
        x11.libX11 = ::dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
        if (!x11.libX11) return false;
        x11.pXOpenDisplay = reinterpret_cast<decltype(x11.pXOpenDisplay)>(::dlsym(x11.libX11, "XOpenDisplay"));
        x11.pXCloseDisplay = reinterpret_cast<decltype(x11.pXCloseDisplay)>(::dlsym(x11.libX11, "XCloseDisplay"));
        x11.pXDefaultRootWindow = reinterpret_cast<decltype(x11.pXDefaultRootWindow)>(::dlsym(x11.libX11, "XDefaultRootWindow"));
        x11.pXConnectionNumber = reinterpret_cast<decltype(x11.pXConnectionNumber)>(::dlsym(x11.libX11, "XConnectionNumber"));
        x11.pXInternAtom = reinterpret_cast<decltype(x11.pXInternAtom)>(::dlsym(x11.libX11, "XInternAtom"));
        x11.pXGetWindowProperty = reinterpret_cast<decltype(x11.pXGetWindowProperty)>(::dlsym(x11.libX11, "XGetWindowProperty"));
        x11.pXGetInputFocus = reinterpret_cast<decltype(x11.pXGetInputFocus)>(::dlsym(x11.libX11, "XGetInputFocus"));
        x11.pXQueryTree = reinterpret_cast<decltype(x11.pXQueryTree)>(::dlsym(x11.libX11, "XQueryTree"));
        x11.pXFree = reinterpret_cast<decltype(x11.pXFree)>(::dlsym(x11.libX11, "XFree"));
        x11.pXPending = reinterpret_cast<decltype(x11.pXPending)>(::dlsym(x11.libX11, "XPending"));
        x11.pXNextEvent = reinterpret_cast<decltype(x11.pXNextEvent)>(::dlsym(x11.libX11, "XNextEvent"));
        x11.pXFlush = reinterpret_cast<decltype(x11.pXFlush)>(::dlsym(x11.libX11, "XFlush"));
        x11.pXSetErrorHandler = reinterpret_cast<decltype(x11.pXSetErrorHandler)>(::dlsym(x11.libX11, "XSetErrorHandler"));
    }
    if (!x11.pXOpenDisplay || !x11.pXCloseDisplay || !x11.pXDefaultRootWindow ||
        !x11.pXConnectionNumber || !x11.pXInternAtom || !x11.pXGetWindowProperty ||
        !x11.pXGetInputFocus || !x11.pXFree || !x11.pXPending || !x11.pXNextEvent || !x11.pXFlush) {
        return false;
    }

    if (!x11.libXfixes) {
        x11.libXfixes = ::dlopen("libXfixes.so.3", RTLD_LAZY | RTLD_LOCAL);
        if (x11.libXfixes) {
            x11.pXFixesQueryExtension = reinterpret_cast<decltype(x11.pXFixesQueryExtension)>(
                ::dlsym(x11.libXfixes, "XFixesQueryExtension"));
            x11.pXFixesSelectCursorInput = reinterpret_cast<decltype(x11.pXFixesSelectCursorInput)>(
                ::dlsym(x11.libXfixes, "XFixesSelectCursorInput"));
            x11.pXFixesGetCursorImage = reinterpret_cast<decltype(x11.pXFixesGetCursorImage)>(
                ::dlsym(x11.libXfixes, "XFixesGetCursorImage"));
        }
    }
    if (!x11.pXFixesQueryExtension || !x11.pXFixesSelectCursorInput || !x11.pXFixesGetCursorImage) {
        return false;
    }

    if (x11.pXSetErrorHandler) {
        x11.pXSetErrorHandler(&safeXErrorHandler);
    }

    x11.dpy = x11.pXOpenDisplay(dispStr.c_str());
    if (!x11.dpy && !xwayland_env::isIsolatedTestEnv()) {
        if (xwayland_env::ensureXAuthorityEnv(true)) {
            x11.dpy = x11.pXOpenDisplay(dispStr.c_str());
        }
    }
    if (!x11.dpy) return false;

    int evBase = 0, errBase = 0;
    if (!x11.pXFixesQueryExtension(x11.dpy, &evBase, &errBase)) {
        disconnectX11(x11);
        return false;
    }

    x11.fixesEventBase = evBase;
    x11.hasFixes = true;
    x11.root = x11.pXDefaultRootWindow(x11.dpy);
    x11.atomNetActiveWindow = x11.pXInternAtom(x11.dpy, "_NET_ACTIVE_WINDOW", 0);
    x11.atomWmClass = x11.pXInternAtom(x11.dpy, "WM_CLASS", 0);

    x11.pXFixesSelectCursorInput(x11.dpy, x11.root, kXFixesDisplayCursorNotifyMask);
    x11.pXFlush(x11.dpy);
    return true;
}

void GameModeMonitor::disconnectX11(X11Handle& x11) {
    if (x11.dpy && x11.pXCloseDisplay) {
        x11.pXCloseDisplay(x11.dpy);
    }
    x11.dpy = nullptr;
    x11.root = 0;
    x11.hasFixes = false;
    if (x11.libXfixes) {
        ::dlclose(x11.libXfixes);
        x11.libXfixes = nullptr;
    }
    if (x11.libX11) {
        ::dlclose(x11.libX11);
        x11.libX11 = nullptr;
    }
}

bool GameModeMonitor::probeX11Snapshot(X11Handle& x11, bool& outHasFocus, bool& outCursorVisible) {
    outHasFocus = false;
    outCursorVisible = false;
    if (!x11.dpy || !x11.hasFixes) return false;

    bool sawNotify = false;
    while (x11.pXPending(x11.dpy) > 0) {
        XEventStub ev{};
        x11.pXNextEvent(x11.dpy, &ev);
        if (ev.type == x11.fixesEventBase + kXFixesDisplayCursorNotify) {
            sawNotify = true;
        }
    }

    unsigned long activeWid = 0;
    {
        unsigned long actualType = 0;
        int actualFormat = 0;
        unsigned long nitems = 0;
        unsigned long bytesAfter = 0;
        unsigned char* prop = nullptr;
        g_x11TransientError.store(false, std::memory_order_relaxed);
        int rc = x11.pXGetWindowProperty(
            x11.dpy, x11.root, x11.atomNetActiveWindow, 0, 1, 0, 0 /*AnyPropertyType*/,
            &actualType, &actualFormat, &nitems, &bytesAfter, &prop);
        if (rc == 0 && prop && nitems > 0 && actualFormat == 32) {
            activeWid = *reinterpret_cast<unsigned long*>(prop);
        }
        if (prop) x11.pXFree(prop);
    }

    if (activeWid == 0) {
        int revertTo = 0;
        x11.pXGetInputFocus(x11.dpy, &activeWid, &revertTo);
        // Xlib None=0, PointerRoot=1
        if (activeWid <= 1 || activeWid == x11.root) {
            activeWid = 0;
        }
    }

    bool hasWmClass = false;
    if (activeWid != 0 && x11.atomWmClass != 0) {
        unsigned long cur = activeWid;
        for (int depth = 0; depth < 16 && cur != 0 && cur != x11.root; ++depth) {
            unsigned long actualType = 0;
            int actualFormat = 0;
            unsigned long nitems = 0;
            unsigned long bytesAfter = 0;
            unsigned char* prop = nullptr;
            g_x11TransientError.store(false, std::memory_order_relaxed);
            int rc = x11.pXGetWindowProperty(
                x11.dpy, cur, x11.atomWmClass, 0, 128, 0, 0,
                &actualType, &actualFormat, &nitems, &bytesAfter, &prop);
            if (rc == 0 && prop && nitems > 0) {
                // Confirm WM_CLASS is non-empty (KWin Wayland focus proxy leaves WM_CLASS empty)
                if (prop[0] != '\0') {
                    hasWmClass = true;
                }
            }
            if (prop) x11.pXFree(prop);
            if (hasWmClass || !x11.pXQueryTree) break;

            unsigned long rootRet = 0, parentRet = 0;
            unsigned long* childrenRet = nullptr;
            unsigned int nchildren = 0;
            if (x11.pXQueryTree(x11.dpy, cur, &rootRet, &parentRet, &childrenRet, &nchildren) == 0) {
                break;
            }
            if (childrenRet) x11.pXFree(childrenRet);
            cur = parentRet;
        }
    }

    if (sawNotify) {
        std::lock_guard<std::mutex> lock(mutex_);
        sawCursorNotifyInSession_ = true;
    }

    bool notifiedDuringSession = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        notifiedDuringSession = sawCursorNotifyInSession_;
    }

    outHasFocus = hasWmClass || notifiedDuringSession;
    if (outHasFocus) {
        XFixesCursorImageAbi* img = x11.pXFixesGetCursorImage(x11.dpy);
        if (img) {
            outCursorVisible = isCursorImageVisible(img);
            x11.pXFree(img);
        } else {
            outCursorVisible = false;
        }
    }
    return true;
}

void GameModeMonitor::workerLoop() {
    X11Handle x11;

    while (running_.load()) {
        bool active = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return !running_.load() || snapshot_.gameModeActive;
            });
            if (!running_.load()) break;
            active = snapshot_.gameModeActive;
        }

        if (!active) {
            continue;
        }

        if (ensureX11Connected(x11)) {
            bool hasFocus = false;
            bool cursorVis = false;
            if (probeX11Snapshot(x11, hasFocus, cursorVis)) {
                std::unique_lock<std::mutex> lock(mutex_);
                if (snapshot_.gameModeActive) {
                    if (hasFocus && !cursorVis) {
                        int64_t nowMs = steadyNowMs();
                        if (x11HiddenSinceMs_ == 0) {
                            x11HiddenSinceMs_ = nowMs;
                        }
                        const bool sustainedHidden = (nowMs - x11HiddenSinceMs_ >= 300);
                        if (!isGnomeCompositor_ || sustainedHidden) {
                            x11EverSawHiddenCursor_ = true;
                        }
                        if (sustainedHidden) {
                            snapshot_.cursorExitOverride = false;
                        }
                    } else {
                        x11HiddenSinceMs_ = 0;
                    }
                    // On GNOME Mutter XWayland without extension cursor signal, XFixesGetCursorImage
                    // often keeps returning a visible arrow even under GLFW_CURSOR_DISABLED relative
                    // pointer lock. Never force chat=true while cursor has never hidden once.
                    if (isGnomeCompositor_ && !snapshot_.hasDesktopCursorSignal &&
                        cursorVis && !x11EverSawHiddenCursor_) {
                        snapshot_.hasX11WindowFocus = false;
                        snapshot_.x11CursorVisible = false;
                    } else {
                        snapshot_.hasX11WindowFocus = hasFocus;
                        snapshot_.x11CursorVisible = cursorVis;
                    }
                    notifyIfChangedLocked(lock);
                }
            }

            pollfd pfds[2]{};
            int nfds = 0;
            if (wakeFd_ >= 0) {
                pfds[nfds++] = {wakeFd_, POLLIN, 0};
            }
            int xfd = x11.pXConnectionNumber(x11.dpy);
            if (xfd >= 0) {
                pfds[nfds++] = {xfd, POLLIN, 0};
            }
            if (nfds > 0) {
                ::poll(pfds, static_cast<nfds_t>(nfds), 40);
                if (wakeFd_ >= 0 && (pfds[0].revents & POLLIN)) {
                    uint64_t val = 0;
                    ssize_t n = ::read(wakeFd_, &val, sizeof(val));
                    (void)n;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
        } else {
            // No X11/XWayland connection available (pure Wayland without DISPLAY):
            // sleep cleanly until KWin/Desktop D-Bus signal, mode change, or typing-grace expiry wakes us.
            std::unique_lock<std::mutex> lock(mutex_);
            const int64_t lastType = lastChatTypingMs_.load(std::memory_order_relaxed);
            if (lastEmittedChatActive_ && lastType > 0) {
                cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return !running_.load() || !snapshot_.gameModeActive ||
                           lastChatTypingMs_.load(std::memory_order_relaxed) == 0;
                });
                if (running_.load() && snapshot_.gameModeActive) {
                    notifyIfChangedLocked(lock);
                }
            } else {
                cv_.wait_for(lock, std::chrono::milliseconds(500), [this]() {
                    return !running_.load() || !snapshot_.gameModeActive ||
                           lastChatTypingMs_.load(std::memory_order_relaxed) > 0;
                });
            }
        }
    }

    disconnectX11(x11);
}

} // namespace vnsnowkey
