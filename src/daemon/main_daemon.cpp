/*
 * VNSnowKey - Vietnamese Input Method Daemon (vnsnowkeyd)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "daemon_server.hpp"
#include <QCoreApplication>
#include <QSocketNotifier>
#include <csignal>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int g_sigPipe[2] = {-1, -1};
vnsnowkey::DaemonServer* g_daemonPtr = nullptr;

void posixSignalHandler(int sig) {
    char b = static_cast<char>(sig);
    if (g_sigPipe[1] >= 0) {
        ssize_t unused = ::write(g_sigPipe[1], &b, 1);
        (void)unused;
    }
}

void crashSignalHandler(int sig) {
    // Emergency cleanup: release all virtual keys and ungrab physical keyboards immediately
    if (g_daemonPtr) {
        g_daemonPtr->stop();
    }
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void setupSignalHandlers() {
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, g_sigPipe) == 0) {
        struct sigaction sa{};
        sa.sa_handler = posixSignalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGINT,  &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGHUP,  &sa, nullptr);
        sigaction(SIGQUIT, &sa, nullptr);
    }

    struct sigaction saCrash{};
    saCrash.sa_handler = crashSignalHandler;
    sigemptyset(&saCrash.sa_mask);
    saCrash.sa_flags = SA_RESETHAND;
    sigaction(SIGSEGV, &saCrash, nullptr);
    sigaction(SIGABRT, &saCrash, nullptr);
    sigaction(SIGBUS,  &saCrash, nullptr);
    sigaction(SIGFPE,  &saCrash, nullptr);
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("vnsnowkeyd");
    app.setApplicationVersion("1.0.6");

    std::string configPath;
    bool noGrab = false;
    bool printDoctor = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--no-grab") {
            noGrab = true;
        } else if (arg == "--doctor") {
            printDoctor = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "VNSnowKey Daemon (vnsnowkeyd) v1.0.6\n"
                      << "Usage: vnsnowkeyd [--config <path>] [--no-grab] [--doctor]\n";
            return 0;
        }
    }

    if (printDoctor) {
        std::cout << vnsnowkey::DaemonServer::buildDiagnosticsReport();
        return 0;
    }

    setupSignalHandlers();

    vnsnowkey::DaemonServer server(configPath, noGrab);
    g_daemonPtr = &server;

    QSocketNotifier* sigNotifier = nullptr;
    if (g_sigPipe[0] >= 0) {
        sigNotifier = new QSocketNotifier(g_sigPipe[0], QSocketNotifier::Read, &app);
        QObject::connect(sigNotifier, &QSocketNotifier::activated, &app, [&]() {
            char sig = 0;
            ssize_t unused = ::read(g_sigPipe[0], &sig, 1);
            (void)unused;
            if (sig == SIGHUP) {
                server.ReloadConfig();
            } else {
                server.Quit();
            }
        });
    }

    std::string err;
    if (!server.start(&err)) {
        std::cerr << "[vnsnowkeyd] Failed to start: " << err << "\n";
        return 1;
    }

    int rc = app.exec();
    server.stop();
    g_daemonPtr = nullptr;
    if (g_sigPipe[0] >= 0) ::close(g_sigPipe[0]);
    if (g_sigPipe[1] >= 0) ::close(g_sigPipe[1]);
    return rc;
}
