/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QIcon>
#include <QList>
#include <QMenu>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace vnsnowkey {

struct SniPixmap {
    qint32 width = 0;
    qint32 height = 0;
    QByteArray data;
};
using SniPixmapList = QList<SniPixmap>;

struct SniToolTip {
    QString iconName;
    SniPixmapList iconPixmap;
    QString title;
    QString description;
};

struct DbusMenuItem {
    qint32 id = 0;
    QVariantMap properties;
    QList<QDBusVariant> children;
};

struct DbusMenuPropsItem {
    qint32 id = 0;
    QVariantMap properties;
};
using DbusMenuPropsList = QList<DbusMenuPropsItem>;

struct DbusMenuRemovedItem {
    qint32 id = 0;
    QStringList properties;
};
using DbusMenuRemovedList = QList<DbusMenuRemovedItem>;

class SniTrayBackend;

class SniItemAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")

    Q_PROPERTY(QString Category READ category)
    Q_PROPERTY(QString Id READ id)
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(quint32 WindowId READ windowId)
    Q_PROPERTY(QString IconName READ iconName)
    Q_PROPERTY(QString IconThemePath READ iconThemePath)
    Q_PROPERTY(vnsnowkey::SniPixmapList IconPixmap READ iconPixmap)
    Q_PROPERTY(QString OverlayIconName READ overlayIconName)
    Q_PROPERTY(vnsnowkey::SniPixmapList OverlayIconPixmap READ overlayIconPixmap)
    Q_PROPERTY(QString AttentionIconName READ attentionIconName)
    Q_PROPERTY(vnsnowkey::SniPixmapList AttentionIconPixmap READ attentionIconPixmap)
    Q_PROPERTY(QString AttentionMovieName READ attentionMovieName)
    Q_PROPERTY(vnsnowkey::SniToolTip ToolTip READ toolTip)
    Q_PROPERTY(bool ItemIsMenu READ itemIsMenu)
    Q_PROPERTY(QDBusObjectPath Menu READ menuPath)

public:
    explicit SniItemAdaptor(SniTrayBackend* parent);

    QString category() const { return QStringLiteral("SystemServices"); }
    QString id() const { return QStringLiteral("vnsnowkey"); }
    QString title() const;
    QString status() const { return QStringLiteral("Active"); }
    quint32 windowId() const { return 0; }
    QString iconName() const { return QString(); }
    QString iconThemePath() const { return QString(); }
    SniPixmapList iconPixmap() const;
    QString overlayIconName() const { return QString(); }
    SniPixmapList overlayIconPixmap() const { return {}; }
    QString attentionIconName() const { return QString(); }
    SniPixmapList attentionIconPixmap() const { return {}; }
    QString attentionMovieName() const { return QString(); }
    SniToolTip toolTip() const;
    bool itemIsMenu() const { return false; }
    QDBusObjectPath menuPath() const { return QDBusObjectPath(QStringLiteral("/MenuBar")); }

public slots:
    Q_NOREPLY void Activate(int x, int y);
    Q_NOREPLY void SecondaryActivate(int x, int y);
    Q_NOREPLY void ContextMenu(int x, int y);
    Q_NOREPLY void Scroll(int delta, const QString& orientation);

signals:
    void NewTitle();
    void NewIcon();
    void NewIconThemePath(const QString& iconThemePath);
    void NewAttentionIcon();
    void NewOverlayIcon();
    void NewToolTip();
    void NewStatus(const QString& status);

private:
    SniTrayBackend* owner_ = nullptr;
};

class DbusMenuAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")

    Q_PROPERTY(quint32 Version READ version)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QString TextDirection READ textDirection)
    Q_PROPERTY(QStringList IconThemePath READ iconThemePath)

public:
    explicit DbusMenuAdaptor(SniTrayBackend* parent);

    quint32 version() const { return 3; }
    QString status() const { return QStringLiteral("normal"); }
    QString textDirection() const { return QStringLiteral("ltr"); }
    QStringList iconThemePath() const { return {QStringLiteral("/usr/share/vnsnowkey/icons")}; }

public slots:
    quint32 GetLayout(int parentId, int recursionDepth, const QStringList& propertyNames, vnsnowkey::DbusMenuItem& layout);
    vnsnowkey::DbusMenuPropsList GetGroupProperties(const QList<int>& ids, const QStringList& propertyNames);
    QDBusVariant GetProperty(int id, const QString& name);
    Q_NOREPLY void Event(int id, const QString& eventId, const QDBusVariant& data, quint32 timestamp);
    bool AboutToShow(int id);

signals:
    void ItemsPropertiesUpdated(const vnsnowkey::DbusMenuPropsList& updatedProps,
                                const vnsnowkey::DbusMenuRemovedList& removedProps);
    void LayoutUpdated(quint32 revision, int parent);
    void ItemActivationRequested(int id, quint32 timestamp);

private:
    SniTrayBackend* owner_ = nullptr;
};

/**
 * Pure Qt6/QtDBus StatusNotifierItem + com.canonical.dbusmenu host client.
 * Allows GNOME (ubuntu-appindicators / AppIndicator extension), Waybar (Hyprland/Sway),
 * and KDE Plasma to display VNSnowKey's tray icon and full context menu without KF6 or GTK3 plugins.
 */
class SniTrayBackend : public QObject {
    Q_OBJECT

public:
    explicit SniTrayBackend(QMenu* menu, QObject* parent = nullptr);
    ~SniTrayBackend() override;

    void setIcon(const QIcon& icon);
    void setToolTip(const QString& title, const QString& description);
    void notifyMenuUpdated();
    bool isHostActive() const { return hostActive_; }

signals:
    void activateRequested();
    void hostAvailabilityChanged(bool active);

private slots:
    void tryRegisterWithWatcher();
    void onWatcherRegistered();
    void onWatcherUnregistered();

private:
    friend class SniItemAdaptor;
    friend class DbusMenuAdaptor;

    void rebuildActionMap();
    void indexMenuRecursive(QMenu* menu);
    DbusMenuItem buildNodeRecursive(int id, int depthRemaining, const QStringList& propertyNames) const;
    QVariantMap propertiesForId(int id, const QStringList& propertyNames) const;
    void setHostActive(bool active);

    static void registerMetaTypes();

    QMenu* menu_ = nullptr;
    QString serviceName_;
    QString title_ = QStringLiteral("VNSnowKey — Bộ gõ Tiếng Việt");
    QString description_;
    SniPixmapList iconPixmaps_;

    QObject* itemObject_ = nullptr;
    QObject* menuObject_ = nullptr;
    SniItemAdaptor* itemAdaptor_ = nullptr;
    DbusMenuAdaptor* menuAdaptor_ = nullptr;
    QDBusServiceWatcher* watcher_ = nullptr;
    bool hostActive_ = false;
    bool dbusRegistered_ = false;

    quint32 revision_ = 1;
    QList<QAction*> idToAction_; // index 0 unused (root); id >= 1 maps to idToAction_[id]
};

} // namespace vnsnowkey

Q_DECLARE_METATYPE(vnsnowkey::SniPixmap)
Q_DECLARE_METATYPE(vnsnowkey::SniPixmapList)
Q_DECLARE_METATYPE(vnsnowkey::SniToolTip)
Q_DECLARE_METATYPE(vnsnowkey::DbusMenuItem)
Q_DECLARE_METATYPE(vnsnowkey::DbusMenuPropsItem)
Q_DECLARE_METATYPE(vnsnowkey::DbusMenuPropsList)
Q_DECLARE_METATYPE(vnsnowkey::DbusMenuRemovedItem)
Q_DECLARE_METATYPE(vnsnowkey::DbusMenuRemovedList)
