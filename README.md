# VNSnowKey — Bộ gõ Tiếng Việt cho Linux (Wayland & X11)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

**VNSnowKey** là bộ gõ tiếng Việt mã nguồn mở dành cho Linux, hỗ trợ **KDE Plasma, GNOME Shell, Hyprland, Sway và X11** trên **Arch Linux, Ubuntu/Debian, Fedora và openSUSE**.

VNSnowKey đọc phím vật lý trực tiếp từ nhân Linux qua **`evdev` (`EVIOCGRAB`)**, xử lý âm tiết bằng lõi **BambooEngine (`bamboo-core`)**, và phát tổ hợp `Backspace` + ký tự tiếng Việt qua bàn phím ảo **`uinput`** kết hợp **XKB 8-Level Vietnamese Keymap Overlay** (độ trễ gần bằng 0, không nháy chữ, không làm bẩn Clipboard ở chế độ mặc định).

---

## Tính năng nổi bật

- **Hoạt động đa môi trường (Wayland & X11)**: Wayland native (Qt6, GTK3/4), XWayland, Electron/Chromium, Firefox (kể cả thanh địa chỉ có autocomplete), Konsole, GNOME Terminal/Ptyxis, Alacritty, Kitty, VS Code, LibreOffice, Telegram.
- **Lõi xử lý tiếng Việt chuẩn BambooEngine (`bamboo-core`)**:
  - Kiểu gõ: **Telex, Simple Telex, VNI, VIQR, Telex 2, Telex W, Telex + VNI, Microsoft layout**.
  - Bảng mã đầu ra: **Unicode (mặc định), TCVN3 (ABC), VNI Windows, Unicode tổ hợp, VIQR**.
  - Hỗ trợ Backtrack dấu (`as` → `á`, `f` → `à`, `z` → `a`), sửa chữ giữ nguyên word buffer theo `Backspace` và phím mũi tên `←` / `→`, đặt dấu kiểu mới (`hoà`, `thuỷ`) / kiểu cũ (`hòa`, `thủy`), kiểm tra chính tả & tự trả từ tiếng Anh (`windows`, `search`, `archlinux`).
  - Gõ tắt phụ âm đầu (`cc→ch, gg→gi, kk→kh, nn→ng, qq→qu, pp→ph, tt→th`), phụ âm cuối (`g→ng, h→nh, k→ch`), bảng **Macro** và công cụ chuyển mã trực tiếp / Clipboard.
- **Game Mode (`game_mode`)**:
  - Tự động giữ nguyên phím di chuyển (`W/A/S/D`) trong game 3D (như Minecraft trên Wayland/XWayland) và chỉ bật gõ tiếng Việt khi mở khung chat (`T`/`/` hoặc khi con trỏ/input mở).
- **An toàn, không nháy chữ, không kẹt phím**:
  - Gom `Backspace` và ký tự thay thế trong cùng khung `SYN_REPORT`; chờ nhả phím (`EVIOCGKEY`) trước khi grab và tự nhả `EVIOCGRAB` ngay khi `SIGTERM`/`SIGINT` hoặc vào `Safe Mode`.
  - Hỗ trợ ngắt/bảo vệ từ an toàn theo click chuột (`reset_on_mouse_click`), đổi cửa sổ (`reset_on_focus_change`), phím điều hướng (`reset_on_navigation_keys`) và hết hạn đệm (`buffer_timeout_ms`).
- **Giao diện Qt6 & Khay hệ thống**:
  - Hỗ trợ `KF6StatusNotifierItem` (KDE) và fallback `QSystemTrayIcon` (GNOME AppIndicator / các desktop khác) với biểu tượng `V` / `E`.
  - Cửa sổ cài đặt Qt6 gồm **6 trang** (`Chung & Kiểu gõ`, `Phím tắt`, `Ứng dụng`, `Macro & Mã chữ`, `Nâng cao`, `Chẩn đoán`), áp dụng tức thời (**Hot-Reload**) qua D-Bus.

---

## Cài đặt nhanh từ gói phát hành ([Releases](https://github.com/hotantruyen/VNSnowKey/releases))

1. Vào trang [Releases](https://github.com/hotantruyen/VNSnowKey/releases), tải gói phù hợp distro của bạn (`.pkg.tar.zst` cho Arch, `.deb` cho Ubuntu/Debian, `.rpm` cho Fedora/openSUSE) cùng tệp `install.sh` vào **cùng một thư mục**.
2. Chạy trình cài đặt tự động:

```bash
chmod +x install.sh
./install.sh
```

Xem chi tiết lệnh cài đặt thủ công theo từng distro tại [docs/INSTALL.md](docs/INSTALL.md).

---

## Clone mã nguồn, Build & Chạy kiểm thử

### 1. Clone repo và cài thư viện phụ thuộc

```bash
git clone https://github.com/hotantruyen/VNSnowKey.git
cd VNSnowKey
```

- **Arch Linux**: `gcc make pkgconf libevdev libxkbcommon qt6-base qt6-svg kstatusnotifieritem systemd wayland dbus libx11 libxfixes`
- **Ubuntu / Debian**: `build-essential pkg-config libevdev-dev libxkbcommon-dev qt6-base-dev qt6-svg-dev libsystemd-dev libwayland-dev wayland-scanner++ dbus libx11-dev libxfixes-dev`
- **Fedora**: `gcc-c++ make pkgconf libevdev-devel libxkbcommon-devel qt6-qtbase-devel qt6-qtsvg-devel systemd-devel wayland-devel libX11-devel libXfixes-devel dbus-daemon`

*(Lưu ý: Khi build trên hệ thống không có `KF6StatusNotifierItem` như Ubuntu/Debian/Fedora, `Makefile` sẽ tự động chuyển sang nhánh fallback `HAVE_KF6_SNI=0`.)*

### 2. Biên dịch, kiểm thử và cài đặt từ source

```bash
make -j$(nproc) all
QT_QPA_PLATFORM=offscreen make test
sudo make install PREFIX=/usr
```

*(Trên Arch Linux có thể đóng gói & cài trực tiếp bằng `makepkg -si`. Hoặc nếu muốn build toàn bộ gói `.pkg.tar.zst`, `.deb`, `.rpm` vào thư mục `dist/` cục bộ — cần `makepkg` + `podman`/`docker` — chạy `./packaging/build-release.sh` rồi `./dist/install.sh`.)*

---

## Cấu hình quyền `/dev/uinput` & Dịch vụ nền

Sau khi cài thủ công bằng `make install`, cấp quyền thiết bị một lần cho user hiện tại:

```bash
sudo modprobe uinput
sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=misc --subsystem-match=input
sudo usermod -aG input "$USER"
```

Đăng xuất và đăng nhập lại để nhóm `input` có hiệu lực, sau đó bật systemd user services:

```bash
systemctl --user daemon-reload
systemctl --user enable --now vnsnowkeyd.service vnsnowkey-gui.service
vnsnowkey ctl doctor
```

*Trên GNOME Wayland, bật thêm extension đi kèm và hỗ trợ khay hệ thống:*

```bash
gnome-extensions enable vnsnowkey-focus@vnsnowkey.org
gnome-extensions enable appindicatorsupport@rgcjonas.gmail.com
```

---

## Điều khiển qua CLI (`vnsnowkey ctl`)

```bash
vnsnowkey ctl toggle              # Đổi nhanh Tiếng Việt (VI) <-> Tiếng Anh (EN)
vnsnowkey ctl vi                  # Bật Tiếng Việt (VI)
vnsnowkey ctl en                  # Bật Tiếng Anh (EN)
vnsnowkey ctl status              # Xem trạng thái daemon (JSON)
vnsnowkey ctl reload              # Tải lại ~/.config/vnsnowkey/config.toml
vnsnowkey ctl panic               # Chế độ an toàn (nhả grab & chuyển về EN)
vnsnowkey ctl resume              # Thoát chế độ an toàn
vnsnowkey ctl im "Telex"          # Đổi kiểu gõ (Telex, Simple Telex, VNI, VIQR, Telex 2)
vnsnowkey ctl charset "Unicode"   # Đổi bảng mã (Unicode, TCVN3 (ABC), VNI Windows, ...)
vnsnowkey ctl convert-clipboard "TCVN3 (ABC)" "Unicode"
vnsnowkey ctl backup ~/vnsnowkey-backup.toml
vnsnowkey ctl restore ~/vnsnowkey-backup.toml
vnsnowkey ctl doctor              # Chẩn đoán uinput/evdev/Wayland/X11/XKB/D-Bus
```

---

## Bảo mật & Quyền riêng tư

- Chạy dưới quyền user thuộc nhóm `input`, không chạy `root`.
- Không mở kết nối mạng và không lưu nội dung gõ phím ra đĩa; debug log mặc định che phím chữ/số (`[REDACTED_KEY]`).

---

## Giấy phép & Ghi công

- Dự án phát hành theo giấy phép **GNU GPLv3 (`GPL-3.0-or-later`)** — xem toàn văn tại [LICENSE](LICENSE) và ghi công bên thứ ba tại [CREDITS](CREDITS).
- Lõi xử lý tiếng Việt (`src/engine/`) được chuyển thể theo **BambooEngine (`bamboo-core`)** của tác giả Lương Thành Lâm (MIT License).
