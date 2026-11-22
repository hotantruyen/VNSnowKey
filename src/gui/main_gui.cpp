/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "settings_window.hpp"
#include "tray_controller.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLockFile>
#include <QStandardPaths>
#include <QThread>
#include <iostream>

namespace {

QIcon loadAppWindowIcon() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/../assets/icons/vnsnowkey.png"),
        appDir + QStringLiteral("/assets/icons/vnsnowkey.png"),
        QDir::currentPath() + QStringLiteral("/assets/icons/vnsnowkey.png"),
        QStringLiteral("/usr/share/vnsnowkey/icons/vnsnowkey.png"),
        QStringLiteral("/usr/share/icons/hicolor/256x256/apps/vnsnowkey.png"),
        appDir + QStringLiteral("/../assets/icons/vnsnowkey.svg"),
        QStringLiteral("/usr/share/icons/hicolor/scalable/apps/vnsnowkey.svg"),
    };
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            QIcon ic(path);
            if (!ic.isNull()) return ic;
        }
    }
    return QIcon::fromTheme(QStringLiteral("vnsnowkey"));
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("vnsnowkey-gui"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.6"));
    QApplication::setOrganizationName(QStringLiteral("VNSnowKey"));
    QApplication::setDesktopFileName(QStringLiteral("org.vnsnowkey.App"));
    QApplication::setWindowIcon(loadAppWindowIcon());
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VNSnowKey Qt6 Settings GUI & System Tray"));
    parser.addHelpOption();
    QCommandLineOption trayOnlyOpt(QStringLiteral("tray"), QStringLiteral("Start minimized in system tray"));
    QCommandLineOption settingsOpt(QStringLiteral("settings"), QStringLiteral("Open settings window immediately"));
    QCommandLineOption testIconsOpt(QStringLiteral("test-icons"), QStringLiteral("Self-test V/E monochrome icons at 16/22/24/32px for dark/light themes"));
    parser.addOption(trayOnlyOpt);
    parser.addOption(settingsOpt);
    parser.addOption(testIconsOpt);
    parser.process(app);

    if (parser.isSet(testIconsOpt)) {
        const int sizes[] = {16, 22, 24, 32};
        for (bool isVi : {true, false}) {
            for (bool isDark : {true, false}) {
                QIcon icon = vnsnowkey::TrayController::buildAdaptiveMonochromeIcon(isVi, isDark, false);
                if (icon.isNull()) {
                    std::cerr << "[FAIL] Icon is null for isVi=" << isVi << " isDark=" << isDark << "\n";
                    return 1;
                }
                for (int sz : sizes) {
                    QPixmap px = icon.pixmap(sz, sz);
                    if (px.width() != sz || px.height() != sz) {
                        std::cerr << "[FAIL] Unexpected pixmap size for " << sz << "px\n";
                        return 1;
                    }
                    QImage img = px.toImage();
                    int nonTransparent = 0;
                    for (int y = 0; y < img.height(); ++y) {
                        for (int x = 0; x < img.width(); ++x) {
                            QColor c = img.pixelColor(x, y);
                            if (c.alpha() > 64) {
                                ++nonTransparent;
                                // Verify monochrome color matches dark (light foreground) vs light (dark foreground)
                                if (isDark && c.lightness() < 150) {
                                    std::cerr << "[FAIL] Dark-theme icon pixel too dark at " << sz << "px\n";
                                    return 1;
                                }
                                if (!isDark && c.lightness() > 120) {
                                    std::cerr << "[FAIL] Light-theme icon pixel too bright at " << sz << "px\n";
                                    return 1;
                                }
                            }
                        }
                    }
                    if (nonTransparent < (sz * sz) / 10) {
                        std::cerr << "[FAIL] Too few rendered pixels at " << sz << "px\n";
                        return 1;
                    }
                }
            }
        }
        QIcon appIcon = loadAppWindowIcon();
        if (appIcon.isNull() || appIcon.pixmap(64, 64).isNull()) {
            std::cerr << "[FAIL] App icon vnsnowkey.png/vnsnowkey.svg failed to load.\n";
            return 1;
        }
        std::cout << "[PASS] All V/E monochrome icons and VNSnowKey app icon verified.\n";
        return 0;
    }

    // Enforce single-instance per user D-Bus session using QLockFile + org.vnsnowkey.Gui D-Bus service.
    // A second launch activates the existing instance's Settings window (unless --tray) and exits 0
    // so only ONE StatusNotifierItem tray icon is ever created.
    QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtimeDir.isEmpty()) {
        runtimeDir = QDir::tempPath();
    }
    QDir().mkpath(runtimeDir);
    QLockFile instanceLock(runtimeDir + QStringLiteral("/vnsnowkey-gui.lock"));
    instanceLock.setStaleLockTime(0);

    QDBusConnection bus = QDBusConnection::sessionBus();
    const bool lockAcquired = instanceLock.tryLock(100);
    const bool busConnected = bus.isConnected();
    const bool serviceRegistered = !busConnected || bus.registerService(QStringLiteral("org.vnsnowkey.Gui"));

    if (!lockAcquired || !serviceRegistered) {
        const bool shouldActivate = !parser.isSet(trayOnlyOpt) || parser.isSet(settingsOpt);
        if (shouldActivate && busConnected) {
            for (int retry = 0; retry < 10; ++retry) {
                QDBusMessage call = QDBusMessage::createMethodCall(
                    QStringLiteral("org.vnsnowkey.Gui"),
                    QStringLiteral("/org/vnsnowkey/Gui"),
                    QStringLiteral("org.vnsnowkey.Gui"),
                    QStringLiteral("ActivateSettings"));
                QDBusMessage reply = bus.call(call, QDBus::Block, 1000);
                if (reply.type() != QDBusMessage::ErrorMessage) {
                    break;
                }
                QThread::msleep(50);
            }
        }
        return 0;
    }

    vnsnowkey::SettingsWindow settingsWin;
    if (bus.isConnected()) {
        bus.registerObject(QStringLiteral("/org/vnsnowkey/Gui"),
                           QStringLiteral("org.vnsnowkey.Gui"),
                           &settingsWin,
                           QDBusConnection::ExportScriptableSlots);
    }

    vnsnowkey::TrayController tray(&settingsWin);

    if (!parser.isSet(trayOnlyOpt) || parser.isSet(settingsOpt)) {
        settingsWin.showAndRaise();
    }

    return app.exec();
}
