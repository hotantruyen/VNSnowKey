/*
 * VNSnowKey - Vietnamese Input Method for Linux Wayland and X11
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sni_tray.hpp"
#include <QActionGroup>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QImage>
#include <QPixmap>
#include <QTimer>
#include <QtEndian>
#include <atomic>

QDBusArgument& operator<<(QDBusArgument& arg, const vnsnowkey::SniPixmap& p) {
    arg.beginStructure();
    arg << p.width << p.height << p.data;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, vnsnowkey::SniPixmap& p) {
    arg.beginStructure();
    arg >> p.width >> p.height >> p.data;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const vnsnowkey::SniToolTip& tt) {
    arg.beginStructure();
    arg << tt.iconName << tt.iconPixmap << tt.title << tt.description;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, vnsnowkey::SniToolTip& tt) {
    arg.beginStructure();
    arg >> tt.iconName >> tt.iconPixmap >> tt.title >> tt.description;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const vnsnowkey::DbusMenuItem& item) {
    arg.beginStructure();
    arg << item.id << item.properties << item.children;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, vnsnowkey::DbusMenuItem& item) {
    arg.beginStructure();
    arg >> item.id >> item.properties >> item.children;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const vnsnowkey::DbusMenuPropsItem& item) {
    arg.beginStructure();
    arg << item.id << item.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, vnsnowkey::DbusMenuPropsItem& item) {
    arg.beginStructure();
    arg >> item.id >> item.properties;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const vnsnowkey::DbusMenuRemovedItem& item) {
    arg.beginStructure();
    arg << item.id << item.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, vnsnowkey::DbusMenuRemovedItem& item) {
    arg.beginStructure();
    arg >> item.id >> item.properties;
    arg.endStructure();
    return arg;
}

namespace vnsnowkey {

namespace {
std::atomic<int> g_sniInstanceCounter{0};
constexpr const char* kWatcherService = "org.kde.StatusNotifierWatcher";
constexpr const char* kWatcherPath = "/StatusNotifierWatcher";
constexpr const char* kWatcherIface = "org.kde.StatusNotifierWatcher";
} // namespace

// ─── SniItemAdaptor ───────────────────────────────────────────────────────────

SniItemAdaptor::SniItemAdaptor(SniTrayBackend* parent)
    : QDBusAbstractAdaptor(parent->itemObject_), owner_(parent) {}

QString SniItemAdaptor::title() const {
    return owner_ ? owner_->title_ : QString();
}

SniPixmapList SniItemAdaptor::iconPixmap() const {
    return owner_ ? owner_->iconPixmaps_ : SniPixmapList{};
}

SniToolTip SniItemAdaptor::toolTip() const {
    SniToolTip tt;
    if (owner_) {
        tt.title = owner_->title_;
        tt.description = owner_->description_;
        tt.iconPixmap = owner_->iconPixmaps_;
    }
    return tt;
}

void SniItemAdaptor::Activate(int /*x*/, int /*y*/) {
    if (owner_) {
        emit owner_->activateRequested();
    }
}

void SniItemAdaptor::SecondaryActivate(int /*x*/, int /*y*/) {
    if (owner_) {
        emit owner_->activateRequested();
    }
}

void SniItemAdaptor::ContextMenu(int /*x*/, int /*y*/) {}

void SniItemAdaptor::Scroll(int /*delta*/, const QString& /*orientation*/) {}

// ─── DbusMenuAdaptor ──────────────────────────────────────────────────────────

DbusMenuAdaptor::DbusMenuAdaptor(SniTrayBackend* parent)
    : QDBusAbstractAdaptor(parent->menuObject_), owner_(parent) {}

quint32 DbusMenuAdaptor::GetLayout(int parentId,
                                   int recursionDepth,
                                   const QStringList& propertyNames,
                                   DbusMenuItem& layout) {
    if (!owner_) {
        layout = DbusMenuItem{};
        return 1;
    }
    owner_->rebuildActionMap();
    layout = owner_->buildNodeRecursive(parentId, recursionDepth, propertyNames);
    return owner_->revision_;
}

DbusMenuPropsList DbusMenuAdaptor::GetGroupProperties(const QList<int>& ids, const QStringList& propertyNames) {
    DbusMenuPropsList out;
    if (!owner_) return out;
    owner_->rebuildActionMap();
    for (int id : ids) {
        DbusMenuPropsItem entry;
        entry.id = id;
        entry.properties = owner_->propertiesForId(id, propertyNames);
        out.append(entry);
    }
    return out;
}

QDBusVariant DbusMenuAdaptor::GetProperty(int id, const QString& name) {
    if (!owner_) return QDBusVariant(QVariant());
    owner_->rebuildActionMap();
    const QVariantMap props = owner_->propertiesForId(id, {name});
    return QDBusVariant(props.value(name));
}

void DbusMenuAdaptor::Event(int id, const QString& eventId, const QDBusVariant& /*data*/, quint32 /*timestamp*/) {
    if (!owner_ || eventId != QStringLiteral("clicked")) return;
    if (id > 0 && id < owner_->idToAction_.size()) {
        QAction* act = owner_->idToAction_[id];
        if (act && act->isEnabled() && !act->isSeparator()) {
            QTimer::singleShot(0, act, &QAction::trigger);
        }
    }
}

bool DbusMenuAdaptor::AboutToShow(int /*id*/) {
    return false;
}

// ─── SniTrayBackend ───────────────────────────────────────────────────────────

void SniTrayBackend::registerMetaTypes() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    qDBusRegisterMetaType<SniPixmap>();
    qDBusRegisterMetaType<SniPixmapList>();
    qDBusRegisterMetaType<SniToolTip>();
    qDBusRegisterMetaType<DbusMenuItem>();
    qDBusRegisterMetaType<DbusMenuPropsItem>();
    qDBusRegisterMetaType<DbusMenuPropsList>();
    qDBusRegisterMetaType<DbusMenuRemovedItem>();
    qDBusRegisterMetaType<DbusMenuRemovedList>();
}

SniTrayBackend::SniTrayBackend(QMenu* menu, QObject* parent)
    : QObject(parent), menu_(menu) {
    registerMetaTypes();

    const int idx = ++g_sniInstanceCounter;
    serviceName_ = QStringLiteral("org.kde.StatusNotifierItem-%1-%2")
                       .arg(QCoreApplication::applicationPid())
                       .arg(idx);

    itemObject_ = new QObject(this);
    menuObject_ = new QObject(this);
    itemAdaptor_ = new SniItemAdaptor(this);
    menuAdaptor_ = new DbusMenuAdaptor(this);

    rebuildActionMap();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        if (bus.registerService(serviceName_)) {
            dbusRegistered_ = bus.registerObject(QStringLiteral("/StatusNotifierItem"), itemObject_) &&
                              bus.registerObject(QStringLiteral("/MenuBar"), menuObject_);
        }

        watcher_ = new QDBusServiceWatcher(
            QString::fromLatin1(kWatcherService),
            bus,
            QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
            this);
        connect(watcher_, &QDBusServiceWatcher::serviceRegistered, this, &SniTrayBackend::onWatcherRegistered);
        connect(watcher_, &QDBusServiceWatcher::serviceUnregistered, this, &SniTrayBackend::onWatcherUnregistered);

        // If GNOME Shell / AppIndicator registers StatusNotifierHost slightly after creating Watcher,
        // re-register cleanly so the icon always appears after login.
        bus.connect(
            QString::fromLatin1(kWatcherService),
            QString::fromLatin1(kWatcherPath),
            QString::fromLatin1(kWatcherIface),
            QStringLiteral("StatusNotifierHostRegistered"),
            this,
            SLOT(tryRegisterWithWatcher()));

        // Register after TrayController finishes building the initial menu and IconPixmap
        // so StatusNotifierWatcher hosts never observe an empty IconPixmap during init().
        QTimer::singleShot(0, this, &SniTrayBackend::tryRegisterWithWatcher);
    }
}

SniTrayBackend::~SniTrayBackend() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        bus.unregisterObject(QStringLiteral("/StatusNotifierItem"));
        bus.unregisterObject(QStringLiteral("/MenuBar"));
        if (!serviceName_.isEmpty()) {
            bus.unregisterService(serviceName_);
        }
    }
}

void SniTrayBackend::setHostActive(bool active) {
    if (hostActive_ == active) return;
    hostActive_ = active;
    emit hostAvailabilityChanged(active);
}

void SniTrayBackend::onWatcherRegistered() {
    tryRegisterWithWatcher();
}

void SniTrayBackend::onWatcherUnregistered() {
    setHostActive(false);
}

void SniTrayBackend::tryRegisterWithWatcher() {
    if (!dbusRegistered_) return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()) return;

    if (!bus.interface()->isServiceRegistered(QString::fromLatin1(kWatcherService))) {
        setHostActive(false);
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(kWatcherService),
        QString::fromLatin1(kWatcherPath),
        QString::fromLatin1(kWatcherIface),
        QStringLiteral("RegisterStatusNotifierItem"));
    msg << serviceName_;

    QDBusPendingCall call = bus.asyncCall(msg);
    auto* callWatcher = new QDBusPendingCallWatcher(call, this);
    connect(callWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        QDBusPendingReply<> reply = *w;
        w->deleteLater();
        const bool ok = !reply.isError();
        setHostActive(ok);
        if (ok) {
            if (itemAdaptor_) {
                emit itemAdaptor_->NewIcon();
                emit itemAdaptor_->NewToolTip();
                emit itemAdaptor_->NewStatus(QStringLiteral("Active"));
            }
            if (menuAdaptor_) {
                emit menuAdaptor_->LayoutUpdated(revision_, 0);
            }
        }
    });
}

void SniTrayBackend::setIcon(const QIcon& icon) {
    SniPixmapList list;
    const int sizes[] = {16, 22, 24, 32, 48};
    for (int sz : sizes) {
        const QPixmap pm = icon.pixmap(sz, sz);
        if (pm.isNull()) continue;
        const QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
        QByteArray raw(img.width() * img.height() * 4, Qt::Uninitialized);
        auto* dst = reinterpret_cast<uchar*>(raw.data());
        for (int y = 0; y < img.height(); ++y) {
            const auto* row = reinterpret_cast<const quint32*>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                qToBigEndian<quint32>(row[x], dst + (y * img.width() + x) * 4);
            }
        }
        list.append(SniPixmap{img.width(), img.height(), raw});
    }
    iconPixmaps_ = std::move(list);
    if (itemAdaptor_) {
        emit itemAdaptor_->NewIcon();
        emit itemAdaptor_->NewToolTip();
    }
}

void SniTrayBackend::setToolTip(const QString& title, const QString& description) {
    title_ = title;
    description_ = description;
    if (itemAdaptor_) {
        emit itemAdaptor_->NewTitle();
        emit itemAdaptor_->NewToolTip();
    }
}

void SniTrayBackend::notifyMenuUpdated() {
    rebuildActionMap();
    ++revision_;
    if (menuAdaptor_) {
        emit menuAdaptor_->LayoutUpdated(revision_, 0);
    }
}

void SniTrayBackend::rebuildActionMap() {
    idToAction_.clear();
    idToAction_.append(nullptr); // 0 = root
    if (menu_) {
        indexMenuRecursive(menu_);
    }
}

void SniTrayBackend::indexMenuRecursive(QMenu* menu) {
    if (!menu) return;
    for (QAction* act : menu->actions()) {
        if (!act) continue;
        idToAction_.append(act);
        if (QMenu* sub = act->menu()) {
            indexMenuRecursive(sub);
        }
    }
}

QVariantMap SniTrayBackend::propertiesForId(int id, const QStringList& propertyNames) const {
    QVariantMap props;
    auto want = [&](const QString& key) {
        return propertyNames.isEmpty() || propertyNames.contains(key);
    };

    if (id == 0) {
        if (want(QStringLiteral("children-display"))) {
            props.insert(QStringLiteral("children-display"), QStringLiteral("submenu"));
        }
        return props;
    }
    if (id < 0 || id >= idToAction_.size()) {
        return props;
    }

    QAction* act = idToAction_[id];
    if (!act) return props;

    if (act->isSeparator()) {
        if (want(QStringLiteral("type"))) props.insert(QStringLiteral("type"), QStringLiteral("separator"));
        if (want(QStringLiteral("visible"))) props.insert(QStringLiteral("visible"), act->isVisible());
        return props;
    }

    if (want(QStringLiteral("label"))) props.insert(QStringLiteral("label"), act->text());
    if (want(QStringLiteral("enabled"))) props.insert(QStringLiteral("enabled"), act->isEnabled());
    if (want(QStringLiteral("visible"))) props.insert(QStringLiteral("visible"), act->isVisible());
    if (act->menu()) {
        if (want(QStringLiteral("children-display"))) {
            props.insert(QStringLiteral("children-display"), QStringLiteral("submenu"));
        }
    } else if (act->isCheckable()) {
        if (want(QStringLiteral("toggle-type"))) {
            const bool radio = act->actionGroup() && act->actionGroup()->isExclusive();
            props.insert(QStringLiteral("toggle-type"), radio ? QStringLiteral("radio") : QStringLiteral("checkmark"));
        }
        if (want(QStringLiteral("toggle-state"))) {
            props.insert(QStringLiteral("toggle-state"), act->isChecked() ? 1 : 0);
        }
    }
    return props;
}

DbusMenuItem SniTrayBackend::buildNodeRecursive(int id, int depthRemaining, const QStringList& propertyNames) const {
    DbusMenuItem node;
    node.id = id;
    node.properties = propertiesForId(id, propertyNames);

    if (depthRemaining == 0) {
        return node;
    }

    QMenu* childMenu = nullptr;
    if (id == 0) {
        childMenu = menu_;
    } else if (id > 0 && id < idToAction_.size() && idToAction_[id]) {
        childMenu = idToAction_[id]->menu();
    }
    if (!childMenu) {
        return node;
    }

    const int nextDepth = (depthRemaining < 0) ? -1 : (depthRemaining - 1);
    for (QAction* act : childMenu->actions()) {
        const int childId = idToAction_.indexOf(act);
        if (childId <= 0) continue;
        const DbusMenuItem childNode = buildNodeRecursive(childId, nextDepth, propertyNames);
        node.children.append(QDBusVariant(QVariant::fromValue(childNode)));
    }
    return node;
}

} // namespace vnsnowkey
