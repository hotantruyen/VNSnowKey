/*
 * VNSnowKey - Vietnamese Input Method for Arch Linux + KDE Plasma (Wayland)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace vnsnowkey {

enum class GameSignalSource {
    None = 0,
    XWaylandCursor = 1,
    KWinTextInput = 2,
    WaylandInputMethod = 3,
    DesktopCursor = 4,
};

struct GameSignalSnapshot {
    bool gameModeActive = false;
    bool hasDesktopCursorSignal = false;
    bool desktopCursorVisible = false;
    bool hasX11WindowFocus = false;
    bool x11CursorVisible = false;
    bool hasWaylandImBridge = false;
    bool waylandImTextInputActive = false;
    bool hasKWinTextInputTransition = false;
    bool kwinTextInputSupported = false;
    bool cursorExitOverride = false;
};

struct GameChatDecision {
    bool chatActive = false;
    GameSignalSource source = GameSignalSource::None;
};

// Pure state-fusion helper: determines whether a GameMode app is currently in Chat/UI vs 3D Gameplay.
GameChatDecision evaluateGameChatDecision(const GameSignalSnapshot& snap);
const char* gameSignalSourceToString(GameSignalSource src);

// Monitors system signals (XWayland cursor visibility via XFixes, plus KWin VirtualKeyboard text-input)
// ONLY while a window whose profile is AppMode::GameMode holds focus.
class GameModeMonitor {
public:
    using StateCallback = std::function<void(bool chatActive, GameSignalSource source)>;

    GameModeMonitor();
    ~GameModeMonitor();

    GameModeMonitor(const GameModeMonitor&) = delete;
    GameModeMonitor& operator=(const GameModeMonitor&) = delete;

    void start();
    void stop();

    void setOnStateChanged(StateCallback cb);

    // Enable or disable monitoring as focus enters/leaves an AppMode::GameMode window.
    void setGameModeActive(bool active, const std::string& appClass = "");

    // Feed KWin org.kde.kwin.VirtualKeyboard.activeClientSupportsTextInput state/transitions.
    void setKWinTextInputState(bool supported, bool isTransition);

    // Feed standalone Wayland zwp_input_method_v1 bridge (vnsnowkey-im) state.
    void setWaylandImState(bool textInputActive, bool bridgeAlive);

    // Feed compositor-native cursor visibility (e.g. GNOME Meta.CursorTracker via D-Bus extension).
    void setDesktopCursorState(bool visible, bool hasSignal);

    // Inform monitor when running on GNOME Shell so stuck-visible XFixes cursor images never trap 3D WASD.
    void setCompositorIsGnome(bool isGnome);

    // Notify monitor about keystrokes inside GameMode chat to prevent transient pointer auto-hide
    // from dropping chat mode mid-word; pressing Enter/Esc clears grace immediately.
    void notifyChatKeystroke(bool isExitKey = false);

    // Trigger an immediate worker poll (e.g. when T or / opens chat in GameMode).
    void requestSignalProbe();

    bool isGameModeActive() const;
    bool isChatActive() const;
    bool isWaylandImBridgeAlive() const;
    GameSignalSource currentSource() const;

private:
    struct X11Handle;

    void workerLoop();
    void signalWorkerWakeup();
    void notifyIfChangedLocked(std::unique_lock<std::mutex>& lock);

    bool ensureX11Connected(X11Handle& x11);
    void disconnectX11(X11Handle& x11);
    bool probeX11Snapshot(X11Handle& x11, bool& outHasFocus, bool& outCursorVisible);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread workerThread_;
    std::atomic<bool> running_{false};
    int wakeFd_ = -1;

    GameSignalSnapshot snapshot_;
    std::string activeAppClass_;
    bool isGnomeCompositor_ = false;
    bool desktopEverSawHiddenCursor_ = false;
    bool x11EverSawHiddenCursor_ = false;
    bool sawCursorNotifyInSession_ = false;
    int64_t desktopHiddenSinceMs_ = 0;
    int64_t x11HiddenSinceMs_ = 0;
    bool lastEmittedChatActive_ = false;
    GameSignalSource lastEmittedSource_ = GameSignalSource::None;
    std::atomic<int64_t> lastChatTypingMs_{0};
    StateCallback onStateChanged_;
};

} // namespace vnsnowkey
