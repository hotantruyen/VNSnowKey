/*
 * VNSnowKey CLI Control Tool (vnsnowkey)
 * Usage:
 *   vnsnowkey ctl toggle|vi|en|status|reload|panic|resume|doctor|reapply-xkb
 *   vnsnowkey ctl im <Telex|Simple Telex|VNI|VIQR|Telex 2>
 *   vnsnowkey ctl charset <Unicode|TCVN3 (ABC)|VNI Windows|Unicode tổ hợp|VIQR>
 *   vnsnowkey ctl convert [--from <src>] [--to <dst>] [--strip] [text]
 *   vnsnowkey ctl convert-clipboard [--from <src>] [--to <dst>] [--strip]
 *   vnsnowkey ctl backup <path>
 *   vnsnowkey ctl restore <path>
 */

#include "../config/config.hpp"
#include "../daemon/daemon_server.hpp"
#include "../engine/charset_encoder.hpp"
#include "../output/uinput_emitter.hpp"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void printUsage() {
    std::cout << "VNSnowKey CLI v1.0.3 — Bộ gõ tiếng Việt cho Linux (Wayland & X11)\n\n"
              << "Cách dùng:\n"
              << "  vnsnowkey ctl toggle                 Đổi nhanh chế độ Tiếng Việt (VI) / Tiếng Anh (EN)\n"
              << "  vnsnowkey ctl vi                     Chuyển sang chế độ Tiếng Việt (VI)\n"
              << "  vnsnowkey ctl en                     Chuyển sang chế độ Tiếng Anh (EN)\n"
              << "  vnsnowkey ctl status                 Hiển thị trạng thái hiện tại của daemon (JSON)\n"
              << "  vnsnowkey ctl reload                 Tải lại cấu hình ~/.config/vnsnowkey/config.toml ngay lập tức\n"
              << "  vnsnowkey ctl panic                  Bật Chế độ An toàn (nhả grab bàn phím ngay lập tức)\n"
              << "  vnsnowkey ctl resume                 Thoát Chế độ An toàn và tiếp tục gõ\n"
              << "  vnsnowkey ctl im <name>              Đổi kiểu gõ (Telex, Simple Telex, VNI, VIQR, Telex 2)\n"
              << "  vnsnowkey ctl charset <name>         Đổi bảng mã (Unicode, TCVN3 (ABC), VNI Windows, ...)\n"
              << "  vnsnowkey ctl reapply-xkb            Cài đặt lại XKB Virtual Keymap và nạp lại compositor\n"
              << "  vnsnowkey ctl convert-clipboard [--from <src>] [--to <dst>] [--strip]\n"
              << "                                     Chuyển mã nội dung Clipboard hiện tại\n"
              << "  vnsnowkey ctl convert [--from <src>] [--to <dst>] [--strip] <text>\n"
              << "                                     Chuyển mã chuỗi văn bản trực tiếp\n"
              << "  vnsnowkey ctl backup <file.toml>     Sao lưu cấu hình ra file\n"
              << "  vnsnowkey ctl restore <file.toml>    Khôi phục cấu hình từ file và áp dụng ngay\n"
              << "  vnsnowkey ctl doctor                 Chẩn đoán quyền uinput/evdev, Wayland/X11, Focus, XKB\n";
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        printUsage();
        return 0;
    }

    int idx = 1;
    std::string sub = argv[idx];
    if (sub == "ctl" && idx + 1 < argc) {
        ++idx;
        sub = argv[idx];
    }

    if (sub == "--help" || sub == "-h" || sub == "help") {
        printUsage();
        return 0;
    }

    // Offline/direct commands that work even if daemon is not running
    if (sub == "convert" || sub == "convert-clipboard") {
        std::string fromCs = vnsnowkey::bamboo::CHARSET_UNICODE;
        std::string toCs = vnsnowkey::bamboo::CHARSET_UNICODE;
        bool strip = false;
        std::string inputText;

        for (int i = idx + 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--from" && i + 1 < argc) {
                fromCs = argv[++i];
            } else if (arg == "--to" && i + 1 < argc) {
                toCs = argv[++i];
            } else if (arg == "--strip" || arg == "--strip-diacritics") {
                strip = true;
            } else {
                if (!inputText.empty()) inputText += " ";
                inputText += arg;
            }
        }

        if (sub == "convert-clipboard") {
            if (!vnsnowkey::UinputEmitter::readSystemClipboard(inputText)) {
                QDBusInterface klipper("org.kde.klipper", "/klipper", "org.kde.klipper.klipper", QDBusConnection::sessionBus());
                if (klipper.isValid()) {
                    QDBusReply<QString> rep = klipper.call("getClipboardContents");
                    if (rep.isValid()) inputText = rep.value().toStdString();
                }
            }
            std::string converted = vnsnowkey::bamboo::convertCharset(fromCs, toCs, inputText, strip);
            if (!vnsnowkey::UinputEmitter::writeSystemClipboard(converted)) {
                QDBusInterface klipper("org.kde.klipper", "/klipper", "org.kde.klipper.klipper", QDBusConnection::sessionBus());
                if (klipper.isValid()) klipper.call("setClipboardContents", QString::fromStdString(converted));
            }
            std::cout << converted << "\n";
            return 0;
        } else {
            std::string converted = vnsnowkey::bamboo::convertCharset(fromCs, toCs, inputText, strip);
            std::cout << converted << "\n";
            return 0;
        }
    }

    if (sub == "backup") {
        if (idx + 1 >= argc) {
            std::cerr << "Lỗi: Thiếu đường dẫn file sao lưu.\n";
            return 1;
        }
        std::string dest = argv[idx + 1];
        vnsnowkey::Config cfg = vnsnowkey::Config::loadFromFile();
        std::string err;
        if (!cfg.saveToFile(dest, &err)) {
            std::cerr << "Lỗi sao lưu cấu hình: " << err << "\n";
            return 1;
        }
        std::cout << "Đã sao lưu cấu hình thành công vào: " << dest << "\n";
        return 0;
    }

    if (sub == "restore") {
        if (idx + 1 >= argc) {
            std::cerr << "Lỗi: Thiếu đường dẫn file khôi phục.\n";
            return 1;
        }
        std::string src = argv[idx + 1];
        bool ok = false;
        vnsnowkey::Config cfg = vnsnowkey::Config::loadFromFile(src, &ok);
        if (!ok) {
            std::cerr << "Lỗi: Không thể đọc file cấu hình: " << src << "\n";
            return 1;
        }
        cfg.saveToFile();
        QDBusInterface iface("org.vnsnowkey.Daemon", "/org/vnsnowkey/Daemon", "org.vnsnowkey.Daemon", QDBusConnection::sessionBus());
        if (iface.isValid()) {
            iface.call("ReloadConfig");
        }
        std::cout << "Đã khôi phục và áp dụng cấu hình từ: " << src << "\n";
        return 0;
    }

    QDBusInterface iface("org.vnsnowkey.Daemon", "/org/vnsnowkey/Daemon", "org.vnsnowkey.Daemon", QDBusConnection::sessionBus());

    if (sub == "doctor" || sub == "diagnostics") {
        if (iface.isValid()) {
            QDBusReply<QString> rep = iface.call("RunDiagnostics");
            if (rep.isValid()) {
                std::cout << rep.value().toStdString();
                return 0;
            }
        }
        std::cout << vnsnowkey::DaemonServer::buildDiagnosticsReport();
        return 0;
    }

    if (!iface.isValid()) {
        std::cerr << "Lỗi: Không kết nối được tới vnsnowkeyd qua D-Bus (org.vnsnowkey.Daemon).\n"
                  << "Hãy kiểm tra service bằng lệnh: systemctl --user status vnsnowkeyd.service\n";
        return 1;
    }

    if (sub == "toggle") {
        QDBusReply<bool> rep = iface.call("Toggle");
        if (rep.isValid()) {
            std::cout << (rep.value() ? "VI" : "EN") << "\n";
            return 0;
        }
    } else if (sub == "vi") {
        QDBusReply<bool> rep = iface.call("SetVietnamese", true);
        if (rep.isValid()) {
            std::cout << "VI\n";
            return 0;
        }
    } else if (sub == "en") {
        QDBusReply<bool> rep = iface.call("SetVietnamese", false);
        if (rep.isValid()) {
            std::cout << "EN\n";
            return 0;
        }
    } else if (sub == "status") {
        QDBusReply<QString> rep = iface.call("GetStatusJson");
        if (rep.isValid()) {
            std::cout << rep.value().toStdString() << "\n";
            return 0;
        }
    } else if (sub == "reload") {
        QDBusReply<bool> rep = iface.call("ReloadConfig");
        if (rep.isValid()) {
            std::cout << "Reloaded\n";
            return 0;
        }
    } else if (sub == "panic" || sub == "safe-mode") {
        QDBusReply<bool> rep = iface.call("SetSafeMode", true);
        if (rep.isValid()) {
            std::cout << "SafeMode: ON (Keyboards ungrabbed)\n";
            return 0;
        }
    } else if (sub == "resume") {
        QDBusReply<bool> rep = iface.call("SetSafeMode", false);
        if (rep.isValid()) {
            std::cout << "SafeMode: OFF\n";
            return 0;
        }
    } else if (sub == "reapply-xkb") {
        QDBusReply<bool> rep = iface.call("ReapplyXkbOverlay");
        if (rep.isValid()) {
            std::cout << (rep.value() ? "XKB Overlay applied and KWin reloaded.\n" : "Failed to apply XKB Overlay.\n");
            return rep.value() ? 0 : 1;
        }
    } else if (sub == "im" && idx + 1 < argc) {
        QString imName = QString::fromUtf8(argv[idx + 1]);
        QDBusReply<bool> rep = iface.call("SetInputMethod", imName);
        if (rep.isValid()) {
            std::cout << "InputMethod: " << imName.toStdString() << "\n";
            return 0;
        }
    } else if (sub == "charset" && idx + 1 < argc) {
        QString csName = QString::fromUtf8(argv[idx + 1]);
        QDBusReply<bool> rep = iface.call("SetOutputCharset", csName);
        if (rep.isValid()) {
            std::cout << "Charset: " << csName.toStdString() << "\n";
            return 0;
        }
    } else if (sub == "quit") {
        iface.call("Quit");
        return 0;
    } else {
        std::cerr << "Lệnh không hợp lệ: " << sub << "\n";
        printUsage();
        return 1;
    }

    std::cerr << "Lỗi khi gọi phương thức D-Bus: " << iface.lastError().message().toStdString() << "\n";
    return 1;
}
