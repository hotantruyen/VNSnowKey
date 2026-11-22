/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tray_controller.hpp"
#include "settings_window.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusReply>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QPainter>
#include <QPalette>
#include <QStyleHints>
#include <QSvgRenderer>

namespace vnsnowkey {

TrayController::TrayController(SettingsWindow* settingsWin, QObject* parent)
    : QObject(parent), settingsWin_(settingsWin) {
#ifdef VNSNOWKEY_HAVE_KF6_SNI
    sni_ = new KStatusNotifierItem(QStringLiteral("org.vnsnowkey.StatusNotifierItem"), this);
    sni_->setCategory(KStatusNotifierItem::SystemServices);
    sni_->setStatus(KStatusNotifierItem::Active);
    sni_->setTitle(QStringLiteral("VNSnowKey - Bộ gõ tiếng Việt"));
    sni_->setStandardActionsEnabled(false);
#else
    trayMenu_ = new QMenu();
    sniBackend_ = new SniTrayBackend(trayMenu_, this);
    trayIcon_ = new QSystemTrayIcon(this);
    trayIcon_->setContextMenu(trayMenu_);
    trayIcon_->setToolTip(QStringLiteral("VNSnowKey — Bộ gõ Tiếng Việt"));
#endif

    dbus_ = new QDBusInterface(
        QStringLiteral("org.vnsnowkey.Daemon"),
        QStringLiteral("/org/vnsnowkey/Daemon"),
        QStringLiteral("org.vnsnowkey.Daemon"),
        QDBusConnection::sessionBus(),
        this);

    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.vnsnowkey.Daemon"),
        QStringLiteral("/org/vnsnowkey/Daemon"),
        QStringLiteral("org.vnsnowkey.Daemon"),
        QStringLiteral("StateChanged"),
        this,
        SLOT(onStateChanged(bool, QString, QString, bool, QString)));

#ifdef VNSNOWKEY_HAVE_KF6_SNI
    connect(sni_, &KStatusNotifierItem::activateRequested,
            this, &TrayController::onActivateRequested);
#else
    connect(sniBackend_, &SniTrayBackend::activateRequested,
            this, &TrayController::onToggleLanguage);
    connect(sniBackend_, &SniTrayBackend::hostAvailabilityChanged, this, [this](bool active) {
        if (!trayIcon_) return;
        if (active) {
            trayIcon_->hide();
        } else {
            trayIcon_->show();
        }
    });
    connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            onToggleLanguage();
        }
    });
#endif

    if (qApp) {
        qApp->installEventFilter(this);
    }

    setupMenu();
    refreshFromDaemon();
#ifndef VNSNOWKEY_HAVE_KF6_SNI
    sniBackend_->notifyMenuUpdated();
    if (!sniBackend_->isHostActive()) {
        trayIcon_->show();
    }
#endif

    pollTimer_ = new QTimer(this);
    connect(pollTimer_, &QTimer::timeout, this, &TrayController::refreshFromDaemon);
    pollTimer_->start(2500);
}

TrayController::~TrayController() {
#ifndef VNSNOWKEY_HAVE_KF6_SNI
    delete sniBackend_;
    sniBackend_ = nullptr;
    delete trayMenu_;
    trayMenu_ = nullptr;
#endif
}

QString TrayController::locateIconAsset(const QString& filename) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/../assets/icons/") + filename,
        appDir + QStringLiteral("/assets/icons/") + filename,
        QDir::currentPath() + QStringLiteral("/assets/icons/") + filename,
        QDir::homePath() + QStringLiteral("/.local/share/icons/hicolor/scalable/status/") + filename,
        QStringLiteral("/usr/share/vnsnowkey/icons/") + filename,
        QStringLiteral("/usr/share/icons/hicolor/scalable/status/") + filename
    };
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

QIcon TrayController::buildAdaptiveMonochromeIcon(bool isVietnamese, bool forceDarkTheme, bool useSystemTheme) {
    bool isDark = forceDarkTheme;
    if (useSystemTheme && qApp) {
        const QByteArray desktop = qgetenv("XDG_CURRENT_DESKTOP").toLower();
        if (desktop.contains("gnome")) {
            // GNOME Shell top panel is dark regardless of application light/dark theme.
            isDark = true;
        } else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
            auto cs = QGuiApplication::styleHints()->colorScheme();
            if (cs == Qt::ColorScheme::Dark) {
                isDark = true;
            } else if (cs == Qt::ColorScheme::Light) {
                isDark = false;
            } else {
                const QColor winBg = qApp->palette().color(QPalette::Window);
                isDark = (winBg.lightness() < 128);
            }
#else
            const QColor winBg = qApp->palette().color(QPalette::Window);
            isDark = (winBg.lightness() < 128);
#endif
        }
    }

    const QString variantName = isVietnamese
        ? (isDark ? QStringLiteral("vnsnowkey-vi-dark.svg") : QStringLiteral("vnsnowkey-vi-light.svg"))
        : (isDark ? QStringLiteral("vnsnowkey-en-dark.svg") : QStringLiteral("vnsnowkey-en-light.svg"));

    const QString svgPath = locateIconAsset(variantName);
    QIcon icon;
    const int sizes[] = {16, 22, 24, 32, 48};

    if (!svgPath.isEmpty()) {
        QSvgRenderer renderer(svgPath);
        if (renderer.isValid()) {
            for (int sz : sizes) {
                QPixmap px(sz, sz);
                px.fill(Qt::transparent);
                QPainter p(&px);
                p.setRenderHint(QPainter::Antialiasing, true);
                renderer.render(&p, QRectF(0, 0, sz, sz));
                p.end();
                icon.addPixmap(px);
            }
            return icon;
        }
    }

    // Programmatic vector fallback if SVG file is not found
    const QColor fg = isDark ? QColor(242, 242, 247) : QColor(35, 38, 41);
    for (int sz : sizes) {
        QPixmap px(sz, sz);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(fg, std::max(1.5, sz * 0.08));
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        qreal margin = sz * 0.1;
        qreal radius = sz * 0.16;
        p.drawRoundedRect(QRectF(margin, margin, sz - 2 * margin, sz - 2 * margin), radius, radius);

        QFont font = p.font();
        font.setBold(true);
        font.setPixelSize( static_cast<int>(sz * 0.56));
        p.setFont(font);
        p.drawText(QRectF(0, 0, sz, sz), Qt::AlignCenter, isVietnamese ? QStringLiteral("V") : QStringLiteral("E"));
        p.end();
        icon.addPixmap(px);
    }
    return icon;
}

void TrayController::setupMenu() {
#ifdef VNSNOWKEY_HAVE_KF6_SNI
    QMenu* menu = sni_->contextMenu();
    if (!menu) {
        menu = new QMenu();
        sni_->setContextMenu(menu);
    }
#else
    QMenu* menu = trayMenu_;
    if (!menu) {
        menu = new QMenu();
        trayMenu_ = menu;
        if (trayIcon_) trayIcon_->setContextMenu(menu);
    }
#endif
    menu->clear();

    statusHeaderAction_ = menu->addAction(QStringLiteral("VNSnowKey: Tiếng Việt (VI)"));
    statusHeaderAction_->setEnabled(false);

    toggleLangAction_ = menu->addAction(QStringLiteral("Chuyển chế độ Tiếng Việt / Tiếng Anh (VI ↔ EN)"));
    connect(toggleLangAction_, &QAction::triggered, this, &TrayController::onToggleLanguage);

    menu->addSeparator();

    // Input Method submenu
    QMenu* imMenu = menu->addMenu(QStringLiteral("Kiểu gõ (Input Method)"));
    imGroup_ = new QActionGroup(this);
    imGroup_->setExclusive(true);
    const QStringList imList = {
        QStringLiteral("Telex"),
        QStringLiteral("Simple Telex"),
        QStringLiteral("VNI"),
        QStringLiteral("VIQR"),
        QStringLiteral("Telex 2"),
        QStringLiteral("Telex W"),
        QStringLiteral("Telex + VNI"),
        QStringLiteral("Telex + VNI + VIQR"),
        QStringLiteral("Microsoft layout"),
        QStringLiteral("VNI Bàn phím tiếng Pháp")
    };
    for (const QString& im : imList) {
        QAction* act = imMenu->addAction(im);
        act->setCheckable(true);
        act->setData(im);
        if (im == currentInputMethod_) act->setChecked(true);
        imGroup_->addAction(act);
    }
    connect(imGroup_, &QActionGroup::triggered, this, &TrayController::onSelectInputMethod);

    // Charset submenu
    QMenu* csMenu = menu->addMenu(QStringLiteral("Bảng mã (Charset)"));
    charsetGroup_ = new QActionGroup(this);
    charsetGroup_->setExclusive(true);
    const QStringList csList = {
        QStringLiteral("Unicode"),
        QStringLiteral("TCVN3 (ABC)"),
        QStringLiteral("VNI Windows"),
        QStringLiteral("Unicode tổ hợp"),
        QStringLiteral("VIQR"),
        QStringLiteral("Windows 1258 codepage")
    };
    for (const QString& cs : csList) {
        QAction* act = csMenu->addAction(cs);
        act->setCheckable(true);
        act->setData(cs);
        if (cs == currentCharset_) act->setChecked(true);
        charsetGroup_->addAction(act);
    }
    connect(charsetGroup_, &QActionGroup::triggered, this, &TrayController::onSelectCharset);

    QAction* clipConvAct = menu->addAction(QStringLiteral("Chuyển mã Clipboard (TCVN3 → Unicode)"));
    connect(clipConvAct, &QAction::triggered, this, &TrayController::onConvertClipboardAction);

    menu->addSeparator();

    QAction* openSettingsAct = menu->addAction(QStringLiteral("Cài đặt VNSnowKey..."));
    connect(openSettingsAct, &QAction::triggered, this, &TrayController::onOpenSettings);

    safeModeAction_ = menu->addAction(QStringLiteral("Chế độ an toàn / Tạm dừng (Safe Mode)"));
    safeModeAction_->setCheckable(true);
    connect(safeModeAction_, &QAction::toggled, this, &TrayController::onToggleSafeMode);

    menu->addSeparator();

    QAction* quitAct = menu->addAction(QStringLiteral("Thoát"));
    connect(quitAct, &QAction::triggered, this, &TrayController::onQuitApplication);
}

void TrayController::refreshFromDaemon() {
    if (!dbus_ || !dbus_->isValid()) {
        delete dbus_;
        dbus_ = new QDBusInterface(
            QStringLiteral("org.vnsnowkey.Daemon"),
            QStringLiteral("/org/vnsnowkey/Daemon"),
            QStringLiteral("org.vnsnowkey.Daemon"),
            QDBusConnection::sessionBus(),
            this);
    }

    QDBusReply<QString> reply = dbus_->call(QStringLiteral("GetStatus"));
    if (!reply.isValid()) {
        daemonOnline_ = false;
        updateVisuals();
        return;
    }

    daemonOnline_ = true;
    const QString statusStr = reply.value();
    const QStringList lines = statusStr.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        QString k = line.left(eq).trimmed();
        QString v = line.mid(eq + 1).trimmed();
        if (k == QStringLiteral("mode")) currentMode_ = v;
        else if (k == QStringLiteral("input_method")) currentInputMethod_ = v;
        else if (k == QStringLiteral("charset")) currentCharset_ = v;
        else if (k == QStringLiteral("safe_mode")) safeMode_ = (v == QStringLiteral("true"));
    }
    updateVisuals();
}

void TrayController::onStateChanged(
    bool vietnameseEnabled,
    const QString& inputMethod,
    const QString& outputCharset,
    bool safeMode,
    const QString& /*activeApp*/) {
    daemonOnline_ = true;
    currentMode_ = vietnameseEnabled ? QStringLiteral("vi") : QStringLiteral("en");
    currentInputMethod_ = inputMethod;
    currentCharset_ = outputCharset;
    safeMode_ = safeMode;
    updateVisuals();
    if (settingsWin_) {
        settingsWin_->updateDaemonLiveStatus(currentMode_, currentInputMethod_, safeMode_);
    }
}

void TrayController::updateVisuals() {
    const bool isVi = (currentMode_ == QStringLiteral("vi")) && !safeMode_ && daemonOnline_;
    const QIcon icon = buildAdaptiveMonochromeIcon(isVi);

    // Provide both symbolic icon name (when installed into system hicolor theme) and crisp multi-res pixmap
#ifdef VNSNOWKEY_HAVE_KF6_SNI
    sni_->setIconByPixmap(icon);
#else
    if (sniBackend_) {
        sniBackend_->setIcon(icon);
    }
    if (trayIcon_) {
        trayIcon_->setIcon(icon);
    }
#endif

    QString modeLabel;
    if (!daemonOnline_) {
        modeLabel = QStringLiteral("Daemon chưa chạy (Offline)");
    } else if (safeMode_) {
        modeLabel = QStringLiteral("Tạm dừng / Safe Mode (EN)");
    } else if (isVi) {
        modeLabel = QStringLiteral("Tiếng Việt (VI)");
    } else {
        modeLabel = QStringLiteral("Tiếng Anh (EN)");
    }

    if (statusHeaderAction_) {
        statusHeaderAction_->setText(QStringLiteral("VNSnowKey: %1 — %2").arg(modeLabel, currentInputMethod_));
    }
    if (safeModeAction_) {
        safeModeAction_->blockSignals(true);
        safeModeAction_->setChecked(safeMode_);
        safeModeAction_->blockSignals(false);
    }
    if (imGroup_) {
        for (QAction* act : imGroup_->actions()) {
            if (act->data().toString() == currentInputMethod_) {
                act->setChecked(true);
            }
        }
    }
    if (charsetGroup_) {
        for (QAction* act : charsetGroup_->actions()) {
            if (act->data().toString() == currentCharset_) {
                act->setChecked(true);
            }
        }
    }

    const QString subTitle = QStringLiteral("Chế độ: %1 | Kiểu gõ: %2 | Bảng mã: %3\nClick trái để chuyển ngôn ngữ VI/EN")
                                 .arg(modeLabel, currentInputMethod_, currentCharset_);
#ifdef VNSNOWKEY_HAVE_KF6_SNI
    sni_->setToolTip(icon, QStringLiteral("VNSnowKey — Bộ gõ Tiếng Việt"), subTitle);
#else
    if (sniBackend_) {
        sniBackend_->setToolTip(QStringLiteral("VNSnowKey — Bộ gõ Tiếng Việt"), subTitle);
        sniBackend_->notifyMenuUpdated();
    }
    if (trayIcon_) {
        trayIcon_->setToolTip(QStringLiteral("VNSnowKey — Bộ gõ Tiếng Việt\n") + subTitle);
    }
#endif
}

void TrayController::onActivateRequested(bool /*active*/, const QPoint& /*pos*/) {
    onToggleLanguage();
}

void TrayController::onToggleLanguage() {
    if (dbus_ && dbus_->isValid()) {
        dbus_->call(QStringLiteral("Toggle"));
        refreshFromDaemon();
    }
}

void TrayController::onSelectInputMethod(QAction* action) {
    if (!action) return;
    const QString im = action->data().toString();
    if (dbus_ && dbus_->isValid()) {
        dbus_->call(QStringLiteral("SetInputMethod"), im);
        refreshFromDaemon();
    }
}

void TrayController::onSelectCharset(QAction* action) {
    if (!action) return;
    const QString cs = action->data().toString();
    if (dbus_ && dbus_->isValid()) {
        dbus_->call(QStringLiteral("SetCharset"), cs);
        refreshFromDaemon();
    }
}

void TrayController::onToggleSafeMode(bool checked) {
    if (dbus_ && dbus_->isValid()) {
        if (checked) {
            dbus_->call(QStringLiteral("PanicStop"));
        } else {
            dbus_->call(QStringLiteral("ResumeFromPanic"));
        }
        refreshFromDaemon();
    }
}

void TrayController::onConvertClipboardAction() {
    if (dbus_ && dbus_->isValid()) {
        dbus_->call(QStringLiteral("ConvertClipboard"), QStringLiteral("TCVN3 (ABC)"), QStringLiteral("Unicode"));
    }
}

void TrayController::onOpenSettings() {
    if (settingsWin_) {
        settingsWin_->showAndRaise();
    }
}

void TrayController::onQuitApplication() {
    QCoreApplication::quit();
}

bool TrayController::eventFilter(QObject* watched, QEvent* event) {
    if (event && event->type() == QEvent::ApplicationPaletteChange) {
        updateVisuals();
    }
    return QObject::eventFilter(watched, event);
}

} // namespace vnsnowkey
