/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "daemon_server.hpp"
#include "../focus/xwayland_env.hpp"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusVariant>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <grp.h>
#include <pwd.h>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace vnsnowkey {

namespace {

DaemonServer* g_activeDaemonInstance = nullptr;

std::string toLowerAscii(std::string_view sv) {
    std::string out(sv);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return out;
}

bool isGenericGameRuntime(std::string_view appClass) {
    if (appClass.empty()) return true;
    std::string low = toLowerAscii(appClass);
    static constexpr const char* kGenericRuntimes[] = {
        "java", "javaw", "glfw", "lwjgl", "wine", "wine64",
        "gamescope", "prismlauncher", "org.prismlauncher", "modrinth",
        "heroic", "lutris", "fabricmc", "knotclient", "bootstraplauncher",
        "neoforge", "forge", "quiltmc", "atlauncher", "multimc",
        "polymc", "sklauncher", "hmcl", "xmcl", "lunarclient", "badlion",
        "openjdk", "org.openjdk", "com.mojang", "flatpak",
    };
    for (const char* rt : kGenericRuntimes) {
        if (low == rt || low.find(rt) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string resolveEffectiveFocusAppClass(const Config& cfg, const WindowFocusInfo& info) {
    if (cfg.findProfileForApp(info.appClass) != nullptr) {
        return info.appClass;
    }
    if (!info.appName.empty() && cfg.findProfileForApp(info.appName) != nullptr) {
        return info.appName;
    }
    if (isGenericGameRuntime(info.appClass) && !info.caption.empty()) {
        std::string lowerCap = toLowerAscii(info.caption);
        for (const auto& prof : cfg.appProfiles) {
            if (prof.mode == AppMode::GameMode && !prof.appClass.empty()) {
                if (lowerCap.find(toLowerAscii(prof.appClass)) != std::string::npos) {
                    return prof.appClass;
                }
            }
        }
    }
    return info.appClass;
}

} // namespace

DaemonServer::DaemonServer(const std::string& configPath, bool noGrab, QObject* parent)
    : QObject(parent),
      configPath_(configPath.empty() ? Config::defaultConfigPath() : configPath),
      noGrab_(noGrab),
      config_(Config::loadFromFile(configPath_)),
      effectiveVietnameseEnabled_(config_.vietnameseEnabled),
      capture_(processor_, emitter_),
      imBridgeWatcher_(this) {
    g_activeDaemonInstance = this;

    imBridgeWatcher_.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(&imBridgeWatcher_, &QDBusServiceWatcher::serviceUnregistered,
            this, &DaemonServer::onImBridgeServiceUnregistered);

    processor_.setOptions(config_.toSessionOptions());
    processor_.macroTable().setMacros(config_.macros);

    capture_.setOnLanguageToggled([this](bool newVie, const std::string& appClassSnapshot) {
        QMetaObject::invokeMethod(this, [this, newVie, appClassSnapshot]() {
            effectiveVietnameseEnabled_ = newVie;
            const std::string activeFocusApp = focusTracker_.currentFocus().appClass;
            const std::string effectiveApp = !appClassSnapshot.empty() ? appClassSnapshot : activeFocusApp;
            const bool hasFocusedApp = !effectiveApp.empty();
            focusTracker_.recordAppLanguageStateForApp(appClassSnapshot, newVie, config_.rememberPerAppLang);
            if (!config_.rememberPerAppLang || !hasFocusedApp) {
                config_.vietnameseEnabled = newVie;
                config_.saveToFile(configPath_);
            }
            emitStateChangedSignal();
            if (config_.osdEnabled) {
                QString msg = newVie
                    ? QString::fromUtf8("Tiếng Việt (VI) — %1").arg(QString::fromStdString(config_.inputMethod))
                    : QString::fromUtf8("Tiếng Anh (EN)");
                showOsdNotification(msg);
            }
        }, Qt::QueuedConnection);
    });

    connect(&xkbWatchdogTimer_, &QTimer::timeout, this, &DaemonServer::onXkbWatchdogTimer);

    capture_.setOnPanicTriggered([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            SetSafeMode(true);
            showOsdNotification(QString::fromUtf8("VNSnowKey: Đã bật Chế độ An toàn (Nhả bàn phím)"));
        }, Qt::QueuedConnection);
    });

    focusTracker_.setOnFocusChanged([this](const WindowFocusInfo& info, bool newVieState, bool langChanged) {
        std::string effectiveApp = resolveEffectiveFocusAppClass(config_, info);
        capture_.notifyFocusChanged(effectiveApp, info.isPasswordDialog);
        if (langChanged && !config_.safeMode) {
            if (capture_.setVietnameseEnabledFromFocus(newVieState, lastFocusCheckEpoch_)) {
                effectiveVietnameseEnabled_ = newVieState;
            }
        }
        updateGameModeForFocus(effectiveApp, info.isPasswordDialog);
        emitStateChangedSignal();
    });

    focusTracker_.setOnXkbMappingChanged([this]() {
        QMetaObject::invokeMethod(this, "onXkbMappingChanged", Qt::QueuedConnection);
    });

    focusTracker_.setOnRawFocusEvent([this](
                                         const std::string& appClass,
                                         const std::string& appName,
                                         const std::string& caption,
                                         const std::string& windowId) {
        QString qCls = QString::fromStdString(appClass);
        QString qName = QString::fromStdString(appName);
        QString qCap = QString::fromStdString(caption);
        QString qId = QString::fromStdString(windowId);
        QMetaObject::invokeMethod(
            this,
            [this, qCls, qName, qCap, qId]() { NotifyWindowFocus(qCls, qName, qCap, qId); },
            Qt::QueuedConnection);
    });

    gameModeMonitor_.setOnStateChanged([this](bool chatActive, GameSignalSource source) {
        capture_.setGameChatActive(chatActive, source != GameSignalSource::None);
    });

    capture_.setOnGameChatKey([this](bool isExitKey) {
        gameModeMonitor_.notifyChatKeystroke(isExitKey);
    });

    capture_.setOnKeyboardHotplug([this]() {
        QMetaObject::invokeMethod(this, "onKeyboardHotplug", Qt::QueuedConnection);
    });
}

namespace {

XkbCompositorHint backendToXkbHint(DesktopBackend be) {
    switch (be) {
        case DesktopBackend::KWin:       return XkbCompositorHint::KWin;
        case DesktopBackend::GnomeShell: return XkbCompositorHint::GnomeShell;
        case DesktopBackend::Hyprland:   return XkbCompositorHint::Hyprland;
        case DesktopBackend::Sway:       return XkbCompositorHint::Sway;
        case DesktopBackend::X11:        return XkbCompositorHint::X11;
        case DesktopBackend::None:
        default:                         return XkbCompositorHint::Auto;
    }
}

OutputMethod pickSafeXkbFallbackMethod(DesktopBackend be) {
    if (be == DesktopBackend::GnomeShell) {
        return OutputMethod::CtrlShiftU;
    }
    return UinputEmitter::hasClipboardBackend() ? OutputMethod::ClipboardCtrlV : OutputMethod::CtrlShiftU;
}

} // namespace

DaemonServer::~DaemonServer() {
    stop();
    if (g_activeDaemonInstance == this) {
        g_activeDaemonInstance = nullptr;
    }
}

bool DaemonServer::start(std::string* errorMsg) {
    const bool isTest = xwayland_env::isIsolatedTestEnv();

    // 1. Install XKB overlay (~/.config/xkb/symbols/us & types/level5)
    std::string xkbErr;
    const bool wasInstalled = isTest ? true : XkbOverlay::isOverlayInstalled();
    if (!isTest) {
        emitter_.xkbOverlay().installOverlayFiles("", &xkbErr);
    }

    // 2. Initialize virtual keyboard (or attach custom test output fd)
    std::string uinputErr;
    if (const char* testOutFdStr = std::getenv("VNSNOWKEY_TEST_OUT_FD")) {
        try {
            int testOutFd = std::stoi(testOutFdStr);
            emitter_.initializeWithFd(testOutFd);
        } catch (...) {}
    } else {
        if (!emitter_.initialize(&uinputErr)) {
            if (errorMsg) *errorMsg = uinputErr;
            // Continue running D-Bus service even if uinput fails so GUI/CLI doctor can report exact diagnostics!
        }
    }

    // 3. Register D-Bus service org.vnsnowkey.Daemon on session bus
    auto bus = QDBusConnection::sessionBus();
    imBridgeWatcher_.setConnection(bus);
    if (!bus.registerService("org.vnsnowkey.Daemon")) {
        if (errorMsg) *errorMsg = "Failed to register D-Bus service org.vnsnowkey.Daemon (is vnsnowkeyd already running?)";
        return false;
    }
    if (!bus.registerObject("/org/vnsnowkey/Daemon", this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
        if (errorMsg) *errorMsg = "Failed to register D-Bus object /org/vnsnowkey/Daemon";
        return false;
    }

    // 4. Start Multi-Desktop Window Focus Tracker (KDE KWin, Hyprland, Sway, GNOME Shell, X11)
    focusTracker_.startDesktopFocusMonitor();
    gameModeMonitor_.setCompositorIsGnome(focusTracker_.activeBackend() == DesktopBackend::GnomeShell);
    if (!isTest) {
        bus.connect(
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/VirtualKeyboard"),
            QStringLiteral("org.kde.kwin.VirtualKeyboard"),
            QStringLiteral("activeClientSupportsTextInputChanged"),
            this,
            SLOT(onKWinTextInputChanged()));
        gameModeMonitor_.start();
    }

    // 5. Verify and apply XKB overlay AFTER virtual keyboard creation & desktop backend detection.
    // On X11 (GNOME Xorg / KDE X11), X server does not read ~/.config/xkb at login, so xkbcomp must run every session.
    const bool wayland = XkbOverlay::isWaylandSession();
    if (!isTest) {
        verifyAndEnsureXkbOverlay(!wasInstalled || !wayland, false);
        if (!wayland) {
            capture_.setExternalCapsLockHint(XkbOverlay::queryX11CapsLockState());
            // Guard against late desktop startup daemons (gsd-keyboard / kded) overwriting X11 keymap right after login.
            QTimer::singleShot(1500, this, [this]() { verifyAndEnsureXkbOverlay(true, false); });
            QTimer::singleShot(4500, this, [this]() { verifyAndEnsureXkbOverlay(true, true); });
        }
        sysSleepConnected_ = QDBusConnection::systemBus().connect(
            QStringLiteral("org.freedesktop.login1"),
            QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"),
            QStringLiteral("PrepareForSleep"),
            this,
            SLOT(onPrepareForSleep(bool)));
        xkbWatchdogTimer_.start(10000);
    }

    // 6. Start evdev capture thread
    focusTracker_.setDefaultLanguageState(config_.vietnameseEnabled);
    capture_.updateConfig(config_);
    if (!noGrab_ && emitter_.isInitialized()) {
        std::vector<std::string> explicitDevs;
        if (const char* testInFdStr = std::getenv("VNSNOWKEY_TEST_IN_FD")) {
            explicitDevs.push_back("fd:" + std::string(testInFdStr));
        } else if (const char* envDevs = std::getenv("VNSNOWKEY_ONLY_DEVICES")) {
            std::stringstream ss(envDevs);
            std::string item;
            while (std::getline(ss, item, ':')) {
                if (!item.empty()) explicitDevs.push_back(item);
            }
        }
        capture_.start(explicitDevs);
    }

    emitStateChangedSignal();
    return true;
}

void DaemonServer::stop() {
    xkbWatchdogTimer_.stop();
    if (sysSleepConnected_) {
        QDBusConnection::systemBus().disconnect(
            QStringLiteral("org.freedesktop.login1"),
            QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"),
            QStringLiteral("PrepareForSleep"),
            this,
            SLOT(onPrepareForSleep(bool)));
        sysSleepConnected_ = false;
    }
    auto bus = QDBusConnection::sessionBus();
    imBridgeWatcher_.setWatchedServices(QStringList{});
    bus.disconnect(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/VirtualKeyboard"),
        QStringLiteral("org.kde.kwin.VirtualKeyboard"),
        QStringLiteral("activeClientSupportsTextInputChanged"),
        this,
        SLOT(onKWinTextInputChanged()));
    gameModeMonitor_.stop();
    focusTracker_.stopDesktopFocusMonitor();
    capture_.stop();
    emitter_.shutdown();
    bus.unregisterObject("/org/vnsnowkey/Daemon");
    bus.unregisterService("org.vnsnowkey.Daemon");
}

bool DaemonServer::Toggle() {
    return SetVietnamese(!effectiveVietnameseEnabled_);
}

bool DaemonServer::SetVietnamese(bool enabled) {
    effectiveVietnameseEnabled_ = enabled;
    bool needSave = false;
    if (enabled && config_.safeMode) {
        config_.safeMode = false;
        capture_.exitSafeMode();
        needSave = true;
    }
    const bool hasFocusedApp = !focusTracker_.currentFocus().appClass.empty();
    focusTracker_.recordAppLanguageState(enabled, config_.rememberPerAppLang);
    capture_.setVietnameseEnabled(enabled);
    if (!config_.rememberPerAppLang || !hasFocusedApp) {
        config_.vietnameseEnabled = enabled;
        needSave = true;
    }
    if (needSave) {
        config_.saveToFile(configPath_);
    }
    emitStateChangedSignal();

    if (config_.osdEnabled) {
        QString msg = enabled
            ? QString::fromUtf8("Tiếng Việt (VI) — %1").arg(QString::fromStdString(config_.inputMethod))
            : QString::fromUtf8("Tiếng Anh (EN)");
        showOsdNotification(msg);
    }
    return effectiveVietnameseEnabled_;
}

bool DaemonServer::SetSafeMode(bool safe) {
    config_.safeMode = safe;
    if (safe) {
        capture_.enterSafeMode();
    } else {
        capture_.exitSafeMode();
    }
    Config effCfg = config_;
    effCfg.vietnameseEnabled = effectiveVietnameseEnabled_;
    capture_.updateConfig(effCfg);
    config_.saveToFile(configPath_);
    emitStateChangedSignal();
    return config_.safeMode;
}

bool DaemonServer::PanicStop() {
    return SetSafeMode(true);
}

bool DaemonServer::ResumeFromPanic() {
    return SetSafeMode(false);
}

bool DaemonServer::SetCharset(const QString& charsetName) {
    return SetOutputCharset(charsetName);
}

bool DaemonServer::ConvertClipboard(const QString& fromCharset, const QString& toCharset) {
    std::string src;
    QDBusMessage getMsg = QDBusMessage::createMethodCall(
        "org.kde.klipper", "/klipper", "org.kde.klipper.klipper", "getClipboardContents");
    QDBusMessage rep = QDBusConnection::sessionBus().call(getMsg, QDBus::Block, 300);
    if (rep.type() == QDBusMessage::ReplyMessage && !rep.arguments().isEmpty()) {
        src = rep.arguments().first().toString().toStdString();
    } else if (!UinputEmitter::readSystemClipboard(src)) {
        return false;
    }
    bool strip = (toCharset == QStringLiteral("ASCII"));
    std::string targetCs = strip ? std::string(bamboo::CHARSET_UNICODE) : toCharset.toStdString();
    std::string converted = bamboo::convertCharset(fromCharset.toStdString(), targetCs, src, strip);
    if (rep.type() == QDBusMessage::ReplyMessage) {
        QDBusMessage setMsg = QDBusMessage::createMethodCall(
            "org.kde.klipper", "/klipper", "org.kde.klipper.klipper", "setClipboardContents");
        setMsg << QString::fromStdString(converted);
        QDBusConnection::sessionBus().call(setMsg, QDBus::Block, 300);
        return true;
    }
    return UinputEmitter::writeSystemClipboard(converted);
}

QString DaemonServer::GetStatus() {
    const auto& st = processor_.stats();
    WindowFocusInfo focus = focusTracker_.currentFocus();
    bool isSafe = config_.safeMode || capture_.isInSafeMode();
    bool isVi = effectiveVietnameseEnabled_ && !isSafe;
    return QStringLiteral(
               "mode=%1\n"
               "input_method=%2\n"
               "charset=%3\n"
               "safe_mode=%4\n"
               "active_app=%5\n"
               "game_mode=%6\n"
               "game_chat=%7\n"
               "game_signal=%8\n"
               "words_typed=%9\n"
               "keys_typed=%10\n")
        .arg(isVi ? QStringLiteral("vi") : QStringLiteral("en"),
             QString::fromStdString(config_.inputMethod),
             QString::fromStdString(config_.outputCharset),
             isSafe ? QStringLiteral("true") : QStringLiteral("false"),
             QString::fromStdString(focus.appClass),
             gameModeMonitor_.isGameModeActive() ? QStringLiteral("true") : QStringLiteral("false"),
             capture_.isGameChatActive() ? QStringLiteral("true") : QStringLiteral("false"),
             QString::fromUtf8(gameSignalSourceToString(gameModeMonitor_.currentSource())),
             QString::number(st.vietnameseTransforms),
             QString::number(st.totalKeysProcessed));
}

bool DaemonServer::ReloadConfig() {
    bool ok = false;
    config_ = Config::loadFromFile(configPath_, &ok);
    effectiveVietnameseEnabled_ = config_.vietnameseEnabled;
    focusTracker_.setDefaultLanguageState(config_.vietnameseEnabled);
    capture_.updateConfig(config_);
    verifyAndEnsureXkbOverlay(false, false);
    WindowFocusInfo focus = focusTracker_.currentFocus();
    updateGameModeForFocus(focus.appClass, focus.isPasswordDialog);
    emitStateChangedSignal();
    return ok;
}

bool DaemonServer::SetInputMethod(const QString& imName) {
    config_.inputMethod = imName.toStdString();
    Config effCfg = config_;
    effCfg.vietnameseEnabled = effectiveVietnameseEnabled_;
    capture_.updateConfig(effCfg);
    config_.saveToFile(configPath_);
    emitStateChangedSignal();
    if (config_.osdEnabled) {
        showOsdNotification(QString::fromUtf8("Kiểu gõ: %1").arg(imName));
    }
    return true;
}

bool DaemonServer::SetOutputCharset(const QString& charsetName) {
    config_.outputCharset = charsetName.toStdString();
    Config effCfg = config_;
    effCfg.vietnameseEnabled = effectiveVietnameseEnabled_;
    capture_.updateConfig(effCfg);
    config_.saveToFile(configPath_);
    emitStateChangedSignal();
    if (config_.osdEnabled) {
        showOsdNotification(QString::fromUtf8("Bảng mã: %1").arg(charsetName));
    }
    return true;
}

bool DaemonServer::ReapplyXkbOverlay() {
    std::string err;
    if (!emitter_.xkbOverlay().installOverlayFiles("", &err)) {
        return false;
    }
    XkbOverlay::reloadCompositorXkb(backendToXkbHint(focusTracker_.activeBackend()));
    verifyAndEnsureXkbOverlay(false, true);
    return xkbOverlayVerified_;
}

void DaemonServer::onKeyboardHotplug() {
    if (xwayland_env::isIsolatedTestEnv()) {
        return;
    }
    if (!XkbOverlay::isWaylandSession()) {
        capture_.setExternalCapsLockHint(XkbOverlay::queryX11CapsLockState());
    }
    // Re-check after Xorg / XWayland / compositor settles on newly plugged keyboard (~250ms)
    QTimer::singleShot(250, this, [this]() { runXkbWatchdogCheck(true); });
}

void DaemonServer::onXkbMappingChanged() {
    if (!xwayland_env::isIsolatedTestEnv()) {
        runXkbWatchdogCheck(true);
    }
}

void DaemonServer::onXkbWatchdogTimer() {
    runXkbWatchdogCheck(false);
}

void DaemonServer::onPrepareForSleep(bool goingToSleep) {
    if (goingToSleep || xwayland_env::isIsolatedTestEnv()) {
        return;
    }
    // Recover hardware modifier state, CapsLock LED sync, and XKB overlay immediately after resume
    capture_.resyncAllModifiersFromKernel();
    if (!XkbOverlay::isWaylandSession()) {
        capture_.setExternalCapsLockHint(XkbOverlay::queryX11CapsLockState());
    }
    QTimer::singleShot(400, this, [this]() { runXkbWatchdogCheck(true); });
    QTimer::singleShot(1800, this, [this]() { runXkbWatchdogCheck(true); });
}

void DaemonServer::runXkbWatchdogCheck(bool forceCheck) {
    if (xwayland_env::isIsolatedTestEnv()) {
        return;
    }
    bool needVerify = forceCheck || !xkbOverlayVerified_ || (capture_.xkbFallbackMethod() >= 0);
    if (!XkbOverlay::isOverlayInstalled()) {
        std::string err;
        if (emitter_.xkbOverlay().installOverlayFiles("", &err)) {
            ++xkbAutoHealCount_;
            needVerify = true;
        }
    }
    const std::string curDisp = XkbOverlay::resolveX11Display();
    if (!curDisp.empty() && curDisp != lastVerifiedX11Display_) {
        needVerify = true;
    }
    if (needVerify) {
        verifyAndEnsureXkbOverlay(true, false);
    }
}

void DaemonServer::verifyAndEnsureXkbOverlay(bool allowReload, bool notifyOnFailure) {
    if (xwayland_env::isIsolatedTestEnv()) {
        xkbOverlayVerified_ = true;
        xkbDiagnosticNote_.clear();
        capture_.setXkbFallbackMethod(-1);
        return;
    }

    if (!XkbOverlay::isOverlayInstalled()) {
        std::string err;
        if (emitter_.xkbOverlay().installOverlayFiles("", &err)) {
            ++xkbAutoHealCount_;
            allowReload = true;
        }
    }

    const DesktopBackend be = focusTracker_.activeBackend();
    const XkbCompositorHint hint = backendToXkbHint(be);
    const bool wayland = XkbOverlay::isWaylandSession();
    const bool filesInstalled = XkbOverlay::isOverlayInstalled();
    const std::string curDisp = XkbOverlay::resolveX11Display();

    std::string layout;
    std::string variant;
    XkbOverlay::detectActiveKeyboardLayout(layout, variant, hint);
    const bool isStandardUs = (layout.empty() || layout == "us") &&
                              (variant.empty() || variant == "basic");

    if (!isStandardUs) {
        xkbOverlayVerified_ = false;
        xkbDiagnosticNote_ = "Layout hiện tại là '" + layout +
                             (variant.empty() ? "" : "(" + variant + ")") +
                             "' (khác 'us' mặc định)";
        capture_.setXkbFallbackMethod(static_cast<int>(pickSafeXkbFallbackMethod(be)));
        if (notifyOnFailure && !xkbFailureNotified_ && config_.osdEnabled &&
            config_.defaultOutputMethod == OutputMethod::XkbKeymap) {
            xkbFailureNotified_ = true;
            showOsdNotification(QString::fromUtf8("VNSnowKey: Layout khác 'us', đang tạm dùng chế độ gõ dự phòng"));
        }
        return;
    }

    std::string missing;
    XkbLiveStatus status = XkbOverlay::probeX11KeymapStatus(&missing);
    if (status == XkbLiveStatus::InactiveOverlayMissing && allowReload) {
        if (wayland) {
            XkbOverlay::reloadCompositorXkb(hint);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            if (XkbOverlay::probeX11KeymapStatus(&missing) == XkbLiveStatus::InactiveOverlayMissing) {
                XkbOverlay::applyX11XkbOverlay();
            }
        } else {
            XkbOverlay::applyX11XkbOverlay();
        }
        missing.clear();
        status = XkbOverlay::probeX11KeymapStatus(&missing);
        if (status == XkbLiveStatus::VerifiedActive) {
            ++xkbAutoHealCount_;
        }
    }

    if (status == XkbLiveStatus::VerifiedActive ||
        (wayland && status == XkbLiveStatus::WaylandUnverifiedNoXDisplay)) {
        xkbOverlayVerified_ = true;
        xkbFailureNotified_ = false;
        xkbDiagnosticNote_.clear();
        lastVerifiedX11Display_ = curDisp;
        capture_.setXkbFallbackMethod(-1);
        return;
    }

    // Native Wayland compositors read ~/.config/xkb directly even if XWayland probe failed or isn't up yet;
    // keep XkbKeymap enabled for native Wayland windows instead of forcing slow/incompatible fallbacks.
    if (wayland && filesInstalled && status != XkbLiveStatus::ToolMissing) {
        xkbOverlayVerified_ = false;
        xkbDiagnosticNote_ = "XWayland thiếu: " + (missing.empty() ? "agrave" : missing) +
                             " (Wayland thuần vẫn giữ XKB)";
        capture_.setXkbFallbackMethod(-1);
        return;
    }

    xkbOverlayVerified_ = false;
    if (status == XkbLiveStatus::ToolMissing) {
        xkbDiagnosticNote_ = missing.empty() ? "Thiếu xkbcomp" : missing;
    } else {
        xkbDiagnosticNote_ = "Keymap đang chạy thiếu: " + (missing.empty() ? "agrave" : missing);
    }
    capture_.setXkbFallbackMethod(static_cast<int>(pickSafeXkbFallbackMethod(be)));
    if (notifyOnFailure && !xkbFailureNotified_ && config_.osdEnabled &&
        config_.defaultOutputMethod == OutputMethod::XkbKeymap) {
        xkbFailureNotified_ = true;
        showOsdNotification(QString::fromUtf8("VNSnowKey: XKB chưa nạp được, tạm bật chế độ dự phòng"));
    }
}

void DaemonServer::NotifyWindowFocus(
    const QString& appClass,
    const QString& appName,
    const QString& caption,
    const QString& windowId) {
    // Sync effectiveVietnameseEnabled_ and epoch with EvdevCapture in case a hotkey toggle just ran.
    lastFocusCheckEpoch_ = capture_.langToggleEpoch();
    effectiveVietnameseEnabled_ = capture_.vietnameseEnabled();
    focusTracker_.onWindowFocusChanged(
        appClass.toStdString(),
        appName.toStdString(),
        caption.toStdString(),
        windowId.toStdString(),
        config_,
        effectiveVietnameseEnabled_);
    // Catch lazy XWayland initialization as soon as user switches into an X11/XWayland window.
    runXkbWatchdogCheck(false);
}

QString DaemonServer::computeViInactiveReason() const {
    if (config_.safeMode || capture_.isInSafeMode()) {
        return QStringLiteral("safe_mode");
    }
    const WindowFocusInfo focus = focusTracker_.currentFocus();
    if (config_.bypassPasswordDialogs && focus.isPasswordDialog) {
        return QStringLiteral("password_dialog");
    }
    const AppProfile* appProf = config_.findProfileForApp(focus.appClass);
    if (appProf && appProf->mode == AppMode::Excluded) {
        return QStringLiteral("app_excluded");
    }
    if (appProf && appProf->mode == AppMode::GameMode && !capture_.isGameChatActive()) {
        return QStringLiteral("game_mode_3d");
    }
    if (!effectiveVietnameseEnabled_) {
        return QStringLiteral("user_or_per_app_en");
    }
    return QStringLiteral("none");
}

void DaemonServer::NotifyDesktopCursorVisible(bool visible) {
    gameModeMonitor_.setDesktopCursorState(visible, true);
}

void DaemonServer::NotifyInputMethodState(bool active, bool bridgeAlive) {
    if (bridgeAlive && calledFromDBus()) {
        const QString sender = message().service();
        if (!sender.isEmpty()) {
            imBridgeWatcher_.setWatchedServices(QStringList{sender});
        }
    } else if (!bridgeAlive) {
        imBridgeWatcher_.setWatchedServices(QStringList{});
    }
    gameModeMonitor_.setWaylandImState(active, bridgeAlive);
}

void DaemonServer::onImBridgeServiceUnregistered(const QString& /*serviceName*/) {
    imBridgeWatcher_.setWatchedServices(QStringList{});
    gameModeMonitor_.setWaylandImState(false, false);
}

QString DaemonServer::GetStatusJson() {
    QJsonObject obj;
    obj["vietnamese_enabled"] = effectiveVietnameseEnabled_ && !config_.safeMode;
    obj["safe_mode"] = config_.safeMode || capture_.isInSafeMode();
    obj["input_method"] = QString::fromStdString(config_.inputMethod);
    obj["output_charset"] = QString::fromStdString(config_.outputCharset);
    obj["output_method"] = QString::fromStdString(Config::outputMethodToString(config_.defaultOutputMethod));

    WindowFocusInfo focus = focusTracker_.currentFocus();
    obj["active_app"] = QString::fromStdString(focus.appClass);
    obj["active_caption"] = QString::fromStdString(focus.caption);
    obj["is_password_dialog"] = focus.isPasswordDialog;
    obj["game_mode_active"] = gameModeMonitor_.isGameModeActive();
    obj["game_chat_active"] = capture_.isGameChatActive();
    obj["game_signal_source"] = QString::fromUtf8(gameSignalSourceToString(gameModeMonitor_.currentSource()));
    obj["wayland_im_bridge_connected"] = gameModeMonitor_.isWaylandImBridgeAlive();

    obj["uinput_initialized"] = emitter_.isInitialized();
    obj["xkb_overlay_installed"] = XkbOverlay::isOverlayInstalled();
    obj["xkb_overlay_active"] = xkbOverlayVerified_;
    obj["xkb_fallback_active"] = (capture_.xkbFallbackMethod() >= 0);
    obj["xkb_auto_heals"] = static_cast<qint64>(xkbAutoHealCount_);
    obj["stuck_mod_recoveries"] = static_cast<qint64>(capture_.stuckModRecoveries());
    obj["syn_drop_recoveries"] = static_cast<qint64>(capture_.synDropRecoveries());
    obj["vi_inactive_reason"] = computeViInactiveReason();
    if (!xkbDiagnosticNote_.empty()) {
        obj["xkb_warning"] = QString::fromStdString(xkbDiagnosticNote_);
    }
    obj["kwin_script_loaded"] = focusTracker_.isKWinScriptLoaded();
    obj["desktop_backend"] = QString::fromUtf8(desktopBackendToString(focusTracker_.activeBackend()));
    obj["desktop_focus_active"] = focusTracker_.isDesktopMonitorActive();

    auto devs = capture_.getMonitoredDevices();
    int kbdGrabbedCount = 0;
    QJsonArray devArr;
    for (const auto& d : devs) {
        if (d.isKeyboard && d.isGrabbed) ++kbdGrabbedCount;
        QJsonObject dobj;
        dobj["path"] = QString::fromStdString(d.path);
        dobj["name"] = QString::fromStdString(d.name);
        dobj["is_keyboard"] = d.isKeyboard;
        dobj["is_mouse"] = d.isMouse;
        dobj["is_grabbed"] = d.isGrabbed;
        devArr.append(dobj);
    }
    obj["grabbed_keyboards_count"] = kbdGrabbedCount;
    obj["monitored_devices"] = devArr;

    QJsonArray recentArr;
    for (const auto& app : focusTracker_.recentAppClasses()) {
        recentArr.append(QString::fromStdString(app));
    }
    obj["recent_apps"] = recentArr;

    QJsonObject statsObj;
    const auto& st = processor_.stats();
    statsObj["total_keys"] = static_cast<qint64>(st.totalKeysProcessed);
    statsObj["vietnamese_transforms"] = static_cast<qint64>(st.vietnameseTransforms);
    statsObj["backtracks"] = static_cast<qint64>(st.backtracksCount);
    statsObj["english_restores"] = static_cast<qint64>(st.englishRestoresCount);
    statsObj["macros_expanded"] = static_cast<qint64>(st.macrosExpandedCount);
    obj["stats"] = statsObj;

    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString DaemonServer::RunDiagnostics() {
    return QString::fromStdString(buildDiagnosticsReport(
        &emitter_, &capture_, &focusTracker_, gameModeMonitor_.isWaylandImBridgeAlive()));
}

void DaemonServer::Quit() {
    stop();
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->quit();
    }
}

void DaemonServer::emitStateChangedSignal() {
    WindowFocusInfo focus = focusTracker_.currentFocus();
    Q_EMIT StateChanged(
        effectiveVietnameseEnabled_ && !config_.safeMode,
        QString::fromStdString(config_.inputMethod),
        QString::fromStdString(config_.outputCharset),
        config_.safeMode || capture_.isInSafeMode(),
        QString::fromStdString(focus.appClass));
}

bool DaemonServer::queryKWinTextInputSupported() const {
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/VirtualKeyboard"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"));
    msg << QStringLiteral("org.kde.kwin.VirtualKeyboard")
        << QStringLiteral("activeClientSupportsTextInput");
    QDBusMessage rep = QDBusConnection::sessionBus().call(msg, QDBus::Block, 50);
    if (rep.type() == QDBusMessage::ReplyMessage && !rep.arguments().isEmpty()) {
        return rep.arguments().first().value<QDBusVariant>().variant().toBool();
    }
    return false;
}

void DaemonServer::updateGameModeForFocus(const std::string& appClass, bool isPasswordDialog) {
    const AppProfile* prof = config_.findProfileForApp(appClass);
    bool isGame = (!isPasswordDialog && prof && prof->mode == AppMode::GameMode);
    if (!isGame) {
        gameModeMonitor_.setGameModeActive(false, appClass);
        capture_.setGameChatActive(false, false);
        return;
    }
    bool textInputSupported = queryKWinTextInputSupported();
    gameModeMonitor_.setKWinTextInputState(textInputSupported, false);
    gameModeMonitor_.setGameModeActive(true, appClass);
}

void DaemonServer::onKWinTextInputChanged() {
    if (!gameModeMonitor_.isGameModeActive()) {
        return;
    }
    bool supported = queryKWinTextInputSupported();
    gameModeMonitor_.setKWinTextInputState(supported, true);
}

void DaemonServer::showOsdNotification(const QString& text) {
    auto bus = QDBusConnection::sessionBus();
    const bool hasPlasmaOsd = (focusTracker_.activeBackend() == DesktopBackend::KWin) ||
                              (bus.interface() && bus.interface()->isServiceRegistered(QStringLiteral("org.kde.plasmashell")));
    if (hasPlasmaOsd) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.plasmashell"),
            QStringLiteral("/org/kde/osdService"),
            QStringLiteral("org.kde.osdService"),
            QStringLiteral("showText"));
        msg << QStringLiteral("input-keyboard") << text;
        QDBusConnection::sessionBus().asyncCall(msg);
        return;
    }

    // Standard Freedesktop Notifications OSD fallback for GNOME / Hyprland / Sway / XFCE
    QDBusMessage notif = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("Notify"));
    notif << QStringLiteral("VNSnowKey")
          << static_cast<uint>(90012)
          << QStringLiteral("input-keyboard")
          << text
          << QString()
          << QStringList()
          << QVariantMap()
          << static_cast<int>(1500);
    QDBusConnection::sessionBus().asyncCall(notif);
}

std::string DaemonServer::buildDiagnosticsReport(
    const UinputEmitter* emitter,
    const EvdevCapture* capture,
    const FocusTracker* focus,
    bool waylandImConnected) {
    std::ostringstream oss;
    oss << "=== CHẨN ĐOÁN HỆ THỐNG VNSNOWKEY (LINUX WAYLAND / X11) ===\n\n";

    // 1. Check /dev/uinput access
    bool uinputWritable = (::access("/dev/uinput", R_OK | W_OK) == 0);
    oss << "[1] Thiết bị ảo /dev/uinput:\n";
    oss << "    - Tồn tại: " << (std::filesystem::exists("/dev/uinput") ? "CÓ" : "KHÔNG (cần modprobe uinput)") << "\n";
    oss << "    - Quyền ghi (W_OK): " << (uinputWritable ? "OK (Có quyền)" : "LỖI (Không có quyền ghi!)") << "\n";
    if (emitter) {
        oss << "    - Virtual Keyboard đang chạy: " << (emitter->isInitialized() ? "OK" : "CHƯA KHỞI TẠO") << "\n";
    }

    // 2. Check /dev/input/event* access
    int readableEvents = 0;
    int totalEvents = 0;
    try {
        for (const auto& entry : std::filesystem::directory_iterator("/dev/input")) {
            std::string fn = entry.path().filename().string();
            if (fn.rfind("event", 0) == 0) {
                ++totalEvents;
                if (::access(entry.path().c_str(), R_OK) == 0) {
                    ++readableEvents;
                }
            }
        }
    } catch (...) {}
    oss << "\n[2] Thiết bị đầu vào /dev/input/event*:\n";
    oss << "    - Số node đọc được: " << readableEvents << " / " << totalEvents << "\n";
    if (capture) {
        auto devs = capture->getMonitoredDevices();
        int kbds = 0, mice = 0;
        for (const auto& d : devs) {
            if (d.isKeyboard && d.isGrabbed) ++kbds;
            if (d.isMouse) ++mice;
        }
        oss << "    - Bàn phím đang chiếm quyền (EVIOCGRAB): " << kbds << "\n";
        oss << "    - Thiết bị chuột theo dõi click: " << mice << "\n";
        oss << "    - Tự phục hồi modifier kẹt (EVIOCGKEY): " << capture->stuckModRecoveries() << "\n";
        for (const auto& d : devs) {
            oss << "      * " << d.path << " [" << d.name << "] "
                << (d.isKeyboard ? "(Bàn phím" + std::string(d.isGrabbed ? " - ĐÃ GRAB)" : " - CHƯA GRAB)") : "(Chuột)") << "\n";
        }
    }

    // 3. Check user group membership
    oss << "\n[3] Quyền người dùng & Nhóm (Groups):\n";
    int ngroups = 32;
    std::vector<gid_t> groups(static_cast<size_t>(ngroups));
    struct passwd* pw = ::getpwuid(::getuid());
    bool inInputGroup = false;
    if (pw && ::getgrouplist(pw->pw_name, pw->pw_gid, groups.data(), &ngroups) >= 0) {
        oss << "    - User: " << pw->pw_name << " (uid=" << ::getuid() << ")\n";
        oss << "    - Groups: ";
        for (int i = 0; i < ngroups; ++i) {
            struct group* gr = ::getgrgid(groups[static_cast<size_t>(i)]);
            if (gr && gr->gr_name) {
                std::string gname(gr->gr_name);
                if (gname == "input" || gname == "uinput") inInputGroup = true;
                oss << gname << " ";
            }
        }
        oss << "\n";
    }
    oss << "    - Thuộc nhóm 'input': " << (inInputGroup ? "OK" : "CHƯA (chạy: sudo usermod -aG input $USER)") << "\n";

    // 4. Wayland & XKB Overlay & KWin Script
    const char* xdgSession = std::getenv("XDG_SESSION_TYPE");
    const char* waylandDisp = std::getenv("WAYLAND_DISPLAY");
    oss << "\n[4] Môi trường Desktop Wayland/X11 & XKB Overlay:\n";
    oss << "    - XDG_SESSION_TYPE: " << (xdgSession ? xdgSession : "(unknown)") << "\n";
    oss << "    - WAYLAND_DISPLAY: " << (waylandDisp ? waylandDisp : "(unknown)") << "\n";
    oss << "    - XKB Virtual Keymap (~/.config/xkb/symbols/us): "
        << (XkbOverlay::isOverlayInstalled() ? "OK (Đã cài đặt 134 ký tự tiếng Việt)" : "CHƯA CÀI ĐẶT") << "\n";
    {
        const XkbCompositorHint hint = backendToXkbHint(focus ? focus->activeBackend() : DesktopBackend::None);
        std::string layout;
        std::string variant;
        XkbOverlay::detectActiveKeyboardLayout(layout, variant, hint);
        oss << "    - Keyboard Layout đang hoạt động: "
            << (layout.empty() ? "(unknown)" : layout)
            << (variant.empty() ? "" : "(" + variant + ")") << "\n";
        std::string missing;
        XkbLiveStatus xkbStatus = XkbOverlay::probeX11KeymapStatus(&missing);
        oss << "    - Trạng thái XKB Keymap thực tế: ";
        switch (xkbStatus) {
            case XkbLiveStatus::VerifiedActive:
                oss << "OK (Đã xác nhận 'agrave', <I178>, <I184>, EIGHT_LEVEL_ALPHABETIC trong keymap sống)\n";
                break;
            case XkbLiveStatus::WaylandUnverifiedNoXDisplay:
                oss << "OK (Wayland thuần; sẽ tự xác minh thêm nếu có XWayland)\n";
                break;
            case XkbLiveStatus::ToolMissing:
                oss << "CHÚ Ý (" << missing << ")\n";
                break;
            case XkbLiveStatus::InactiveOverlayMissing:
                oss << "LỖI (Keymap hiện tại chưa có: " << (missing.empty() ? "agrave" : missing)
                    << " — chạy 'vnsnowkey ctl reapply-xkb' hoặc đăng xuất/đăng nhập lại)\n";
                break;
        }
    }
    if (focus) {
        DesktopBackend be = focus->activeBackend();
        oss << "    - Desktop Focus Backend: " << desktopBackendToString(be);
        if (be == DesktopBackend::GnomeShell && !focus->isGnomeExtensionActive()) {
            oss << " (XWayland fallback — extension vnsnowkey-focus@vnsnowkey.org chưa bật)\n";
        } else {
            oss << (focus->isDesktopMonitorActive() ? " (OK - Đang hoạt động)" : " (Chưa kích hoạt)") << "\n";
        }
    }

    const DesktopBackend activeBe = focus ? focus->activeBackend() : DesktopBackend::None;
    if (activeBe == DesktopBackend::KWin || activeBe == DesktopBackend::None) {
        bool kwinImSelected = false;
        {
            const char* cfgHome = std::getenv("XDG_CONFIG_HOME");
            std::filesystem::path kwinrcPath = (cfgHome && cfgHome[0])
                ? std::filesystem::path(cfgHome) / "kwinrc"
                : std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "") / ".config/kwinrc";
            std::ifstream kf(kwinrcPath);
            std::string line;
            while (std::getline(kf, line)) {
                if (line.find("org.vnsnowkey.InputMethod.desktop") != std::string::npos) {
                    kwinImSelected = true;
                    break;
                }
            }
        }
        oss << "    - Wayland Virtual Keyboard Bridge (vnsnowkey-im): "
            << (waylandImConnected
                    ? "OK (Compositor đang kết nối zwp_input_method_v1)"
                    : (kwinImSelected ? "ĐÃ CHỌN TRONG KWIN (chờ kết nối/đăng nhập lại)"
                                      : "CHƯA BẬT (KDE: System Settings -> Keyboard -> Virtual Keyboard -> VNSnowKey)"))
            << "\n";
    } else if (waylandImConnected) {
        oss << "    - Wayland Input Method Bridge (vnsnowkey-im): OK (Đang kết nối zwp_input_method_v1)\n";
    }

    if (!uinputWritable || readableEvents == 0) {
        oss << "\n[!] HƯỚNG DẪN KHẮC PHỤC LỖI QUYỀN TRUY CẬP:\n"
            << "    1. Nạp udev rule & kích hoạt quyền uaccess / nhóm input:\n"
            << "       sudo modprobe uinput\n"
            << "       sudo cp /usr/lib/udev/rules.d/70-vnsnowkey-uinput.rules /etc/udev/rules.d/ 2>/dev/null || true\n"
            << "       sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=misc --subsystem-match=input\n"
            << "    2. Thêm user vào nhóm input:\n"
            << "       sudo usermod -aG input $USER\n"
            << "    3. Đăng xuất và đăng nhập lại (hoặc reboot nếu vẫn chưa nhận nhóm input).\n";
    }

    return oss.str();
}

} // namespace vnsnowkey
