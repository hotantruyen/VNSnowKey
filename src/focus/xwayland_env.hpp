/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <unistd.h>

namespace vnsnowkey::xwayland_env {

inline bool isIsolatedTestEnv() {
    if (std::getenv("VNSNOWKEY_ONLY_DEVICES") != nullptr ||
        std::getenv("VNSNOWKEY_TEST_IN_FD") != nullptr ||
        std::getenv("VNSNOWKEY_TEST_OUT_FD") != nullptr ||
        std::getenv("VNSNOWKEY_ISOLATED_TEST") != nullptr) {
        return true;
    }
    const char* qpa = std::getenv("QT_QPA_PLATFORM");
    return qpa != nullptr && std::string_view(qpa) == "offscreen";
}

inline std::string resolveRuntimeDir() {
    if (const char* rt = std::getenv("XDG_RUNTIME_DIR"); rt && rt[0] != '\0') {
        return std::string(rt);
    }
    return "/run/user/" + std::to_string(static_cast<unsigned long>(::geteuid()));
}

inline std::string findNewestReadableFile(const std::string& dirPath, std::string_view prefix) {
    if (dirPath.empty()) return {};
    std::string bestPath;
    std::filesystem::file_time_type bestTime{};
    bool hasBest = false;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string fname = entry.path().filename().string();
        if (fname.rfind(prefix, 0) != 0) continue;
        std::string full = entry.path().string();
        if (::access(full.c_str(), R_OK) != 0) continue;

        auto mtime = entry.last_write_time(ec);
        if (ec) continue;
        if (!hasBest || mtime > bestTime) {
            bestTime = mtime;
            bestPath = std::move(full);
            hasBest = true;
        }
    }
    return bestPath;
}

inline std::string findBestXAuthorityFile(const std::string& runtimeDir, const std::string& homeDir) {
    std::string mutterAuth = findNewestReadableFile(runtimeDir, ".mutter-Xwaylandauth.");
    if (!mutterAuth.empty()) {
        return mutterAuth;
    }
    if (!homeDir.empty()) {
        std::string homeAuth = homeDir + "/.Xauthority";
        if (::access(homeAuth.c_str(), R_OK) == 0) {
            return homeAuth;
        }
    }
    return {};
}

inline bool ensureXAuthorityEnv(bool forceRescan = false) {
    if (!forceRescan) {
        if (const char* cur = std::getenv("XAUTHORITY"); cur && cur[0] != '\0') {
            if (::access(cur, R_OK) == 0) {
                return true;
            }
        }
    }
    const char* home = std::getenv("HOME");
    std::string candidate = findBestXAuthorityFile(resolveRuntimeDir(), home ? std::string(home) : std::string());
    if (!candidate.empty()) {
        ::setenv("XAUTHORITY", candidate.c_str(), 1);
        return true;
    }
    return false;
}

inline std::string discoverWaylandSocketPath() {
    const std::string runtimeDir = resolveRuntimeDir();
    if (runtimeDir.empty()) return {};

    if (const char* waylandDisp = std::getenv("WAYLAND_DISPLAY"); waylandDisp && waylandDisp[0] != '\0') {
        std::string sock = (waylandDisp[0] == '/')
                               ? std::string(waylandDisp)
                               : (runtimeDir + "/" + waylandDisp);
        if (::access(sock.c_str(), F_OK) == 0) {
            return sock;
        }
        return {};
    }

    for (int i = 0; i <= 2; ++i) {
        std::string sock = runtimeDir + "/wayland-" + std::to_string(i);
        if (::access(sock.c_str(), F_OK) == 0) {
            return sock;
        }
    }
    return {};
}

inline std::string discoverX11SocketDisplay(const std::string& x11SocketDir = "/tmp/.X11-unix") {
    for (int num = 0; num <= 9; ++num) {
        std::string xSock = x11SocketDir + "/X" + std::to_string(num);
        if (::access(xSock.c_str(), F_OK) == 0) {
            return ":" + std::to_string(num);
        }
    }
    return {};
}

inline std::string resolveLiveX11Display() {
    if (isIsolatedTestEnv()) {
        const char* disp = std::getenv("DISPLAY");
        return (disp && disp[0] != '\0') ? std::string(disp) : std::string();
    }

    // Refresh XAUTHORITY if systemd --user started vnsnowkeyd before Mutter spawned on-demand XWayland.
    const bool hasAuth = ensureXAuthorityEnv();

    if (const char* disp = std::getenv("DISPLAY"); disp && disp[0] != '\0') {
        return std::string(disp);
    }

    if (discoverWaylandSocketPath().empty() || !hasAuth) {
        return {};
    }

    return discoverX11SocketDisplay();
}

} // namespace vnsnowkey::xwayland_env
