/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "../config/config.hpp"
#include "../engine/session_processor.hpp"
#include "../focus/focus_tracker.hpp"
#include "../focus/game_mode_monitor.hpp"
#include "../input/evdev_capture.hpp"
#include "../output/uinput_emitter.hpp"
#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>
#include <string>

namespace vnsnowkey {

class DaemonServer : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.vnsnowkey.Daemon")

public:
    explicit DaemonServer(const std::string& configPath = "", bool noGrab = false, QObject* parent = nullptr);
    ~DaemonServer() override;

    bool start(std::string* errorMsg = nullptr);
    void stop();

    static std::string buildDiagnosticsReport(
        const UinputEmitter* emitter = nullptr,
        const EvdevCapture* capture = nullptr,
        const FocusTracker* focus = nullptr,
        bool waylandImConnected = false);

public Q_SLOTS:
    bool Toggle();
    bool SetVietnamese(bool enabled);
    bool SetSafeMode(bool safe);
    bool PanicStop();
    bool ResumeFromPanic();
    QString GetStatusJson();
    QString GetStatus();
    bool ReloadConfig();
    bool SetInputMethod(const QString& imName);
    bool SetOutputCharset(const QString& charsetName);
    bool SetCharset(const QString& charsetName);
    bool ConvertClipboard(const QString& fromCharset, const QString& toCharset);
    bool ReapplyXkbOverlay();
    void NotifyWindowFocus(
        const QString& appClass,
        const QString& appName,
        const QString& caption,
        const QString& windowId);
    void NotifyDesktopCursorVisible(bool visible);
    void NotifyInputMethodState(bool active, bool bridgeAlive);
    QString RunDiagnostics();
    void Quit();

Q_SIGNALS:
    void StateChanged(
        bool vietnameseEnabled,
        const QString& inputMethod,
        const QString& outputCharset,
        bool safeMode,
        const QString& activeApp);

private Q_SLOTS:
    void onKWinTextInputChanged();
    void onImBridgeServiceUnregistered(const QString& serviceName);
    void onKeyboardHotplug();
    void onXkbMappingChanged();

private:
    void emitStateChangedSignal();
    void showOsdNotification(const QString& text);
    void updateGameModeForFocus(const std::string& appClass, bool isPasswordDialog);
    bool queryKWinTextInputSupported() const;
    void verifyAndEnsureXkbOverlay(bool allowReload, bool notifyOnFailure);

    std::string configPath_;
    bool noGrab_ = false;
    Config config_;
    bool effectiveVietnameseEnabled_ = true;
    uint64_t lastFocusCheckEpoch_ = 0;
    bamboo::SessionProcessor processor_;
    UinputEmitter emitter_;
    EvdevCapture capture_;
    FocusTracker focusTracker_;
    GameModeMonitor gameModeMonitor_;
    QDBusServiceWatcher imBridgeWatcher_;
    bool xkbOverlayVerified_ = true;
    bool xkbFailureNotified_ = false;
    std::string xkbDiagnosticNote_;
};

} // namespace vnsnowkey
