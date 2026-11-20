/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "../config/config.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace vnsnowkey {

enum class DesktopBackend {
    None = 0,
    KWin,
    Hyprland,
    Sway,
    GnomeShell,
    X11,
};

DesktopBackend detectDesktopBackend(
    const std::string& xdgDesktop,
    const std::string& sessionType,
    const std::string& hyprlandSignature,
    const std::string& swaySocket,
    const std::string& waylandDisplay,
    const std::string& x11Display);

const char* desktopBackendToString(DesktopBackend backend);

bool parseHyprlandActiveWindowLine(
    const std::string& line,
    std::string& outAppClass,
    std::string& outCaption);

bool parseHyprlandActiveWindowJson(
    const std::string& jsonStr,
    std::string& outAppClass,
    std::string& outCaption,
    std::string& outWindowId);

bool parseSwayWindowEventJson(
    const std::string& jsonStr,
    std::string& outAppClass,
    std::string& outAppName,
    std::string& outCaption,
    std::string& outWindowId);

struct WindowFocusInfo {
    std::string appClass;
    std::string appName;
    std::string caption;
    std::string windowId;
    bool isPasswordDialog = false;
};

class FocusTracker {
public:
    using FocusChangeCallback = std::function<void(const WindowFocusInfo& info, bool newVietnameseState, bool langChanged)>;
    using RawFocusCallback = std::function<void(
        const std::string& appClass,
        const std::string& appName,
        const std::string& caption,
        const std::string& windowId)>;

    FocusTracker();
    ~FocusTracker();

    FocusTracker(const FocusTracker&) = delete;
    FocusTracker& operator=(const FocusTracker&) = delete;

    // Auto-detect current desktop (KDE KWin, Hyprland, Sway, GNOME Shell, X11) and start focus monitoring
    bool startDesktopFocusMonitor();
    void stopDesktopFocusMonitor();
    DesktopBackend activeBackend() const { return activeBackend_; }
    bool isDesktopMonitorActive() const;
    bool isGnomeExtensionActive() const { return gnomeMonitorActive_.load(); }

    // Install and load KWin focus script via org.kde.KWin /Scripting D-Bus interface
    bool registerKWinScript();
    void unregisterKWinScript();
    bool isKWinScriptLoaded() const;

    // Called when compositor/DE reports focus change over D-Bus or native IPC
    void onWindowFocusChanged(
        const std::string& appClass,
        const std::string& appName,
        const std::string& caption,
        const std::string& windowId,
        const Config& config,
        bool currentVietnameseEnabled);

    // Record user's manual language toggle for the currently focused app
    void recordAppLanguageState(bool vietnameseEnabled);

    WindowFocusInfo currentFocus() const;
    std::vector<std::string> recentAppClasses() const;

    void setOnFocusChanged(FocusChangeCallback cb) { onFocusChanged_ = std::move(cb); }
    void setOnRawFocusEvent(RawFocusCallback cb) { onRawFocus_ = std::move(cb); }
    using XkbMappingChangedCallback = std::function<void()>;
    void setOnXkbMappingChanged(XkbMappingChangedCallback cb) { onXkbMappingChanged_ = std::move(cb); }

    static bool detectPasswordDialog(const std::string& appClass, const std::string& appName, const std::string& caption);

private:
    std::string ensureLocalScriptFile() const;
    bool startGnomeExtension();
    void runHyprlandLoop();
    void runSwayLoop();
    void runX11Loop();
    void emitRawFocus(
        const std::string& appClass,
        const std::string& appName,
        const std::string& caption,
        const std::string& windowId);

    mutable std::mutex mutex_;
    WindowFocusInfo currentFocus_;
    std::map<std::string, bool> perAppLangMemory_;
    std::optional<bool> lastDefaultLang_;
    std::set<std::string> recentApps_;
    FocusChangeCallback onFocusChanged_;
    RawFocusCallback onRawFocus_;
    XkbMappingChangedCallback onXkbMappingChanged_;
    DesktopBackend activeBackend_ = DesktopBackend::None;
    bool scriptRegistered_ = false;
    std::atomic<bool> gnomeMonitorActive_{false};
    std::atomic<bool> running_{false};
    int wakeFd_ = -1;
    std::thread workerThread_;
};

} // namespace vnsnowkey
