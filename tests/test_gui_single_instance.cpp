/*
 * VNSnowKey - Vietnamese Input Method for Arch Linux + KDE Plasma (Wayland)
 * Automated D-Bus Single-Instance & Single Tray Icon Verification Test
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace {

int g_passed = 0;
int g_failed = 0;

void checkBool(bool got, bool expected, const char* expr, int line) {
    if (got == expected) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "[FAIL] Line " << line << " (" << expr << "): got ["
                  << (got ? "true" : "false") << "], expected ["
                  << (expected ? "true" : "false") << "]\n";
    }
}

void checkInt(int got, int expected, const char* expr, int line) {
    if (got == expected) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "[FAIL] Line " << line << " (" << expr << "): got [" << got
                  << "], expected [" << expected << "]\n";
    }
}

#define EXPECT_BOOL_EQ(a, b) checkBool((a), (b), #a, __LINE__)
#define EXPECT_INT_EQ(a, b) checkInt((a), (b), #a, __LINE__)

class MockStatusNotifierWatcher : public QDBusVirtualObject {
public:
    QStringList registeredItems;
    QStringList rawRegistrations;

    QString introspect(const QString& /*path*/) const override {
        return QStringLiteral(
            "<node>"
            "  <interface name=\"org.kde.StatusNotifierWatcher\">"
            "    <method name=\"RegisterStatusNotifierItem\">"
            "      <arg name=\"service\" type=\"s\" direction=\"in\"/>"
            "    </method>"
            "    <method name=\"RegisterStatusNotifierHost\">"
            "      <arg name=\"service\" type=\"s\" direction=\"in\"/>"
            "    </method>"
            "    <property name=\"RegisteredStatusNotifierItems\" type=\"as\" access=\"read\"/>"
            "    <property name=\"IsStatusNotifierHostRegistered\" type=\"b\" access=\"read\"/>"
            "    <property name=\"ProtocolVersion\" type=\"i\" access=\"read\"/>"
            "  </interface>"
            "</node>");
    }

    bool handleMessage(const QDBusMessage& message, const QDBusConnection& conn) override {
        const QString iface = message.interface();
        const QString member = message.member();

        if (iface == QStringLiteral("org.kde.StatusNotifierWatcher") &&
            member == QStringLiteral("RegisterStatusNotifierItem")) {
            QString service = message.arguments().value(0).toString();
            if (service.startsWith(QLatin1Char('/'))) {
                service = message.service() + service;
            }
            rawRegistrations.append(service);
            if (!registeredItems.contains(service)) {
                registeredItems.append(service);
            }
            conn.send(message.createReply());
            return true;
        }

        if (iface == QStringLiteral("org.kde.StatusNotifierWatcher") &&
            member == QStringLiteral("RegisterStatusNotifierHost")) {
            conn.send(message.createReply());
            return true;
        }

        if (iface == QStringLiteral("org.freedesktop.DBus.Properties") &&
            member == QStringLiteral("Get")) {
            const QString prop = message.arguments().value(1).toString();
            QVariant val;
            if (prop == QStringLiteral("IsStatusNotifierHostRegistered")) {
                val = true;
            } else if (prop == QStringLiteral("ProtocolVersion")) {
                val = 0;
            } else if (prop == QStringLiteral("RegisteredStatusNotifierItems")) {
                val = registeredItems;
            } else {
                conn.send(message.createErrorReply(QDBusError::UnknownProperty, prop));
                return true;
            }
            QDBusMessage reply = message.createReply();
            reply << QVariant::fromValue(QDBusVariant(val));
            conn.send(reply);
            return true;
        }

        if (iface == QStringLiteral("org.freedesktop.DBus.Properties") &&
            member == QStringLiteral("GetAll")) {
            QVariantMap map;
            map.insert(QStringLiteral("IsStatusNotifierHostRegistered"), true);
            map.insert(QStringLiteral("ProtocolVersion"), 0);
            map.insert(QStringLiteral("RegisteredStatusNotifierItems"), registeredItems);
            QDBusMessage reply = message.createReply();
            reply << map;
            conn.send(reply);
            return true;
        }

        return false;
    }
};

QStringList findActiveStatusNotifierItems(const QDBusConnection& bus) {
    QStringList out;
    if (!bus.interface()) return out;
    const QDBusReply<QStringList> names = bus.interface()->registeredServiceNames();
    if (!names.isValid()) return out;
    for (const QString& n : names.value()) {
        if (n.startsWith(QStringLiteral("org.kde.StatusNotifierItem-"))) {
            out.append(n);
        }
    }
    return out;
}

void pumpEventsMs(int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        usleep(10000);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (qEnvironmentVariableIsEmpty("VNSNOWKEY_ISOLATED_DBUS")) {
        setenv("VNSNOWKEY_ISOLATED_DBUS", "1", 1);
        int rc = QProcess::execute(QStringLiteral("dbus-run-session"),
                                   {QStringLiteral("--"), QString::fromLocal8Bit(argv[0])});
        return rc;
    }

    QCoreApplication app(argc, argv);

    QTemporaryDir tmpHome;
    EXPECT_BOOL_EQ(tmpHome.isValid(), true);

    QDBusConnection bus = QDBusConnection::sessionBus();
    EXPECT_BOOL_EQ(bus.isConnected(), true);

    MockStatusNotifierWatcher mockWatcher;
    EXPECT_BOOL_EQ(bus.registerService(QStringLiteral("org.kde.StatusNotifierWatcher")), true);
    EXPECT_BOOL_EQ(bus.registerVirtualObject(QStringLiteral("/StatusNotifierWatcher"),
                                             &mockWatcher,
                                             QDBusConnection::VirtualObjectRegisterOption::SubPath),
                   true);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), tmpHome.path() + QStringLiteral("/config"));
    env.insert(QStringLiteral("XDG_RUNTIME_DIR"), tmpHome.path() + QStringLiteral("/runtime"));
    std::filesystem::create_directories((tmpHome.path() + QStringLiteral("/config")).toStdString());
    std::filesystem::create_directories((tmpHome.path() + QStringLiteral("/runtime")).toStdString());

    const QString guiBin = QCoreApplication::applicationDirPath() + QStringLiteral("/vnsnowkey-gui");
    const QString daemonBin = QCoreApplication::applicationDirPath() + QStringLiteral("/vnsnowkeyd");

    // 1. Launch first vnsnowkey-gui (--tray)
    QProcess p1;
    p1.setProcessEnvironment(env);
    p1.start(guiBin, {QStringLiteral("--tray")});
    EXPECT_BOOL_EQ(p1.waitForStarted(3000), true);

    // Wait up to 3s for 1 StatusNotifierItem registration on D-Bus (both KF6 and built-in Qt6 SNI)
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 3000) {
        pumpEventsMs(50);
        if (!mockWatcher.registeredItems.isEmpty()) {
            break;
        }
    }

    EXPECT_INT_EQ(mockWatcher.registeredItems.size(), 1);
    EXPECT_INT_EQ(mockWatcher.rawRegistrations.size(), 1);

    // 2. Launch second vnsnowkey-gui (--settings): must activate first instance and exit quickly,
    // NEVER creating a second StatusNotifierItem!
    QProcess p2;
    p2.setProcessEnvironment(env);
    p2.start(guiBin, {QStringLiteral("--settings")});
    EXPECT_BOOL_EQ(p2.waitForStarted(3000), true);

    // Give second instance up to 2.5s to hand off to first instance and exit
    bool p2Exited = false;
    t.restart();
    while (t.elapsed() < 2500) {
        pumpEventsMs(50);
        if (p2.state() == QProcess::NotRunning) {
            p2Exited = true;
            break;
        }
    }

    EXPECT_BOOL_EQ(p2Exited, true);
    if (p2Exited) {
        EXPECT_INT_EQ(p2.exitCode(), 0);
    }

    // Pump briefly to ensure no delayed second tray registration arrives
    pumpEventsMs(300);
    EXPECT_INT_EQ(mockWatcher.registeredItems.size(), 1);
    EXPECT_INT_EQ(mockWatcher.rawRegistrations.size(), 1);
    EXPECT_BOOL_EQ(p1.state() == QProcess::Running, true);

    // Verify org.vnsnowkey.Gui D-Bus service is registered by instance 1 and responds to IsSettingsVisible
    const QDBusReply<QStringList> allServices = bus.interface()->registeredServiceNames();
    EXPECT_BOOL_EQ(allServices.isValid() && allServices.value().contains(QStringLiteral("org.vnsnowkey.Gui")), true);
    QDBusMessage visQuery = QDBusMessage::createMethodCall(
        QStringLiteral("org.vnsnowkey.Gui"),
        QStringLiteral("/org/vnsnowkey/Gui"),
        QStringLiteral("org.vnsnowkey.Gui"),
        QStringLiteral("IsSettingsVisible"));
    QDBusMessage visReply = bus.call(visQuery, QDBus::Block, 1000);
    EXPECT_BOOL_EQ(visReply.type() == QDBusMessage::ReplyMessage &&
                   visReply.arguments().value(0).toBool(),
                   true);

    // Also launch a 3rd instance (--tray) while instance 1 is active: must exit 0 cleanly with 0 new tray items
    QProcess p3;
    p3.setProcessEnvironment(env);
    p3.start(guiBin, {QStringLiteral("--tray")});
    EXPECT_BOOL_EQ(p3.waitForStarted(3000), true);
    bool p3Exited = false;
    t.restart();
    while (t.elapsed() < 2500) {
        pumpEventsMs(50);
        if (p3.state() == QProcess::NotRunning) {
            p3Exited = true;
            break;
        }
    }
    EXPECT_BOOL_EQ(p3Exited && p3.exitCode() == 0, true);
    pumpEventsMs(200);
    EXPECT_INT_EQ(mockWatcher.registeredItems.size(), 1);
    EXPECT_INT_EQ(mockWatcher.rawRegistrations.size(), 1);

    // Cleanly stop p1 and p2/p3 if still alive
    for (QProcess* proc : {&p3, &p2, &p1}) {
        if (proc->state() != QProcess::NotRunning) {
            proc->terminate();
            proc->waitForFinished(1500);
            if (proc->state() != QProcess::NotRunning) proc->kill();
        }
    }

    // 3. Verify vnsnowkeyd itself NEVER registers a StatusNotifierItem on D-Bus (and does not link QtWidgets/KStatusNotifierItem)
    mockWatcher.rawRegistrations.clear();
    mockWatcher.registeredItems.clear();
    QProcess daemonProc;
    QProcessEnvironment dEnv = env;
    dEnv.insert(QStringLiteral("VNSNOWKEY_ONLY_DEVICES"), QStringLiteral("/dev/null"));
    daemonProc.setProcessEnvironment(dEnv);
    daemonProc.start(daemonBin, {QStringLiteral("--no-grab")});
    EXPECT_BOOL_EQ(daemonProc.waitForStarted(3000), true);
    pumpEventsMs(400);
    EXPECT_INT_EQ(findActiveStatusNotifierItems(bus).size(), 0);
    EXPECT_INT_EQ(mockWatcher.rawRegistrations.size(), 0);
    daemonProc.terminate();
    daemonProc.waitForFinished(2000);
    if (daemonProc.state() != QProcess::NotRunning) daemonProc.kill();

    // Check shared libraries linked by vnsnowkeyd
    QProcess lddProc;
    lddProc.start(QStringLiteral("ldd"), {daemonBin});
    lddProc.waitForFinished(2000);
    const QString lddOut = QString::fromUtf8(lddProc.readAllStandardOutput());
    EXPECT_BOOL_EQ(!lddOut.contains(QStringLiteral("StatusNotifierItem")), true);
    EXPECT_BOOL_EQ(!lddOut.contains(QStringLiteral("Qt6Widgets")), true);

    std::cout << "GUI Single-Instance & Single-Tray Tests Summary: "
              << g_passed << " passed, " << g_failed << " failed.\n";
    return g_failed == 0 ? 0 : 1;
}
