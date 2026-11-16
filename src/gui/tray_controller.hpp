/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#ifdef VNSNOWKEY_HAVE_KF6_SNI
#include <KStatusNotifierItem>
#else
#include "sni_tray.hpp"
#include <QSystemTrayIcon>
#endif
#include <QAction>
#include <QActionGroup>
#include <QDBusInterface>
#include <QMenu>
#include <QObject>
#include <QString>
#include <QTimer>

namespace vnsnowkey {

class SettingsWindow;

class TrayController : public QObject {
    Q_OBJECT

public:
    explicit TrayController(SettingsWindow* settingsWin, QObject* parent = nullptr);
    ~TrayController() override;

    void refreshFromDaemon();
    static QIcon buildAdaptiveMonochromeIcon(bool isVietnamese, bool forceDarkTheme = false, bool useSystemTheme = true);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

public slots:
    void onStateChanged(bool vietnameseEnabled, const QString& inputMethod, const QString& outputCharset, bool safeMode, const QString& activeApp);
    void onActivateRequested(bool active, const QPoint& pos);
    void onToggleLanguage();
    void onSelectInputMethod(QAction* action);
    void onSelectCharset(QAction* action);
    void onToggleSafeMode(bool checked);
    void onConvertClipboardAction();
    void onOpenSettings();
    void onQuitApplication();

private:
    void setupMenu();
    void updateVisuals();
    static QString locateIconAsset(const QString& filename);

    SettingsWindow* settingsWin_ = nullptr;
#ifdef VNSNOWKEY_HAVE_KF6_SNI
    KStatusNotifierItem* sni_ = nullptr;
#else
    QMenu* trayMenu_ = nullptr;
    SniTrayBackend* sniBackend_ = nullptr;
    QSystemTrayIcon* trayIcon_ = nullptr;
#endif
    QDBusInterface* dbus_ = nullptr;
    QTimer* pollTimer_ = nullptr;

    QString currentMode_ = QStringLiteral("vi");
    QString currentInputMethod_ = QStringLiteral("Telex");
    QString currentCharset_ = QStringLiteral("Unicode");
    bool safeMode_ = false;
    bool daemonOnline_ = false;

    QAction* statusHeaderAction_ = nullptr;
    QAction* toggleLangAction_ = nullptr;
    QAction* safeModeAction_ = nullptr;
    QActionGroup* imGroup_ = nullptr;
    QActionGroup* charsetGroup_ = nullptr;
};

} // namespace vnsnowkey
