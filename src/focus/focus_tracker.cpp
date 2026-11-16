/*
 * VNSnowKey - Vietnamese Input Method for Arch Linux + KDE Plasma (Wayland)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "focus_tracker.hpp"
#include "xwayland_env.hpp"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace vnsnowkey {

namespace {

constexpr const char* kKWinPluginName = "vnsnowkey-focus";
constexpr const char* kGnomeExtUuid = "vnsnowkey-focus@vnsnowkey.org";

const char* kKWinScriptSource = R"JS(
function notifyVNSnowKey(window) {
    if (!window) {
        return;
    }
    var appClass = window.resourceClass ? window.resourceClass.toString() : "";
    var appName = window.resourceName ? window.resourceName.toString() : "";
    var caption = window.caption ? window.caption.toString() : "";
    var winId = window.internalId ? window.internalId.toString() : "";

    callDBus(
        "org.vnsnowkey.Daemon",
        "/org/vnsnowkey/Daemon",
        "org.vnsnowkey.Daemon",
        "NotifyWindowFocus",
        appClass,
        appName,
        caption,
        winId
    );
}

if (typeof workspace.windowActivated !== "undefined") {
    workspace.windowActivated.connect(notifyVNSnowKey);
    if (workspace.activeWindow) {
        notifyVNSnowKey(workspace.activeWindow);
    }
} else if (typeof workspace.clientActivated !== "undefined") {
    workspace.clientActivated.connect(notifyVNSnowKey);
    if (workspace.activeClient) {
        notifyVNSnowKey(workspace.activeClient);
    }
}
)JS";

std::string toLowerAscii(std::string_view sv) {
    std::string out(sv);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return out;
}

std::string getEnvStr(const char* name) {
    const char* val = std::getenv(name);
    return val ? std::string(val) : std::string();
}

int connectUnixSocket(const std::string& path) {
    if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) {
        return -1;
    }
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool readPollableBytes(int fd, int wakeFd, void* dst, size_t len, const std::atomic<bool>& running) {
    size_t got = 0;
    auto* out = static_cast<uint8_t*>(dst);
    while (got < len && running.load()) {
        struct pollfd fds[2]{};
        fds[0].fd = fd;
        fds[0].events = POLLIN;
        fds[1].fd = wakeFd;
        fds[1].events = POLLIN;
        int rc = ::poll(fds, (wakeFd >= 0) ? 2 : 1, -1);
        if (rc < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (wakeFd >= 0 && (fds[1].revents & POLLIN)) {
            return false;
        }
        if (fds[0].revents & POLLIN) {
            ssize_t n = ::read(fd, out + got, len - got);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                return false;
            }
            got += static_cast<size_t>(n);
        } else if (fds[0].revents & (POLLERR | POLLHUP)) {
            return false;
        }
    }
    return got == len;
}

bool writeAll(int fd, const void* src, size_t len) {
    size_t sent = 0;
    const auto* in = static_cast<const uint8_t*>(src);
    while (sent < len) {
        ssize_t n = ::write(fd, in + sent, len - sent);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool extractSwayFocusedFromTree(const QJsonObject& node, std::string& appClass, std::string& appName, std::string& caption, std::string& winId) {
    if (node.value(QStringLiteral("focused")).toBool()) {
        QString appId = node.value(QStringLiteral("app_id")).toString();
        QJsonObject wp = node.value(QStringLiteral("window_properties")).toObject();
        QString wmClass = wp.value(QStringLiteral("class")).toString();
        QString wmInst = wp.value(QStringLiteral("instance")).toString();
        appClass = (appId.isEmpty() ? wmClass : appId).toStdString();
        appName = (wmInst.isEmpty() ? QString::fromStdString(appClass) : wmInst).toStdString();
        caption = node.value(QStringLiteral("name")).toString().toStdString();
        winId = QString::number(node.value(QStringLiteral("id")).toInteger()).toStdString();
        return true;
    }
    const QJsonArray nodes = node.value(QStringLiteral("nodes")).toArray();
    for (const QJsonValue& child : nodes) {
        if (extractSwayFocusedFromTree(child.toObject(), appClass, appName, caption, winId)) {
            return true;
        }
    }
    const QJsonArray fNodes = node.value(QStringLiteral("floating_nodes")).toArray();
    for (const QJsonValue& child : fNodes) {
        if (extractSwayFocusedFromTree(child.toObject(), appClass, appName, caption, winId)) {
            return true;
        }
    }
    return false;
}

std::atomic<bool> g_x11FocusErr{false};
int safeFocusXErrorHandler(void* /*dpy*/, void* /*err*/) {
    g_x11FocusErr.store(true, std::memory_order_relaxed);
    return 0;
}

} // namespace

DesktopBackend detectDesktopBackend(
    const std::string& xdgDesktop,
    const std::string& sessionType,
    const std::string& hyprlandSignature,
    const std::string& swaySocket,
    const std::string& waylandDisplay,
    const std::string& x11Display) {
    if (!hyprlandSignature.empty()) {
        return DesktopBackend::Hyprland;
    }
    if (!swaySocket.empty()) {
        return DesktopBackend::Sway;
    }
    const std::string lcDesk = toLowerAscii(xdgDesktop);
    const std::string lcSess = toLowerAscii(sessionType);
    const bool isWayland = (lcSess == "wayland") || !waylandDisplay.empty();

    if (lcDesk.find("hyprland") != std::string::npos) {
        return DesktopBackend::Hyprland;
    }
    if (lcDesk.find("sway") != std::string::npos) {
        return DesktopBackend::Sway;
    }
    if (lcDesk.find("kde") != std::string::npos || lcDesk.find("plasma") != std::string::npos) {
        return (isWayland || x11Display.empty()) ? DesktopBackend::KWin : DesktopBackend::X11;
    }
    if (lcDesk.find("gnome") != std::string::npos || lcDesk.find("ubuntu") != std::string::npos) {
        return isWayland ? DesktopBackend::GnomeShell : DesktopBackend::X11;
    }
    if (!x11Display.empty() && !isWayland) {
        return DesktopBackend::X11;
    }
    if (!waylandDisplay.empty()) {
        return DesktopBackend::KWin;
    }
    if (!x11Display.empty()) {
        return DesktopBackend::X11;
    }
    return DesktopBackend::None;
}

const char* desktopBackendToString(DesktopBackend backend) {
    switch (backend) {
        case DesktopBackend::None: return "none";
        case DesktopBackend::KWin: return "kwin";
        case DesktopBackend::Hyprland: return "hyprland";
        case DesktopBackend::Sway: return "sway";
        case DesktopBackend::GnomeShell: return "gnome_shell";
        case DesktopBackend::X11: return "x11";
    }
    return "none";
}

bool parseHyprlandActiveWindowLine(
    const std::string& line,
    std::string& outAppClass,
    std::string& outCaption) {
    constexpr const char* kPrefix = "activewindow>>";
    if (line.rfind(kPrefix, 0) != 0) {
        return false;
    }
    std::string rest = line.substr(std::strlen(kPrefix));
    size_t comma = rest.find(',');
    if (comma == std::string::npos) {
        outAppClass = rest;
        outCaption.clear();
    } else {
        outAppClass = rest.substr(0, comma);
        outCaption = rest.substr(comma + 1);
    }
    return true;
}

bool parseHyprlandActiveWindowJson(
    const std::string& jsonStr,
    std::string& outAppClass,
    std::string& outCaption,
    std::string& outWindowId) {
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(jsonStr), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    QJsonObject obj = doc.object();
    QString cls = obj.value(QStringLiteral("class")).toString();
    if (cls.isEmpty()) {
        cls = obj.value(QStringLiteral("initialClass")).toString();
    }
    outAppClass = cls.toStdString();
    outCaption = obj.value(QStringLiteral("title")).toString().toStdString();
    outWindowId = obj.value(QStringLiteral("address")).toString().toStdString();
    return !outAppClass.empty() || !outCaption.empty();
}

bool parseSwayWindowEventJson(
    const std::string& jsonStr,
    std::string& outAppClass,
    std::string& outAppName,
    std::string& outCaption,
    std::string& outWindowId) {
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(jsonStr), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    QJsonObject obj = doc.object();
    QString change = obj.value(QStringLiteral("change")).toString();
    if (change != QStringLiteral("focus") && change != QStringLiteral("title")) {
        return false;
    }
    QJsonObject container = obj.value(QStringLiteral("container")).toObject();
    QString appId = container.value(QStringLiteral("app_id")).toString();
    QJsonObject wp = container.value(QStringLiteral("window_properties")).toObject();
    QString wmClass = wp.value(QStringLiteral("class")).toString();
    QString wmInst = wp.value(QStringLiteral("instance")).toString();
    outAppClass = (appId.isEmpty() ? wmClass : appId).toStdString();
    outAppName = (wmInst.isEmpty() ? QString::fromStdString(outAppClass) : wmInst).toStdString();
    outCaption = container.value(QStringLiteral("name")).toString().toStdString();
    outWindowId = QString::number(container.value(QStringLiteral("id")).toInteger()).toStdString();
    return true;
}

FocusTracker::FocusTracker() = default;

FocusTracker::~FocusTracker() {
    stopDesktopFocusMonitor();
}

bool FocusTracker::startDesktopFocusMonitor() {
    stopDesktopFocusMonitor();

    std::string waylandDisp = getEnvStr("WAYLAND_DISPLAY");
    if (waylandDisp.empty() && !xwayland_env::isIsolatedTestEnv()) {
        std::string sockPath = xwayland_env::discoverWaylandSocketPath();
        if (!sockPath.empty()) {
            waylandDisp = std::filesystem::path(sockPath).filename().string();
        }
    }

    activeBackend_ = detectDesktopBackend(
        getEnvStr("XDG_CURRENT_DESKTOP"),
        getEnvStr("XDG_SESSION_TYPE"),
        getEnvStr("HYPRLAND_INSTANCE_SIGNATURE"),
        getEnvStr("SWAYSOCK"),
        waylandDisp,
        getEnvStr("DISPLAY"));

    if (activeBackend_ == DesktopBackend::KWin) {
        return registerKWinScript();
    }
    if (activeBackend_ == DesktopBackend::GnomeShell) {
        gnomeMonitorActive_ = startGnomeExtension();
        if (gnomeMonitorActive_) {
            return true;
        }
        // Fallback to X11 root property tracking for XWayland windows if GNOME extension isn't active yet,
        // and keep retrying XOpenDisplay / GNOME extension activation in the background so on-demand XWayland works.
        running_ = true;
        wakeFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        workerThread_ = std::thread(&FocusTracker::runX11Loop, this);
        return true;
    }
    if (activeBackend_ == DesktopBackend::Hyprland ||
        activeBackend_ == DesktopBackend::Sway ||
        activeBackend_ == DesktopBackend::X11) {
        running_ = true;
        wakeFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (activeBackend_ == DesktopBackend::Hyprland) {
            workerThread_ = std::thread(&FocusTracker::runHyprlandLoop, this);
        } else if (activeBackend_ == DesktopBackend::Sway) {
            workerThread_ = std::thread(&FocusTracker::runSwayLoop, this);
        } else {
            workerThread_ = std::thread(&FocusTracker::runX11Loop, this);
        }
        return true;
    }
    return false;
}

void FocusTracker::stopDesktopFocusMonitor() {
    running_ = false;
    gnomeMonitorActive_ = false;
    if (wakeFd_ >= 0) {
        uint64_t one = 1;
        ssize_t n = ::write(wakeFd_, &one, sizeof(one));
        (void)n;
    }
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    if (wakeFd_ >= 0) {
        ::close(wakeFd_);
        wakeFd_ = -1;
    }
    unregisterKWinScript();
    activeBackend_ = DesktopBackend::None;
}

bool FocusTracker::isDesktopMonitorActive() const {
    if (activeBackend_ == DesktopBackend::KWin) {
        return scriptRegistered_ || isKWinScriptLoaded();
    }
    if (activeBackend_ == DesktopBackend::GnomeShell) {
        return gnomeMonitorActive_ || running_.load();
    }
    return running_.load();
}

bool FocusTracker::startGnomeExtension() {
    QDBusInterface gnomeExt(
        QStringLiteral("org.gnome.Shell.Extensions"),
        QStringLiteral("/org/gnome/Shell/Extensions"),
        QStringLiteral("org.gnome.Shell.Extensions"),
        QDBusConnection::sessionBus());
    if (!gnomeExt.isValid()) {
        return false;
    }

    auto checkEnabled = [&]() -> bool {
        QDBusReply<QVariantMap> rep = gnomeExt.call(QStringLiteral("GetExtensionInfo"), QString::fromUtf8(kGnomeExtUuid));
        if (!rep.isValid()) return false;
        const QVariantMap info = rep.value();
        // GNOME Shell ExtensionState: 1 == ENABLED / ACTIVE
        return info.value(QStringLiteral("state")).toInt() == 1 || info.value(QStringLiteral("enabled")).toBool();
    };

    if (checkEnabled()) {
        return true;
    }

    // Attempt to enable cleanly once if extension package was installed
    gnomeExt.call(QStringLiteral("EnableExtension"), QString::fromUtf8(kGnomeExtUuid));
    return checkEnabled();
}

void FocusTracker::emitRawFocus(
    const std::string& appClass,
    const std::string& appName,
    const std::string& caption,
    const std::string& windowId) {
    if (onRawFocus_) {
        onRawFocus_(appClass, appName, caption, windowId);
    }
}

void FocusTracker::runHyprlandLoop() {
    const std::string sig = getEnvStr("HYPRLAND_INSTANCE_SIGNATURE");
    const std::string runtime = getEnvStr("XDG_RUNTIME_DIR");
    if (sig.empty()) return;

    auto resolveSock = [&](const char* name) -> std::string {
        if (!runtime.empty()) {
            std::string p = runtime + "/hypr/" + sig + "/" + name;
            if (std::filesystem::exists(p)) return p;
        }
        return "/tmp/hypr/" + sig + "/" + name;
    };

    // Query initial active window once
    int qfd = connectUnixSocket(resolveSock(".socket.sock"));
    if (qfd >= 0) {
        const char* cmd = "j/activewindow";
        if (writeAll(qfd, cmd, std::strlen(cmd))) {
            std::string resp;
            char buf[2048];
            ssize_t n = 0;
            while ((n = ::read(qfd, buf, sizeof(buf))) > 0) {
                resp.append(buf, static_cast<size_t>(n));
            }
            std::string cls, cap, wid;
            if (parseHyprlandActiveWindowJson(resp, cls, cap, wid)) {
                emitRawFocus(cls, cls, cap, wid);
            }
        }
        ::close(qfd);
    }

    int fd = connectUnixSocket(resolveSock(".socket2.sock"));
    if (fd < 0) return;

    std::string pending;
    std::string lastAddr;
    char chunk[1024];
    while (running_.load()) {
        struct pollfd fds[2]{};
        fds[0].fd = fd;
        fds[0].events = POLLIN;
        fds[1].fd = wakeFd_;
        fds[1].events = POLLIN;
        int rc = ::poll(fds, (wakeFd_ >= 0) ? 2 : 1, -1);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (wakeFd_ >= 0 && (fds[1].revents & POLLIN)) {
            break;
        }
        if (fds[0].revents & POLLIN) {
            ssize_t n = ::read(fd, chunk, sizeof(chunk));
            if (n <= 0) break;
            pending.append(chunk, static_cast<size_t>(n));
            size_t nl = 0;
            while ((nl = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, nl);
                pending.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("activewindowv2>>", 0) == 0) {
                    lastAddr = line.substr(16);
                } else {
                    std::string cls, cap;
                    if (parseHyprlandActiveWindowLine(line, cls, cap)) {
                        emitRawFocus(cls, cls, cap, lastAddr);
                    }
                }
            }
        } else if (fds[0].revents & (POLLERR | POLLHUP)) {
            break;
        }
    }
    ::close(fd);
}

void FocusTracker::runSwayLoop() {
    const std::string sockPath = getEnvStr("SWAYSOCK");
    int fd = connectUnixSocket(sockPath);
    if (fd < 0) return;

    auto sendIpc = [&](uint32_t type, const std::string& payload) -> bool {
        uint8_t hdr[14];
        std::memcpy(hdr, "i3-ipc", 6);
        uint32_t len = static_cast<uint32_t>(payload.size());
        std::memcpy(hdr + 6, &len, 4);
        std::memcpy(hdr + 10, &type, 4);
        return writeAll(fd, hdr, sizeof(hdr)) &&
               (payload.empty() || writeAll(fd, payload.data(), payload.size()));
    };

    // 1. Seed initial focused window with GET_TREE (type 4)
    if (sendIpc(4, "")) {
        uint8_t hdr[14];
        if (readPollableBytes(fd, wakeFd_, hdr, sizeof(hdr), running_)) {
            uint32_t len = 0;
            std::memcpy(&len, hdr + 6, 4);
            if (len > 0 && len < 16 * 1024 * 1024) {
                std::string body(len, '\0');
                if (readPollableBytes(fd, wakeFd_, body.data(), len, running_)) {
                    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(body));
                    if (doc.isObject()) {
                        std::string cls, name, cap, wid;
                        if (extractSwayFocusedFromTree(doc.object(), cls, name, cap, wid)) {
                            emitRawFocus(cls, name, cap, wid);
                        }
                    }
                }
            }
        }
    }

    // 2. Subscribe to window events (type 2)
    if (!sendIpc(2, "[\"window\"]")) {
        ::close(fd);
        return;
    }

    while (running_.load()) {
        uint8_t hdr[14];
        if (!readPollableBytes(fd, wakeFd_, hdr, sizeof(hdr), running_)) {
            break;
        }
        uint32_t len = 0;
        uint32_t type = 0;
        std::memcpy(&len, hdr + 6, 4);
        std::memcpy(&type, hdr + 10, 4);
        if (len > 16 * 1024 * 1024) break;
        std::string body(len, '\0');
        if (len > 0 && !readPollableBytes(fd, wakeFd_, body.data(), len, running_)) {
            break;
        }
        if (type == 0x80000003u) {
            std::string cls, name, cap, wid;
            if (parseSwayWindowEventJson(body, cls, name, cap, wid)) {
                emitRawFocus(cls, name, cap, wid);
            }
        }
    }
    ::close(fd);
}

namespace {

std::string discoverActiveX11Display() {
    return xwayland_env::resolveLiveX11Display();
}

bool waitWakeOrTimeout(int wakeFd, int timeoutMs) {
    if (wakeFd < 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return false;
    }
    struct pollfd pfd{wakeFd, POLLIN, 0};
    int rc = ::poll(&pfd, 1, timeoutMs);
    return (rc > 0 && (pfd.revents & POLLIN));
}

bool containsLowerAscii(const std::string& hay, const std::string& needle) {
    if (needle.empty() || hay.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            char a = hay[i + j];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (a != needle[j]) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool isGenericX11Runtime(const std::string& cls) {
    if (cls.empty()) return true;
    return containsLowerAscii(cls, "java") ||
           containsLowerAscii(cls, "glfw") ||
           containsLowerAscii(cls, "lwjgl") ||
           containsLowerAscii(cls, "prismlauncher") ||
           containsLowerAscii(cls, "modrinth") ||
           containsLowerAscii(cls, "openjdk") ||
           containsLowerAscii(cls, "mojang");
}

bool procLooksLikeMinecraftGame(unsigned long pidVal) {
    if (pidVal == 0) return false;
    std::ifstream cmdFile("/proc/" + std::to_string(pidVal) + "/cmdline", std::ios::binary);
    if (!cmdFile.is_open()) return false;
    std::string raw((std::istreambuf_iterator<char>(cmdFile)), std::istreambuf_iterator<char>());
    if (raw.empty()) return false;
    for (char& ch : raw) {
        if (ch == '\0') ch = ' ';
    }
    const bool hasMinecraftSignal =
        containsLowerAscii(raw, "net.minecraft") ||
        containsLowerAscii(raw, "net.fabricmc") ||
        containsLowerAscii(raw, "net.neoforged") ||
        containsLowerAscii(raw, "com.mojang") ||
        (containsLowerAscii(raw, "lwjgl") && containsLowerAscii(raw, "gamedir"));
    return hasMinecraftSignal;
}

} // namespace

void FocusTracker::runX11Loop() {
    void* libX11 = ::dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (!libX11) return;

    auto pXOpenDisplay = reinterpret_cast<void* (*)(const char*)>(::dlsym(libX11, "XOpenDisplay"));
    auto pXCloseDisplay = reinterpret_cast<int (*)(void*)>(::dlsym(libX11, "XCloseDisplay"));
    auto pXDefaultRootWindow = reinterpret_cast<unsigned long (*)(void*)>(::dlsym(libX11, "XDefaultRootWindow"));
    auto pXConnectionNumber = reinterpret_cast<int (*)(void*)>(::dlsym(libX11, "XConnectionNumber"));
    auto pXInternAtom = reinterpret_cast<unsigned long (*)(void*, const char*, int)>(::dlsym(libX11, "XInternAtom"));
    auto pXGetWindowProperty = reinterpret_cast<int (*)(void*, unsigned long, unsigned long, long, long, int, unsigned long, unsigned long*, int*, unsigned long*, unsigned long*, unsigned char**)>(::dlsym(libX11, "XGetWindowProperty"));
    auto pXGetInputFocus = reinterpret_cast<int (*)(void*, unsigned long*, int*)>(::dlsym(libX11, "XGetInputFocus"));
    auto pXQueryTree = reinterpret_cast<int (*)(void*, unsigned long, unsigned long*, unsigned long*, unsigned long**, unsigned int*)>(::dlsym(libX11, "XQueryTree"));
    auto pXSelectInput = reinterpret_cast<int (*)(void*, unsigned long, long)>(::dlsym(libX11, "XSelectInput"));
    auto pXFree = reinterpret_cast<int (*)(void*)>(::dlsym(libX11, "XFree"));
    auto pXPending = reinterpret_cast<int (*)(void*)>(::dlsym(libX11, "XPending"));
    auto pXNextEvent = reinterpret_cast<int (*)(void*, void*)>(::dlsym(libX11, "XNextEvent"));
    auto pXFlush = reinterpret_cast<int (*)(void*)>(::dlsym(libX11, "XFlush"));
    auto pXSetErrorHandler = reinterpret_cast<int (*(*)(int (*)(void*, void*)))(void*, void*)>(::dlsym(libX11, "XSetErrorHandler"));

    if (!pXOpenDisplay || !pXCloseDisplay || !pXDefaultRootWindow || !pXConnectionNumber ||
        !pXInternAtom || !pXGetWindowProperty || !pXSelectInput || !pXFree ||
        !pXPending || !pXNextEvent || !pXFlush) {
        ::dlclose(libX11);
        return;
    }

    if (pXSetErrorHandler) {
        pXSetErrorHandler(safeFocusXErrorHandler);
    }

    int retryTick = 0;
    while (running_.load()) {
        if (activeBackend_ == DesktopBackend::GnomeShell && !gnomeMonitorActive_ && (retryTick % 4) == 0) {
            if (startGnomeExtension()) {
                gnomeMonitorActive_ = true;
                break;
            }
        }
        ++retryTick;

        std::string dispName = discoverActiveX11Display();
        void* dpy = dispName.empty() ? nullptr : pXOpenDisplay(dispName.c_str());
        if (!dpy && !dispName.empty() && !xwayland_env::isIsolatedTestEnv()) {
            if (xwayland_env::ensureXAuthorityEnv(true)) {
                dpy = pXOpenDisplay(dispName.c_str());
            }
        }
        if (!dpy) {
            if (waitWakeOrTimeout(wakeFd_, 500)) {
                break;
            }
            continue;
        }

        unsigned long root = pXDefaultRootWindow(dpy);
        unsigned long atomActive = pXInternAtom(dpy, "_NET_ACTIVE_WINDOW", 0);
        unsigned long atomWmClass = pXInternAtom(dpy, "WM_CLASS", 0);
        unsigned long atomNetWmName = pXInternAtom(dpy, "_NET_WM_NAME", 0);
        unsigned long atomWmName = pXInternAtom(dpy, "WM_NAME", 0);
        unsigned long atomNetWmPid = pXInternAtom(dpy, "_NET_WM_PID", 0);

        // PropertyChangeMask = (1L << 22)
        pXSelectInput(dpy, root, (1L << 22));
        pXFlush(dpy);

        unsigned long lastWin = 0;
        std::string lastClass;
        std::string lastInst;
        std::string lastCap;
        auto queryAndEmitActive = [&]() {
            g_x11FocusErr.store(false, std::memory_order_relaxed);
            unsigned long actualType = 0;
            int actualFormat = 0;
            unsigned long nitems = 0, bytesAfter = 0;
            unsigned char* prop = nullptr;
            unsigned long activeWin = 0;
            if (pXGetWindowProperty(dpy, root, atomActive, 0, 1, 0, 0 /*AnyPropertyType*/,
                                    &actualType, &actualFormat, &nitems, &bytesAfter, &prop) == 0 && prop) {
                if (actualFormat == 32 && nitems >= 1) {
                    std::memcpy(&activeWin, prop, sizeof(unsigned long));
                }
                pXFree(prop);
            }
            if ((activeWin <= 1 || activeWin == root) && pXGetInputFocus) {
                int revertTo = 0;
                unsigned long focusedWin = 0;
                pXGetInputFocus(dpy, &focusedWin, &revertTo);
                if (focusedWin > 1 && focusedWin != root) {
                    activeWin = focusedWin;
                }
            }
            if (activeWin <= 1 || activeWin == root) {
                if (lastWin != 0 || !lastClass.empty()) {
                    lastWin = 0;
                    lastClass.clear();
                    lastInst.clear();
                    lastCap.clear();
                    emitRawFocus("", "", "", "");
                }
                return;
            }

            auto readWmClass = [&](unsigned long w, std::string& outInst, std::string& outCls) -> bool {
                unsigned char* clsProp = nullptr;
                bool found = false;
                if (pXGetWindowProperty(dpy, w, atomWmClass, 0, 256, 0, 0,
                                        &actualType, &actualFormat, &nitems, &bytesAfter, &clsProp) == 0 && clsProp) {
                    if (actualFormat == 8 && nitems > 0 && clsProp[0] != '\0') {
                        const char* raw = reinterpret_cast<const char*>(clsProp);
                        outInst = std::string(raw, strnlen(raw, nitems));
                        if (outInst.size() + 1 < nitems) {
                            const char* second = raw + outInst.size() + 1;
                            outCls = std::string(second, strnlen(second, nitems - outInst.size() - 1));
                        }
                        found = !outInst.empty() || !outCls.empty();
                    }
                    pXFree(clsProp);
                }
                return found;
            };

            auto readName = [&](unsigned long w, unsigned long atom) -> std::string {
                unsigned char* nProp = nullptr;
                std::string res;
                if (atom && pXGetWindowProperty(dpy, w, atom, 0, 512, 0, 0,
                                                &actualType, &actualFormat, &nitems, &bytesAfter, &nProp) == 0 && nProp) {
                    if (actualFormat == 8 && nitems > 0) {
                        res.assign(reinterpret_cast<const char*>(nProp), strnlen(reinterpret_cast<const char*>(nProp), nitems));
                    }
                    pXFree(nProp);
                }
                return res;
            };

            std::string cls, inst, cap;
            unsigned long infoWin = activeWin;
            for (int depth = 0; depth < 16 && infoWin != 0 && infoWin != root; ++depth) {
                if (readWmClass(infoWin, inst, cls)) {
                    break;
                }
                if (cap.empty()) {
                    cap = readName(infoWin, atomNetWmName);
                    if (cap.empty()) cap = readName(infoWin, atomWmName);
                }
                if (!pXQueryTree) break;
                unsigned long rootRet = 0, parentRet = 0;
                unsigned long* childrenRet = nullptr;
                unsigned int nchildren = 0;
                if (pXQueryTree(dpy, infoWin, &rootRet, &parentRet, &childrenRet, &nchildren) == 0) {
                    break;
                }
                if (childrenRet) pXFree(childrenRet);
                infoWin = parentRet;
            }
            unsigned long capWin = (infoWin != 0 && infoWin != root) ? infoWin : activeWin;
            if (cap.empty()) {
                cap = readName(capWin, atomNetWmName);
                if (cap.empty()) cap = readName(capWin, atomWmName);
                if (cap.empty() && capWin != activeWin) {
                    cap = readName(activeWin, atomNetWmName);
                    if (cap.empty()) cap = readName(activeWin, atomWmName);
                }
            }
            if (atomNetWmPid != 0 && (isGenericX11Runtime(cls) || isGenericX11Runtime(inst))) {
                unsigned char* pidProp = nullptr;
                unsigned long pidWin = (infoWin != 0 && infoWin != root) ? infoWin : activeWin;
                if (pXGetWindowProperty(dpy, pidWin, atomNetWmPid, 0, 1, 0, 0,
                                        &actualType, &actualFormat, &nitems, &bytesAfter, &pidProp) == 0 && pidProp) {
                    if (actualFormat == 32 && nitems >= 1) {
                        unsigned long pidVal = *reinterpret_cast<unsigned long*>(pidProp);
                        if (procLooksLikeMinecraftGame(pidVal)) {
                            inst = "minecraft";
                        } else if (cls.empty() && inst.empty()) {
                            std::ifstream commFile("/proc/" + std::to_string(pidVal) + "/comm");
                            if (commFile.is_open()) {
                                std::getline(commFile, inst);
                            }
                        }
                    }
                    pXFree(pidProp);
                }
            }

            std::string effectiveCls = cls.empty() ? inst : cls;
            if (!g_x11FocusErr.load(std::memory_order_relaxed) &&
                (activeWin != lastWin || effectiveCls != lastClass || inst != lastInst || cap != lastCap)) {
                lastWin = activeWin;
                lastClass = effectiveCls;
                lastInst = inst;
                lastCap = cap;
                emitRawFocus(effectiveCls, inst, cap, std::to_string(activeWin));
            }
        };

        queryAndEmitActive();

        const int pollTimeoutMs = (activeBackend_ == DesktopBackend::GnomeShell) ? 250 : -1;
        int xfd = pXConnectionNumber(dpy);
        struct XEventStub { int type; char pad[192]; } ev{};
        bool wakeTriggered = false;
        int idlePolls = 0;
        while (running_.load()) {
            struct pollfd fds[2]{};
            fds[0].fd = xfd;
            fds[0].events = POLLIN;
            fds[1].fd = wakeFd_;
            fds[1].events = POLLIN;
            int rc = ::poll(fds, (wakeFd_ >= 0) ? 2 : 1, pollTimeoutMs);
            if (rc < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (wakeFd_ >= 0 && (fds[1].revents & POLLIN)) {
                wakeTriggered = true;
                break;
            }
            if (fds[0].revents & (POLLERR | POLLHUP)) {
                break;
            }
            if (rc == 0) {
                ++idlePolls;
                if (activeBackend_ == DesktopBackend::GnomeShell && !gnomeMonitorActive_ && (idlePolls % 8) == 0) {
                    if (startGnomeExtension()) {
                        gnomeMonitorActive_ = true;
                        wakeTriggered = true;
                        break;
                    }
                }
                queryAndEmitActive();
                continue;
            }
            while (pXPending(dpy) > 0 && running_.load()) {
                pXNextEvent(dpy, &ev);
                if (ev.type == 28 /*PropertyNotify*/) {
                    queryAndEmitActive();
                }
            }
        }

        pXCloseDisplay(dpy);
        if (wakeTriggered || !running_.load()) {
            break;
        }
    }

    ::dlclose(libX11);
}

std::string FocusTracker::ensureLocalScriptFile() const {
    const char* sysPath = "/usr/share/vnsnowkey/kwin/vnsnowkey-focus.js";
    if (std::filesystem::exists(sysPath)) {
        return sysPath;
    }
    const char* legacySysPath = "/usr/share/vnsnowkey/kwin-script/vnsnowkey-focus.js";
    if (std::filesystem::exists(legacySysPath)) {
        return legacySysPath;
    }
    std::string dir = Config::defaultConfigDir();
    std::filesystem::create_directories(dir);
    std::string scriptPath = dir + "/vnsnowkey-focus.js";
    std::ofstream ofs(scriptPath, std::ios::trunc);
    if (ofs.is_open()) {
        ofs << kKWinScriptSource;
    }
    return scriptPath;
}

bool FocusTracker::registerKWinScript() {
    std::string scriptPath = ensureLocalScriptFile();
    QDBusInterface iface("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        return false;
    }

    QDBusReply<bool> isLoaded = iface.call("isScriptLoaded", QString::fromUtf8(kKWinPluginName));
    if (isLoaded.isValid() && isLoaded.value()) {
        iface.call("unloadScript", QString::fromUtf8(kKWinPluginName));
    }

    QDBusReply<int> loadReply = iface.call(
        "loadScript",
        QString::fromStdString(scriptPath),
        QString::fromUtf8(kKWinPluginName));
    if (!loadReply.isValid() || loadReply.value() < 0) {
        return false;
    }

    iface.call("start");
    scriptRegistered_ = true;
    return true;
}

void FocusTracker::unregisterKWinScript() {
    if (!scriptRegistered_) return;
    QDBusInterface iface("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
    if (iface.isValid()) {
        iface.call("unloadScript", QString::fromUtf8(kKWinPluginName));
    }
    scriptRegistered_ = false;
}

bool FocusTracker::isKWinScriptLoaded() const {
    QDBusInterface iface("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
    if (!iface.isValid()) return false;
    QDBusReply<bool> isLoaded = iface.call("isScriptLoaded", QString::fromUtf8(kKWinPluginName));
    return isLoaded.isValid() && isLoaded.value();
}

bool FocusTracker::detectPasswordDialog(
    const std::string& appClass, const std::string& appName, const std::string& caption) {
    std::string lcClass = toLowerAscii(appClass);
    std::string lcName = toLowerAscii(appName);
    std::string lcCap = toLowerAscii(caption);

    static const char* kAuthClasses[] = {
        "polkit-kde-authentication-agent",
        "polkit-gnome-authentication-agent",
        "ksshaskpass",
        "pinentry",
        "gcr-prompter",
        "keepassxc",
        "1password",
        "bitwarden",
        "kwalletd",
        "gnome-keyring",
    };
    for (const char* auth : kAuthClasses) {
        if (lcClass.find(auth) != std::string::npos || lcName.find(auth) != std::string::npos) {
            return true;
        }
    }

    static const char* kAuthCaptions[] = {
        "authentication required",
        "unlock keyring",
        "enter password",
        "ssh passphrase",
        "pinentry",
        "xác thực",
        "nhập mật khẩu",
    };
    for (const char* cap : kAuthCaptions) {
        if (lcCap.find(cap) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void FocusTracker::onWindowFocusChanged(
    const std::string& appClass,
    const std::string& appName,
    const std::string& caption,
    const std::string& windowId,
    const Config& config,
    bool currentVietnameseEnabled) {
    WindowFocusInfo info;
    info.appClass = appClass.empty() ? appName : appClass;
    info.appName = appName;
    info.caption = caption;
    info.windowId = windowId;
    info.isPasswordDialog = detectPasswordDialog(appClass, appName, caption);

    bool newVie = currentVietnameseEnabled;
    bool langChanged = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!info.appClass.empty()) {
            recentApps_.insert(info.appClass);
        }
        currentFocus_ = info;

        const AppProfile* prof = config.findProfileForApp(info.appClass);
        if (config.bypassPasswordDialogs && info.isPasswordDialog) {
            newVie = false;
        } else if (prof) {
            if (prof->mode == AppMode::AlwaysVi) {
                newVie = true;
            } else if (prof->mode == AppMode::AlwaysEn || prof->mode == AppMode::Excluded) {
                newVie = false;
            } else if (config.rememberPerAppLang && !info.appClass.empty()) {
                auto it = perAppLangMemory_.find(info.appClass);
                if (it != perAppLangMemory_.end()) {
                    newVie = it->second;
                } else {
                    perAppLangMemory_[info.appClass] = currentVietnameseEnabled;
                }
            }
        } else if (config.rememberPerAppLang && !info.appClass.empty()) {
            auto it = perAppLangMemory_.find(info.appClass);
            if (it != perAppLangMemory_.end()) {
                newVie = it->second;
            } else {
                perAppLangMemory_[info.appClass] = currentVietnameseEnabled;
            }
        }
        langChanged = (newVie != currentVietnameseEnabled);
    }

    if (onFocusChanged_) {
        onFocusChanged_(info, newVie, langChanged);
    }
}

void FocusTracker::recordAppLanguageState(bool vietnameseEnabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!currentFocus_.appClass.empty()) {
        perAppLangMemory_[currentFocus_.appClass] = vietnameseEnabled;
    }
}

WindowFocusInfo FocusTracker::currentFocus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentFocus_;
}

std::vector<std::string> FocusTracker::recentAppClasses() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<std::string>(recentApps_.begin(), recentApps_.end());
}

} // namespace vnsnowkey
