# Hướng dẫn cài đặt VNSnowKey (`1.0.1`)

VNSnowKey là bộ gõ Tiếng Việt cho Linux Wayland và X11, hỗ trợ **KDE Plasma, GNOME, Hyprland, Sway và X11**.

---

## 1. Cài nhanh tự động (từ GitHub Releases)

Tải tệp gói cài đặt phù hợp distro (`.pkg.tar.zst` / `.deb` / `.rpm`) cùng tệp `install.sh` về **cùng một thư mục** (hoặc đứng trong thư mục `dist/` sau khi tự chạy `./packaging/build-release.sh`) rồi chạy:

```bash
chmod +x install.sh
./install.sh
```

Xem trước lệnh sẽ chạy mà không thay đổi hệ thống: `./install.sh --dry-run`.

---

## 2. Kiểm tra checksum (khi chạy trong `dist/` hoặc có `SHA256SUMS`)

```bash
sha256sum -c SHA256SUMS --ignore-missing
```

---

## 3. Lệnh cài đặt thủ công theo từng distro

> Chạy các lệnh dưới đây ngay trong thư mục chứa file gói tải về từ Releases (hoặc thay bằng đường dẫn tới file trong `dist/`, ví dụ `archlinux/...`, `debian-ubuntu/...`, `fedora-opensuse/...`).

### Arch Linux (Arch, CachyOS, EndeavourOS, Manjaro)
```bash
sudo pacman -U vnsnowkey-1.0.1-x86_64.pkg.tar.zst
```

### Ubuntu / Debian (Ubuntu 24.04+, Debian 12+, Linux Mint, Pop!_OS, KDE Neon)
```bash
sudo apt update
sudo apt install --reinstall ./vnsnowkey_1.0.1_amd64.deb
```
> Lưu ý giữ `./` ở đầu đường dẫn để `apt` hiểu là cài file cục bộ và tự tải các thư viện Qt6 phụ thuộc (`--reinstall` giúp cài đè được cả khi máy đang có sẵn bản `1.0.1` trước đó).

### Fedora (Fedora 41+)
```bash
# Cài lần đầu hoặc nâng cấp lên bản mới:
sudo dnf install ./vnsnowkey-1.0.1-x86_64.rpm

# Hoặc cài đè lại khi máy đã cài đúng cùng phiên bản:
sudo dnf reinstall ./vnsnowkey-1.0.1-x86_64.rpm
```

### openSUSE Tumbleweed
```bash
sudo zypper install --allow-unsigned-rpm --force ./vnsnowkey-1.0.1-x86_64.rpm
```
*(Cần cờ `--allow-unsigned-rpm` khi cài file `.rpm` nội bộ chưa ký GPG key công khai.)*

---

## 4. Kích hoạt quyền `/dev/uinput` và dịch vụ nền (lần đầu cài)

Trên **mọi distro**, thực hiện 1 lần sau khi cài gói:

```bash
sudo modprobe uinput
sudo usermod -aG input "$USER"
sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=misc --subsystem-match=input
```

Sau đó **đăng xuất (log out) và đăng nhập lại** để nhóm `input` có hiệu lực, rồi bật service chạy ngầm:

```bash
systemctl --user daemon-reload
systemctl --user enable --now vnsnowkeyd.service vnsnowkey-gui.service
```

*(Nếu nâng cấp từ bản cũ trên máy đang chạy, chỉ cần restart lại service:)*
```bash
systemctl --user restart vnsnowkeyd.service vnsnowkey-gui.service
```

---

## 5. Thiết lập bổ sung theo từng Desktop Environment

* **KDE Plasma (Wayland)**:
  1. Mở **System Settings → Keyboard → Virtual Keyboard → chọn `VNSnowKey`** (dùng cho cầu `vnsnowkey-im` và chuyển đổi bàn phím ảo).
  2. Nếu icon ngoài menu ứng dụng chưa cập nhật ngay: `kbuildsycoca6 --noincremental`.
* **GNOME (Wayland)**:
  1. Bật extension đọc cửa sổ đang focus (đã được gói sẵn cùng app) và hỗ trợ khay hệ thống (trên Fedora Workstation, gói `.rpm` sẽ tự đề xuất cài `gnome-shell-extension-appindicator`; nếu cài thủ công hoặc bị thiếu thì cài thêm qua `dnf`):
     ```bash
     # Trên Fedora Workstation (nếu chưa có icon V/E ở thanh trên):
     sudo dnf install gnome-shell-extension-appindicator
     gnome-extensions enable appindicatorsupport@rgcjonas.gmail.com

     # Bật extension theo dõi cửa sổ cho Game Mode / per-app:
     gnome-extensions enable vnsnowkey-focus@vnsnowkey.org
     ```
  2. **Đăng xuất và đăng nhập lại** 1 lần để GNOME Shell nạp cả 2 extension mới (sau đó chạy `systemctl --user restart vnsnowkeyd vnsnowkey-gui` nếu service đã bật từ trước).
* **Hyprland / Sway**: Tự động nhận cửa sổ focus qua IPC gốc và dùng StatusNotifier trên thanh bar (`waybar`, v.v.).
* **X11**: Tự động chạy ngay qua X11 và khay hệ thống Qt6.

---

## 6. Kiểm tra trạng thái & Gỡ cài đặt sạch sẽ

Kiểm tra chẩn đoán nhanh (uinput, nhóm `input`, Wayland/X11, XKB overlay):
```bash
vnsnowkey ctl doctor
```

### Gỡ cài đặt tự động trên mọi distro (Khuyên dùng)

Dùng tệp `uninstall.sh` đi kèm trong `dist/` (hoặc `packaging/uninstall.sh` trong repo) để dừng service, gỡ gói hệ thống / file cài từ source, xoá XKB overlay (`~/.config/xkb`), extension GNOME, cấu hình `~/.config/vnsnowkey/` và làm mới cache:

```bash
chmod +x uninstall.sh
./uninstall.sh
```

Tuỳ chọn hữu ích:
- Xem trước các lệnh sẽ thực hiện mà không thay đổi hệ thống: `./uninstall.sh --dry-run`
- Giữ lại cấu hình & macro (`~/.config/vnsnowkey/`): `./uninstall.sh --keep-config`
- Gỡ luôn user khỏi nhóm `input`: `./uninstall.sh --remove-input-group`
- Tự xác nhận không hỏi lại: `./uninstall.sh -y`

### Gỡ gói thủ công theo từng distro (chỉ gỡ package)
* **Arch Linux**: `sudo pacman -Rns vnsnowkey`
* **Ubuntu / Debian**: `sudo apt purge vnsnowkey`
* **Fedora**: `sudo dnf remove vnsnowkey`
* **openSUSE**: `sudo zypper remove -u vnsnowkey`
