/*
 * VNSnowKey - Wayland Input Method Bridge for KDE Plasma (zwp_input_method_v1)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Minimal Virtual Keyboard client launched by KWin when VNSnowKey is selected under
 * System Settings -> Keyboard -> Virtual Keyboard. It never grabs the keyboard and
 * never creates an on-screen input panel; it only observes KWin text-input
 * activation/deactivation events and forwards them to vnsnowkeyd over D-Bus so
 * Game Mode can toggle between 3D raw pass-through and Vietnamese chat typing.
 */

#include "input-method-unstable-v1-client-protocol.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <systemd/sd-bus.h>
#include <unistd.h>
#include <wayland-client.h>

namespace {

volatile sig_atomic_t g_shouldQuit = 0;
int g_wakePipe[2] = {-1, -1};

void handleSignal(int /*sig*/) {
    g_shouldQuit = 1;
    if (g_wakePipe[1] >= 0) {
        char b = 1;
        ssize_t n = ::write(g_wakePipe[1], &b, 1);
        (void)n;
    }
}

struct BridgeState {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    zwp_input_method_v1* inputMethod = nullptr;
    zwp_input_method_context_v1* activeContext = nullptr;
    sd_bus* bus = nullptr;
    bool lastSentValid = false;
    bool lastSentActive = false;
};

int onDbusReplyNoop(sd_bus_message* /*m*/, void* /*userdata*/, sd_bus_error* /*err*/) {
    return 0;
}

void sendDaemonStateAsync(BridgeState* st, bool textInputActive, bool bridgeAlive) {
    if (!st || !st->bus) {
        return;
    }
    if (bridgeAlive && st->lastSentValid && st->lastSentActive == textInputActive) {
        return;
    }
    int r = sd_bus_call_method_async(
        st->bus,
        nullptr,
        "org.vnsnowkey.Daemon",
        "/org/vnsnowkey/Daemon",
        "org.vnsnowkey.Daemon",
        "NotifyInputMethodState",
        onDbusReplyNoop,
        nullptr,
        "bb",
        textInputActive ? 1 : 0,
        bridgeAlive ? 1 : 0);
    if (r >= 0) {
        sd_bus_flush(st->bus);
        st->lastSentValid = bridgeAlive;
        st->lastSentActive = textInputActive;
    }
}

void sendDaemonStateSyncOnExit(BridgeState* st) {
    if (!st || !st->bus) {
        return;
    }
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    (void)sd_bus_call_method(
        st->bus,
        "org.vnsnowkey.Daemon",
        "/org/vnsnowkey/Daemon",
        "org.vnsnowkey.Daemon",
        "NotifyInputMethodState",
        &err,
        &reply,
        "bb",
        0,
        0);
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    sd_bus_flush(st->bus);
}

// No-op context listeners to keep Wayland event dispatch clean.
void onCtxSurroundingText(void* /*data*/, zwp_input_method_context_v1* /*ctx*/, const char* /*text*/, uint32_t /*cursor*/, uint32_t /*anchor*/) {}
void onCtxReset(void* /*data*/, zwp_input_method_context_v1* /*ctx*/) {}
void onCtxContentType(void* /*data*/, zwp_input_method_context_v1* /*ctx*/, uint32_t /*hint*/, uint32_t /*purpose*/) {}
void onCtxInvokeAction(void* /*data*/, zwp_input_method_context_v1* /*ctx*/, uint32_t /*button*/, uint32_t /*index*/) {}
void onCtxCommitState(void* /*data*/, zwp_input_method_context_v1* /*ctx*/, uint32_t /*serial*/) {}
void onCtxPreferredLanguage(void* /*data*/, zwp_input_method_context_v1* /*ctx*/, const char* /*language*/) {}

constexpr zwp_input_method_context_v1_listener kContextListener = {
    onCtxSurroundingText,
    onCtxReset,
    onCtxContentType,
    onCtxInvokeAction,
    onCtxCommitState,
    onCtxPreferredLanguage,
};

void onImActivate(void* data, zwp_input_method_v1* /*im*/, zwp_input_method_context_v1* id) {
    auto* st = static_cast<BridgeState*>(data);
    if (!st || !id) {
        return;
    }
    zwp_input_method_context_v1_add_listener(id, &kContextListener, st);
    st->activeContext = id;
    sendDaemonStateAsync(st, true, true);
}

void onImDeactivate(void* data, zwp_input_method_v1* /*im*/, zwp_input_method_context_v1* context) {
    auto* st = static_cast<BridgeState*>(data);
    if (!st || !context) {
        return;
    }
    bool wasCurrent = (st->activeContext == context);
    if (wasCurrent) {
        st->activeContext = nullptr;
    }
    zwp_input_method_context_v1_destroy(context);
    if (wasCurrent) {
        sendDaemonStateAsync(st, false, true);
    }
}

constexpr zwp_input_method_v1_listener kInputMethodListener = {
    onImActivate,
    onImDeactivate,
};

void onRegistryGlobal(
    void* data,
    wl_registry* registry,
    uint32_t name,
    const char* interface,
    uint32_t /*version*/) {
    auto* st = static_cast<BridgeState*>(data);
    if (!st || !interface) {
        return;
    }
    if (std::strcmp(interface, zwp_input_method_v1_interface.name) == 0 && !st->inputMethod) {
        st->inputMethod = static_cast<zwp_input_method_v1*>(
            wl_registry_bind(registry, name, &zwp_input_method_v1_interface, 1));
        if (st->inputMethod) {
            zwp_input_method_v1_add_listener(st->inputMethod, &kInputMethodListener, st);
        }
    }
}

void onRegistryGlobalRemove(void* /*data*/, wl_registry* /*registry*/, uint32_t /*name*/) {}

constexpr wl_registry_listener kRegistryListener = {
    onRegistryGlobal,
    onRegistryGlobalRemove,
};

int onDaemonNameOwnerChanged(sd_bus_message* m, void* userdata, sd_bus_error* /*err*/) {
    auto* st = static_cast<BridgeState*>(userdata);
    if (!st || !m) {
        return 0;
    }
    const char* name = nullptr;
    const char* oldOwner = nullptr;
    const char* newOwner = nullptr;
    if (sd_bus_message_read(m, "sss", &name, &oldOwner, &newOwner) >= 0) {
        if (newOwner && newOwner[0] != '\0') {
            // Daemon appeared or restarted: push current live state unconditionally.
            st->lastSentValid = false;
            sendDaemonStateAsync(st, st->activeContext != nullptr, true);
        }
    }
    return 0;
}

bool setupWakePipe() {
    if (::pipe2(g_wakePipe, O_NONBLOCK | O_CLOEXEC) != 0) {
        return false;
    }
    struct sigaction sa {};
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
    std::signal(SIGPIPE, SIG_IGN);
    return true;
}

void teardownWakePipe() {
    if (g_wakePipe[0] >= 0) {
        ::close(g_wakePipe[0]);
        g_wakePipe[0] = -1;
    }
    if (g_wakePipe[1] >= 0) {
        ::close(g_wakePipe[1]);
        g_wakePipe[1] = -1;
    }
}

void cleanupBridge(BridgeState& st) {
    if (st.activeContext) {
        zwp_input_method_context_v1_destroy(st.activeContext);
        st.activeContext = nullptr;
    }
    if (st.inputMethod) {
        zwp_input_method_v1_destroy(st.inputMethod);
        st.inputMethod = nullptr;
    }
    if (st.registry) {
        wl_registry_destroy(st.registry);
        st.registry = nullptr;
    }
    if (st.display) {
        wl_display_disconnect(st.display);
        st.display = nullptr;
    }
    if (st.bus) {
        sd_bus_unref(st.bus);
        st.bus = nullptr;
    }
}

} // namespace

int main() {
    setupWakePipe();

    BridgeState st;
    st.display = wl_display_connect(nullptr);
    if (!st.display) {
        std::fprintf(stderr, "vnsnowkey-im: failed to connect to Wayland display (WAYLAND_SOCKET/WAYLAND_DISPLAY)\n");
        teardownWakePipe();
        return 1;
    }

    st.registry = wl_display_get_registry(st.display);
    wl_registry_add_listener(st.registry, &kRegistryListener, &st);
    wl_display_roundtrip(st.display);

    if (!st.inputMethod) {
        std::fprintf(
            stderr,
            "vnsnowkey-im: zwp_input_method_v1 is not available on this compositor socket "
            "(enable VNSnowKey in KDE System Settings -> Keyboard -> Virtual Keyboard).\n");
        cleanupBridge(st);
        teardownWakePipe();
        return 1;
    }

    if (sd_bus_open_user(&st.bus) < 0) {
        std::fprintf(stderr, "vnsnowkey-im: warning: failed to open user D-Bus session\n");
        st.bus = nullptr;
    } else {
        sd_bus_add_match(
            st.bus,
            nullptr,
            "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
            "member='NameOwnerChanged',arg0='org.vnsnowkey.Daemon'",
            onDaemonNameOwnerChanged,
            &st);
        sendDaemonStateAsync(&st, st.activeContext != nullptr, true);
    }

    while (!g_shouldQuit) {
        // Dispatch all pending Wayland events before sleeping.
        while (wl_display_prepare_read(st.display) != 0) {
            if (wl_display_dispatch_pending(st.display) < 0) {
                g_shouldQuit = 1;
                break;
            }
        }
        if (g_shouldQuit) {
            wl_display_cancel_read(st.display);
            break;
        }
        if (wl_display_flush(st.display) < 0 && errno != EAGAIN) {
            wl_display_cancel_read(st.display);
            break;
        }

        // Process any already-queued D-Bus messages before computing poll timeout.
        if (st.bus) {
            while (sd_bus_process(st.bus, nullptr) > 0) {}
        }

        struct pollfd fds[3]{};
        nfds_t nfds = 0;

        fds[nfds].fd = wl_display_get_fd(st.display);
        fds[nfds].events = POLLIN;
        nfds_t wlIdx = nfds++;

        nfds_t busIdx = static_cast<nfds_t>(-1);
        int pollTimeoutMs = -1;
        if (st.bus) {
            int busFd = sd_bus_get_fd(st.bus);
            if (busFd >= 0) {
                fds[nfds].fd = busFd;
                fds[nfds].events = static_cast<short>(sd_bus_get_events(st.bus));
                busIdx = nfds++;
                uint64_t usec = 0;
                if (sd_bus_get_timeout(st.bus, &usec) > 0 && usec != UINT64_MAX) {
                    uint64_t ms = usec / 1000;
                    pollTimeoutMs = ms > 10000 ? 10000 : static_cast<int>(ms);
                }
            }
        }

        nfds_t wakeIdx = static_cast<nfds_t>(-1);
        if (g_wakePipe[0] >= 0) {
            fds[nfds].fd = g_wakePipe[0];
            fds[nfds].events = POLLIN;
            wakeIdx = nfds++;
        }

        int rc = ::poll(fds, nfds, pollTimeoutMs);
        if (rc < 0 && errno == EINTR) {
            wl_display_cancel_read(st.display);
            continue;
        }
        if (rc < 0) {
            wl_display_cancel_read(st.display);
            break;
        }

        if (wakeIdx < nfds && (fds[wakeIdx].revents & POLLIN)) {
            char drain[16];
            while (::read(g_wakePipe[0], drain, sizeof(drain)) > 0) {}
        }

        if (fds[wlIdx].revents & POLLIN) {
            if (wl_display_read_events(st.display) < 0) {
                break;
            }
            if (wl_display_dispatch_pending(st.display) < 0) {
                break;
            }
        } else {
            wl_display_cancel_read(st.display);
            if (fds[wlIdx].revents & (POLLERR | POLLHUP)) {
                break;
            }
        }

        if (busIdx < nfds && st.bus) {
            while (sd_bus_process(st.bus, nullptr) > 0) {}
        }
    }

    sendDaemonStateSyncOnExit(&st);
    cleanupBridge(st);
    teardownWakePipe();
    return 0;
}
