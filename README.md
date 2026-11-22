# VNSnowKey — Bộ gõ Tiếng Việt cho Linux (Wayland & X11)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Release: 2.0.0](https://img.shields.io/badge/Release-2.0.0-success.svg)](https://github.com/hotantruyen/VNSnowKey/releases)
[![Platform: Linux](https://img.shields.io/badge/Platform-Arch%20%7C%20Ubuntu%20%7C%20Debian%20%7C%20Fedora%20%7C%20openSUSE-lightgrey.svg)](docs/INSTALL.md)

**VNSnowKey** là bộ gõ tiếng Việt mã nguồn mở dành cho Linux, hỗ trợ toàn diện **KDE Plasma, GNOME Shell, Hyprland, Sway, XFCE và X11** trên các bản phân phối phổ biến (**Arch Linux, Ubuntu, Debian, Fedora, openSUSE**).

Bộ gõ hoạt động thông qua cơ chế đọc sự kiện bàn phím từ nhân Linux qua **`evdev` (`EVIOCGRAB`)**, xử lý âm tiết tiếng Việt bằng thuật toán **BambooEngine (`bamboo-core`)**, và phát ký tự ra hệ thống qua thiết bị ảo **`uinput`** kết hợp **XKB 8-Level Keymap Overlay** (cho tốc độ phản hồi tức thì, không nháy chữ, không làm bẩn Clipboard).

---

## Mục lục

1. [Tính năng nổi bật](#tính-năng-nổi-bật)
2. [Cài đặt nhanh từ bản phát hành](#cài-đặt-nhanh-từ-bản-phát-hành-releases)
3. [Cấu hình quyền thiết bị & Dịch vụ nền](#cấu-hình-quyền-thiết-bị--dịch-vụ-nền)
4. [Hướng dẫn sử dụng & Phím tắt](#hướng-dẫn-sử-dụng--phím-tắt)
5. [Cửa sổ Cài đặt (VNSnowKey - Bộ gõ Tiếng Việt)](#cửa-sổ-cài-đặt-vnsnowkey---bộ-gõ-tiếng-việt)
6. [Điều khiển qua giao diện dòng lệnh (CLI)](#điều-khiển-qua-giao-diện-dòng-lệnh-cli)
7. [Biên dịch và kiểm thử từ mã nguồn](#biên-dịch-và-kiểm-thử-từ-mã-nguồn)
8. [Gỡ cài đặt sạch sẽ](#gỡ-cài-đặt-sạch-sẽ)
9. [Bảo mật & Quyền riêng tư](#bảo-mật--quyền-riêng-tư)
10. [Tác giả & Giấy phép](#tác-giả--giấy-phép)

---

## Tính năng nổi bật

### 1. Tương thích môi trường và giao diện hệ thống
- **Hỗ trợ đầy đủ Wayland & X11**: Hoạt động mượt mà trên ứng dụng Wayland thuần (Qt6, GTK3/GTK4), XWayland, trình duyệt (Firefox, Chrome, Chromium, Brave), ứng dụng Electron (VS Code, Discord, Slack), ứng dụng văn phòng (LibreOffice, OnlyOffice), và các trình giả lập thiết bị đầu cuối (Konsole, GNOME Terminal, Ptyxis, Alacritty, Kitty, Foot).
- **Thích ứng giao diện hệ thống FreeDesktop**: Tự động nhận diện chủ đề Sáng / Tối (Light / Dark theme) của hệ điều hành, hiển thị đồng bộ màu sắc trên KDE Plasma, GNOME, XFCE và các window manager dạng Tiling.

### 2. Xử lý tiếng Việt chuẩn mực với BambooEngine
- **8 Kiểu gõ phổ thông**:
  - **Telex**: Kiểu gõ thông dụng (`s`, `f`, `r`, `x`, `j`, `w`, `aa`, `ee`, `oo`, `dd`).
  - **Simple Telex**: Telex tối giản (dễ dùng, tránh xung đột phím gõ nhanh).
  - **VNI**: Kiểu gõ số (`1` sắc, `2` huyền, `3` hỏi, `4` ngã, `5` nặng, `6` mũ, `7` móc, `8` trăng, `9` đ).
  - **VIQR**: Bỏ dấu bằng ký tự đặc biệt (`'`, `` ` ``, `?`, `~`, `.`, `^`, `+`, `(`, `dd`).
  - **Telex 2**: Biến thể Telex nâng cao.
  - **Telex W**: Tối ưu phím `w` cho nguyên âm `ư`, `ơ`.
  - **Telex + VNI**: Kết hợp linh hoạt cả phím Telex lẫn phím số VNI.
  - **Microsoft layout**: Kiểu gõ tương thích bàn phím tiếng Việt tiêu chuẩn của Microsoft.
- **5 Bảng mã đầu ra**: **Unicode dựng sẵn (mặc định)**, **TCVN3 (ABC)**, **VNI Windows**, **Unicode tổ hợp**, **VIQR**.
- **Quy tắc dấu linh hoạt**:
  - Tùy chọn đặt dấu kiểu mới (`hoà`, `thuỷ`) hoặc kiểu truyền thống (`hòa`, `thủy`).
  - Cho phép bỏ dấu tự do (đặt dấu ở bất kỳ vị trí nào khi đang gõ từ).
  - Sửa chữ và xóa dấu thông minh: Nhấn phím lặp dấu để hoàn tác về ký tự gốc, sử dụng phím `Backspace` hoặc mũi tên `←` / `→` để di chuyển con trỏ trong từ mà không làm mất liên kết từ tiếng Việt đang gõ.
  - Gõ tắt phụ âm đầu (`cc→ch, gg→gi, kk→kh, nn→ng, qq→qu, pp→ph, tt→th`) và phụ âm cuối (`g→ng, h→nh, k→ch`).
  - Kiểm tra từ điển chính tả tiếng Việt và tùy chọn tự động khôi phục từ tiếng Anh khi gõ từ ngoại quốc.

### 3. Đồng bộ ngôn ngữ & Quản lý ứng dụng
- **Đồng bộ chế độ gõ trên mọi ứng dụng**: Tùy chọn giữ nguyên trạng thái gõ (Tiếng Việt / Tiếng Anh) thống nhất trên toàn hệ thống, hoặc ghi nhớ trạng thái gõ riêng biệt cho từng cửa sổ ứng dụng.
- **Hồ sơ ứng dụng linh hoạt (App Profiles)**: Dễ dàng cấu hình ứng dụng cụ thể luôn gõ Tiếng Việt, luôn Tiếng Anh, hoặc loại trừ hoàn toàn (không can thiệp phím).
- **Chế độ chơi game tự động (Game Mode)**: Khi chơi game 3D (Minecraft, CS, v.v.), hệ thống tự động giữ nguyên các phím di chuyển (`W/A/S/D`) và chỉ bật gõ tiếng Việt khi phát hiện con trỏ chuột xuất hiện hoặc mở khung chat.
- **Tự động bỏ qua hộp thoại mật khẩu**: Tạm thời tắt tiếng Việt khi nhập mật khẩu để đảm bảo an toàn và chính xác.

### 4. Bàn phím ảo tốc độ cao & Chống mất chữ
- Cơ chế xuất phím **XKB Keymap 8-Level Overlay** sử dụng ánh xạ cấp độ trực tiếp, không phụ thuộc bộ nhớ tạm, loại bỏ hoàn toàn hiện tượng chớp nháy con trỏ và xung đột clipboard.
- Bộ đệm an toàn hỗ trợ tự động xóa khi nhấp chuột (`reset_on_mouse_click`), khi đổi cửa sổ (`reset_on_focus_change`), hoặc theo thời gian chờ (`buffer_timeout_ms`).
- Hỗ trợ sửa gợi ý tự động hoàn thành trên thanh địa chỉ trình duyệt web (URL autocomplete fix).

---

## Cài đặt nhanh từ bản phát hành ([Releases](https://github.com/hotantruyen/VNSnowKey/releases))

### Cách 1: Cài đặt tự động bằng script (Khuyên dùng)

1. Tải gói cài đặt phù hợp với bản phân phối của bạn cùng tệp `install.sh` từ trang [Releases](https://github.com/hotantruyen/VNSnowKey/releases) về **cùng một thư mục**.
2. Mở terminal tại thư mục đó và chạy:

```bash
chmod +x install.sh
./install.sh
```

Script sẽ tự động nhận diện distro, kiểm tra checksum SHA-256, cài đặt gói, cấu hình quyền thiết bị `uinput` và kích hoạt dịch vụ chạy ngầm.

---

### Cách 2: Cài đặt thủ công bằng trình quản lý gói của hệ thống

#### Arch Linux / Manjaro / CachyOS / EndeavourOS
```bash
sudo pacman -U vnsnowkey-2.0.0-x86_64.pkg.tar.zst
```

#### Ubuntu 24.04+ / Debian 12+ / Linux Mint / Pop!_OS
```bash
sudo apt update
sudo apt install --reinstall ./vnsnowkey_2.0.0_amd64.deb
```

#### Fedora 41+
```bash
sudo dnf install ./vnsnowkey-2.0.0-x86_64.rpm
```

#### openSUSE Tumbleweed
```bash
sudo zypper install --allow-unsigned-rpm ./vnsnowkey-2.0.0-x86_64.rpm
```

---

## Cấu hình quyền thiết bị & Dịch vụ nền

Nếu cài đặt thủ công từ gói hoặc biên dịch từ mã nguồn, hãy thiết lập quyền truy cập thiết bị nhân Linux (`/dev/uinput` và `/dev/input/event*`) 1 lần cho tài khoản của bạn:

```bash
# 1. Kích hoạt module uinput và thêm tài khoản vào nhóm input
sudo modprobe uinput
sudo usermod -aG input "$USER"
sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=misc --subsystem-match=input
```

> **Quan trọng**: Sau khi chạy lệnh trên, hãy **đăng xuất (Log Out)** và **đăng nhập lại** để quyền nhóm `input` được áp dụng vào phiên làm việc.

Sau khi đăng nhập lại, bật dịch vụ người dùng:

```bash
systemctl --user daemon-reload
systemctl --user enable --now vnsnowkeyd.service vnsnowkey-gui.service
```

### Thiết lập bổ sung theo môi trường Desktop:
- **KDE Plasma (Wayland)**:
  - Mở **System Settings → Keyboard → Virtual Keyboard** và chọn **`VNSnowKey`**.
  - Làm mới bộ nhớ cache biểu tượng nếu cần: `kbuildsycoca6 --noincremental`.
- **GNOME (Wayland)**:
  - Bật extension theo dõi cửa sổ và tiện ích khay hệ thống:
    ```bash
    gnome-extensions enable vnsnowkey-focus@vnsnowkey.org
    gnome-extensions enable appindicatorsupport@rgcjonas.gmail.com
    ```
  - *(Trên Fedora Workstation, nếu chưa có extension AppIndicator: `sudo dnf install gnome-shell-extension-appindicator`)*.

---

## Hướng dẫn sử dụng & Phím tắt

### 1. Phím tắt mặc định

| Tổ hợp phím | Chức năng | Ghi chú |
| :--- | :--- | :--- |
| `Ctrl + Shift` hoặc `Super + Space` | **Chuyển đổi Tiếng Việt ↔ Tiếng Anh** | Có thể tùy chỉnh trong trang *Phím tắt* |
| `Ctrl + Shift + Backspace` | **Phục hồi từ gốc** | Hoàn trả từ về chuỗi phím tiếng Anh ban đầu |
| `Ctrl + Alt + Escape` | **Chế độ an toàn khẩn cấp (Panic Safe Mode)** | Nhả grab bàn phím và đưa về Tiếng Anh |

### 2. Sử dụng qua khay hệ thống (System Tray)
- Biểu tượng trên khay hệ thống hiển thị chữ **`V`** (đang bật Tiếng Việt) hoặc **`E`** (đang ở Tiếng Anh).
- **Nhấp chuột trái** vào biểu tượng: Chuyển đổi nhanh giữa Tiếng Việt và Tiếng Anh.
- **Nhấp chuột phải** vào biểu tượng: Mở menu ngữ cảnh để Bật/Tắt Tiếng Việt, Kích hoạt Chế độ an toàn, Mở cửa sổ Cài đặt, Xem Chẩn đoán hoặc Thoát.

---

## Cửa sổ Cài đặt (`VNSnowKey - Bộ gõ Tiếng Việt`)

Mở giao diện cài đặt từ menu ứng dụng hoặc nhấp chuột phải vào biểu tượng khay hệ thống chọn **Cài đặt...**. Cửa sổ cài đặt được chia thành **7 trang chức năng**:

### 1. Chung & Kiểu gõ
- Bật / tắt chế độ gõ Tiếng Việt.
- Lựa chọn **Kiểu gõ** (Telex, Simple Telex, VNI, VIQR, ...) và **Bảng mã đầu ra** (Unicode, TCVN3, VNI Windows...).
- Tùy chọn đặt dấu kiểu mới (`hoà`, `thuỷ`), cho phép bỏ dấu tự do, bật/tắt kiểm tra chính tả, và tự động khôi phục từ tiếng Anh.
- Bật/tắt gõ tắt phụ âm đầu (`cc→ch, tt→th...`) và phụ âm cuối (`g→ng, h→nh...`).
- Tùy chọn **Ghi nhớ chế độ gõ riêng cho từng ứng dụng** (bỏ chọn để đồng bộ chế độ gõ trên mọi ứng dụng).
- Tùy chọn tự động bỏ qua khi nhập mật khẩu.

### 2. Phím tắt
- Cấu hình phím tắt chuyển đổi chính (`toggle_primary`), phím tắt phụ (`toggle_secondary`), và phím bổ sung (`toggle_extra`).
- Hỗ trợ cả tổ hợp phím thông thường (`Ctrl+Shift`, `Super+Space`, `Alt+Z`) lẫn các phím chức năng độc lập (`ScrollLock`, `Caps_Lock`, `Pause`).
- Tùy chỉnh phím tắt phục hồi từ gốc và phím tắt kích hoạt chế độ an toàn.

### 3. Ứng dụng
- Quản lý danh sách ứng dụng đã cài đặt và hồ sơ hoạt động tương ứng:
  - **Mặc định (Default)**: Hoạt động theo cài đặt chung.
  - **Luôn Tiếng Việt (AlwaysVi)**: Luôn tự động bật Tiếng Việt khi cửa sổ này nhận focus.
  - **Luôn Tiếng Anh (AlwaysEn)**: Luôn chuyển sang Tiếng Anh khi sử dụng cửa sổ này.
  - **Loại trừ (Excluded)**: Daemon hoàn toàn không xử lý phím trong ứng dụng này.
  - **Game Mode**: Áp dụng cơ chế giữ phím di chuyển `W/A/S/D` cho trò chơi.
- Thêm hoặc xóa hồ sơ ứng dụng nhanh chóng bằng giao diện trực quan.

### 4. Macro & Mã chữ
- **Bảng từ gõ tắt (Macro)**: Định nghĩa các từ viết tắt (ví dụ: `vn` → `Việt Nam`). Hỗ trợ tự động viết hoa theo ngữ cảnh gõ (`Vn` → `Việt Nam`, `VN` → `VIỆT NAM`).
- **Chuyển mã văn bản & Khay nhớ tạm**: Tiện ích chuyển đổi trực tiếp văn bản giữa các bảng mã (TCVN3, VNI Windows, Unicode dựng sẵn, Unicode tổ hợp, VIQR) hoặc chuyển đổi nhanh nội dung Clipboard.

### 5. Nâng cao
- **Phương thức xuất ký tự**:
  - `XKB Keymap 8-Level`: Mặc định, nhanh nhất, mượt nhất, không chớp giật con trỏ.
  - `Khay nhớ tạm (Clipboard Ctrl+V)` và `Ctrl+Shift+V`: Hỗ trợ cho các ứng dụng hoặc bảng mã đặc thù.
  - `Mã Hex Unicode (Ctrl+Shift+U)`: Chuẩn nhập ký tự Unicode.
- **Độ trễ & Quản lý bộ đệm**: Tùy chỉnh độ trễ gửi phím ảo, độ trễ khôi phục clipboard, và thời gian tự động xóa bộ đệm (`buffer_timeout_ms`).
- **Quy tắc làm sạch bộ đệm**: Tùy chọn xóa bộ đệm khi nhấp chuột, khi chuyển cửa sổ, hoặc khi nhấn phím điều hướng (`↑`, `↓`, `Home`, `End`, `PageUp`, `PageDown`).
- **Sao lưu & Khôi phục**: Xuất toàn bộ cấu hình ra tệp TOML dự phòng và khôi phục khi cần thiết.

### 6. Chẩn đoán
- Tự động kiểm tra tình trạng kết nối `D-Bus`, quyền truy cập `/dev/uinput`, quyền đọc `/dev/input/event*`, trạng thái phiên hiển thị (`Wayland / X11`), và cơ chế XKB Keymap.
- Xem nhật ký hoạt động thời gian thực (`log`), hỗ trợ kiểm tra phát hiện lỗi nhanh chóng.

### 7. Giới thiệu
- Hiển thị phiên bản hiện tại (**v2.0.0**), tác giả phát triển (**Hồ Tấn Truyền**), và giấy phép mã nguồn mở **GPL-3.0-or-later**.
- Nút **Kiểm tra cập nhật mới**: Tự động kết nối tới GitHub Releases để thông báo khi có phiên bản mới hơn.

---

## Điều khiển qua giao diện dòng lệnh (CLI)

Công cụ `vnsnowkey ctl` cho phép bạn kiểm soát bộ gõ trực tiếp từ terminal hoặc gán vào script của riêng bạn:

```bash
# Chuyển đổi trạng thái gõ
vnsnowkey ctl toggle              # Đổi qua lại Tiếng Việt (VI) <-> Tiếng Anh (EN)
vnsnowkey ctl vi                  # Bật gõ Tiếng Việt
vnsnowkey ctl en                  # Tắt Tiếng Việt (chuyển về Tiếng Anh)

# Xem thông tin trạng thái
vnsnowkey ctl status              # Xuất trạng thái daemon chi tiết dưới dạng JSON
vnsnowkey ctl doctor              # Chạy quy trình chẩn đoán hệ thống toàn diện

# Đổi kiểu gõ và bảng mã nhanh
vnsnowkey ctl im "Telex"          # Đổi kiểu gõ: Telex, Simple Telex, VNI, VIQR, Telex 2...
vnsnowkey ctl charset "Unicode"   # Đổi bảng mã: Unicode, TCVN3 (ABC), VNI Windows...

# Quản lý cấu hình & Chế độ an toàn
vnsnowkey ctl reload              # Nạp lại cấu hình từ ~/.config/vnsnowkey/config.toml
vnsnowkey ctl panic               # Kích hoạt chế độ an toàn (nhả phím và đưa về EN)
vnsnowkey ctl resume              # Thoát chế độ an toàn
vnsnowkey ctl backup ~/config.toml   # Sao lưu cấu hình ra tệp
vnsnowkey ctl restore ~/config.toml  # Khôi phục cấu hình từ tệp

# Chuyển mã văn bản Clipboard
vnsnowkey ctl convert-clipboard "TCVN3 (ABC)" "Unicode"
```

---

## Biên dịch và kiểm thử từ mã nguồn

### 1. Cài đặt các thư viện phụ thuộc

- **Arch Linux / Manjaro / CachyOS**:
  ```bash
  sudo pacman -S --needed gcc make pkgconf libevdev libxkbcommon qt6-base qt6-svg kstatusnotifieritem systemd wayland dbus libx11 libxfixes
  ```
- **Ubuntu / Debian / Linux Mint**:
  ```bash
  sudo apt install build-essential pkgconf libevdev-dev libxkbcommon-dev qt6-base-dev qt6-svg-dev libsystemd-dev libwayland-dev libwayland-bin dbus libx11-dev libxfixes-dev
  ```
- **Fedora**:
  ```bash
  sudo dnf install gcc-c++ make pkgconf libevdev-devel libxkbcommon-devel qt6-qtbase-devel qt6-qtsvg-devel systemd-devel wayland-devel dbus-daemon libX11-devel libXfixes-devel
  ```

### 2. Biên dịch, chạy kiểm thử và cài đặt

```bash
git clone https://github.com/hotantruyen/VNSnowKey.git
cd VNSnowKey

# Biên dịch toàn bộ các thành phần
make -j$(nproc) all

# Chạy toàn bộ 5 bộ kiểm thử tự động
QT_QPA_PLATFORM=offscreen make test

# Cài đặt vào hệ thống
sudo make install PREFIX=/usr
```

---

## Gỡ cài đặt sạch sẽ

Để gỡ cài đặt VNSnowKey hoàn toàn khỏi hệ thống (dừng dịch vụ, gỡ gói ứng dụng, dọn dẹp ánh xạ XKB và tệp cấu hình):

```bash
chmod +x uninstall.sh
./uninstall.sh
```

Hoặc chạy với cờ `--purge` để xóa luôn cả thư mục cấu hình cá nhân:

```bash
./uninstall.sh --purge
```

---

## Bảo mật & Quyền riêng tư

- **Không chạy với quyền root**: Tất cả các thành phần `vnsnowkeyd` và `vnsnowkey-gui` đều hoạt động dưới quyền người dùng thông thường (`user`) thông qua nhóm `input`.
- **Hoàn toàn ngoại tuyến**: Không mở socket mạng để gửi nhận dữ liệu gõ phím.
- **Không lưu nhật ký gõ phím**: Bộ gõ không ghi lại nội dung gõ phím vào ổ đĩa. Mặc định các ký tự chữ và số trong nhật ký gỡ lỗi đều được mã hóa ẩn danh (`[REDACTED_KEY]`).

---

## Tác giả & Giấy phép

- **Tác giả phát triển**: **Hồ Tấn Truyền** ([GitHub](https://github.com/hotantruyen)).
- **Giấy phép phần mềm**: Phát hành theo bản quyền **GNU General Public License v3 ([GPL-3.0-or-later](LICENSE))**.
- **Ghi công lõi xử lý**: Thuật toán xử lý âm tiết tiếng Việt trong `src/engine/` kế thừa và chuyển thể từ thư viện mã nguồn mở **BambooEngine (`bamboo-core`)** của tác giả **Lương Thành Lâm** ([SPDX: GPL-3.0-or-later AND MIT](CREDITS)).
