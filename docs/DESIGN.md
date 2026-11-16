# Tài liệu Thiết kế Kỹ thuật VNSnowKey (`docs/DESIGN.md`)

**VNSnowKey** là bộ gõ tiếng Việt cho Linux (Wayland & X11), sử dụng `evdev`/`uinput`, XKB 8-Level Overlay và lõi BambooEngine.

---

## 1. Kiến trúc Tổng thể

VNSnowKey được tách thành các tiến trình riêng biệt giao tiếp qua **Session D-Bus (`org.vnsnowkey.Daemon`)**:

1. **`vnsnowkeyd` (Core Input Daemon)**:
   - Chạy dưới quyền user thuộc nhóm `input` (systemd user unit `vnsnowkeyd.service`).
   - **`EvdevCapture`** (`src/input/evdev_capture.cpp`): Quét `/dev/input/event*`, theo dõi cắm nóng thiết bị qua `inotify`, đợi toàn bộ phím vật lý nhả hết (`EVIOCGKEY`) rồi mới chiếm quyền độc quyền bàn phím bằng `ioctl(fd, EVIOCGRAB, 1)`. Đồng thời mở các thiết bị chuột ở chế độ không chiếm quyền (passive read) để bắt sự kiện click chuột (`BTN_LEFT`, `BTN_RIGHT`, `BTN_MIDDLE`) phục vụ xử lý bộ đệm từ (và bảo vệ hậu-click khi tắt `reset_on_mouse_click`).
   - **`BambooEngine` & `SessionProcessor`** (`src/engine/`): Xử lý âm tiết tiếng Việt theo đúng thuật toán và bảng luật của **BambooEngine (`bamboo-core`)**, tính toán chênh lệch (`KeyActionDiff`: số lần `Backspace` + chuỗi ký tự mới cần phát).
   - **`UinputEmitter` & `XkbOverlay`** (`src/output/`): Phát tổ hợp `Backspace` + ký tự tiếng Việt ra bàn phím ảo `VNSnowKey Virtual Keyboard` (`/dev/uinput`) trong cùng một chuỗi sự kiện nguyên tử (`SYN_REPORT`).
   - **`FocusTracker` & `GameModeMonitor`** (`src/focus/`): Theo dõi cửa sổ đang focus trên KDE Plasma (KWin Script `packaging/kwin-script/vnsnowkey-focus.js`), GNOME Shell (extension `vnsnowkey-focus@vnsnowkey.org`), Hyprland, Sway và X11; kết hợp giám sát con trỏ / trạng thái text-input cho **Game Mode** (ví dụ Minecraft).
2. **`vnsnowkey-gui` (Qt6 Settings & System Tray)**:
   - Hiển thị biểu tượng chữ **`V`** / **`E`** trên khay hệ thống qua `KStatusNotifierItem` (KDE) hoặc `QSystemTrayIcon` (GNOME AppIndicator / các desktop khác).
   - Cung cấp cửa sổ cài đặt Qt6 gồm **6 trang** (`Chung & Kiểu gõ`, `Phím tắt`, `Ứng dụng`, `Macro & Mã chữ`, `Nâng cao`, `Chẩn đoán`), áp dụng thay đổi tức thời (**Hot-Reload**) qua D-Bus mà không cần khởi động lại daemon.
3. **`vnsnowkey` (CLI Controller)** & **`vnsnowkey-im` (Wayland Virtual Keyboard Signal Bridge)**:
   - `vnsnowkey ctl ...` điều khiển daemon hoặc chẩn đoán hệ thống (`doctor`); `vnsnowkey-im` hỗ trợ cầu nối tín hiệu bàn phím ảo Wayland (`zwp_input_method_v1`) khi cần.

```
┌───────────────────────────┐       inotify + EVIOCGKEY + EVIOCGRAB
│  Bàn phím & Chuột vật lý  │ ──────────────────────────────────────────┐
│   (/dev/input/event*)     │                                           ▼
└───────────────────────────┘                                 ┌───────────────────┐
                                                              │   EvdevCapture    │
┌───────────────────────────┐   D-Bus NotifyWindowChanged     │ (Shortcut/Mod/Key)│
│     KWin 6 Compositor     │ ─────────────────────────────►  └─────────┬─────────┘
│ (vnsnowkey-focus.js Script) │                                           │
└───────────────────────────┘                                           ▼
                                                              ┌───────────────────┐
┌───────────────────────────┐   D-Bus org.vnsnowkey.Daemon      │  BambooEngine &   │
│ vnsnowkey-gui (Qt6 + SNI)   │ ◄─────────────────────────────► │ SessionProcessor  │
│ & vnsnowkey CLI             │                                 └─────────┬─────────┘
└───────────────────────────┘                                           │ KeyActionDiff
                                                                        ▼
┌───────────────────────────┐   Atomic EV_KEY + SYN_REPORT    ┌───────────────────┐
│ Mọi ứng dụng Wayland / X11│ ◄────────────────────────────── │   UinputEmitter   │
│ (Qt, GTK, Electron, Term) │      (/dev/uinput + XKB)        │  + XkbOverlay     │
└───────────────────────────┘                                 └───────────────────┘
```

---

## 2. Giải quyết Bài toán Xuất Ký tự Unicode trên Wayland qua `uinput`

### Bản chất vấn đề
Thiết bị ảo `/dev/uinput` hoạt động ở tầng nhân Linux (`EV_KEY`), chỉ có thể gửi **mã phím vật lý (Linux keycode)** như `KEY_A`, `KEY_BACKSPACE`, `KEY_F23`... chứ không thể gửi trực tiếp mã Unicode (`U+1EA1` `ạ`, `U+01B0` `ư`). Khi `kwin_wayland` nhận keycode từ `uinput`, nó dùng `libxkbcommon` để dịch keycode thành `xkb_keysym_t` và chuỗi UTF-8 rồi mới gửi xuống ứng dụng.

VNSnowKey thiết kế **kiến trúc phát Unicode đa lớp (4-Layer Output Architecture)**, cho phép kết hợp ưu điểm của từng phương pháp và cấu hình riêng cho từng ứng dụng:

| Lớp | Phương pháp | Cơ chế hoạt động | Ưu điểm | Nhược điểm | Phạm vi áp dụng trong VNSnowKey |
|---|---|---|---|---|---|
| **Lớp 1 (Mặc định & Chủ lực)** | **XKB 8-Level Virtual Keymap Overlay** (`xkb_keymap`) | Ghi đè bảng symbol mở rộng `~/.config/xkb/symbols/us` và `~/.config/xkb/types/level5` (chuẩn `libxkbcommon` user-override), ánh xạ toàn bộ **134 ký tự tiếng Việt** (67 chữ thường + 67 chữ hoa) vào **Level 3..8** (`EIGHT_LEVEL_ALPHABETIC`) của 23 phím chữ cái, kích hoạt bằng hai phím modifier ảo không tồn tại trên bàn phím vật lý: `KEY_F23` (`ISO_Level3_Shift`) và `KEY_F24` (`ISO_Level5_Shift`). Sau đó gọi tín hiệu D-Bus `/kxkbrc org.kde.kconfig.notify.ConfigChanged` để `kwin_wayland` nạp lại keymap trực tiếp. | - **Độ trễ bằng 0 (Zero-latency)**.<br>- **Tuyệt đối không đụng vào Clipboard** của người dùng.<br>- Hoạt động đồng nhất trên **100% ứng dụng**: Wayland native (Qt6, GTK3/4), Xwayland, Konsole, Alacritty, Kitty, Firefox, Chromium, VS Code/Electron, LibreOffice, Telegram.<br>- Kể cả khi người dùng bật `CapsLock`, định nghĩa `level5` tuỳ chỉnh của VNSnowKey vẫn giữ đúng chữ hoa/chữ thường cho Level 3..8. | Cần cài file cấu hình XKB vào `~/.config/xkb/` và phát tín hiệu để KWin nạp lại. | **Mặc định cho toàn hệ thống** khi dùng bảng mã `Unicode`. |
| **Lớp 2 (Dự phòng & Bảng mã 8-bit)** | **Safe Clipboard Paste** (`clipboard_ctrl_v` / `clipboard_ctrl_shift_v`) | Lưu nội dung Clipboard hiện tại qua D-Bus `org.kde.klipper /klipper getClipboardContents`, đặt chuỗi văn bản cần phát vào Klipper, gửi tổ hợp `Ctrl+V` (hoặc `Ctrl+Shift+V` cho terminal), sau đó khôi phục lại Clipboard cũ kèm **bộ đếm tuần tự nguyên tử (`std::atomic<uint64_t> clipboardSeq_`)** chống race condition khi gõ nhanh. | - Phát được mọi chuỗi byte bất kỳ (kể cả Emoji trong Macro, hoặc các bảng mã di sản **TCVN3 (ABC)**, **VNI Windows** không có trong XKB). | - Có độ trễ nhỏ (~2–45ms) để ứng dụng kịp nhận sự kiện dán trước khi Clipboard được khôi phục.<br>- Một số ứng dụng dùng phím tắt dán khác nhau (`Ctrl+V` vs `Ctrl+Shift+V`). | Tự động kích hoạt khi người dùng chọn bảng mã **TCVN3 / VNI Windows**, khi Macro chứa ký tự ngoài bảng 134 chữ cái tiếng Việt, hoặc khi cấu hình trong Per-App Profile. |
| **Lớp 3 (Theo ứng dụng)** | **Hex Unicode Entry** (`ctrl_shift_u`) | Gửi `Ctrl+Shift+U` + chuỗi mã hex của codepoint Unicode + `Enter`. | Không cần XKB overlay hay Clipboard trên các ứng dụng GTK/Electron hỗ trợ ISO 14755. | Không hoạt động trên một số ứng dụng Qt/Xwayland và có thể hiện gạch chân `uXXXX` thoáng qua. | Tuỳ chọn trong Per-App Profile. |
| **Lớp 4 (Tuỳ biến theo App)** | **Per-App Profile & URL Bar Autocomplete Fix** | Theo dõi `resourceClass` qua KWin 6 D-Bus để áp dụng `outputMethod`, `delayUs`, và `autocompleteFix` riêng cho từng ứng dụng. | Khi bật `autocompleteFix` cho ô địa chỉ trình duyệt (nơi gợi ý URL đang được bôi đen), VNSnowKey gửi thêm 1 `Backspace` hoặc phím xoá phần bôi đen trước khi xoá ký tự gốc. | Cần nhận diện đúng `resourceClass` từ KWin. | Tự động qua `FocusTracker` + bảng `app_profiles` trong `config.toml`. |

---

## 3. Tích hợp BambooEngine (`bamboo-core`) & Tuân thủ Giấy phép

### 3.1. Giấy phép
`bamboo-core` (`https://github.com/BambooEngine/bamboo-core`) được phát hành dưới **Giấy phép MIT (`Copyright (c) 2018 Luong Thanh Lam`)**. VNSnowKey phát hành toàn dự án dưới **GNU GPLv3 (`GPL-3.0-or-later`)**, đồng thời bảo toàn đầy đủ bản quyền và nội dung giấy phép MIT gốc của `bamboo-core` trong `CREDITS`, `debian/copyright` và phần đầu các file nguồn liên quan trong `src/engine/`.

### 3.2. Lý do Port trực tiếp sang C++20 trong cùng không gian địa chỉ của Daemon
Thay vì nhúng runtime Go (`cgo` với garbage collector có thể gây pause vài mili-giây giữa lúc giữ `EVIOCGRAB`), toàn bộ kiến trúc cây âm tiết (`Node`: `FirstConsonant`, `Vowel`, `LastConsonant`), bộ phân tích luật gõ (`RulesParser`), bộ kiểm tra chính tả hình vị học (`Spelling`), bộ trải phẳng dấu (`Flattener`) và bảng mã (`CharsetEncoder`) của `bamboo-core` được hiện thực hoá 1:1 bằng C++20 thuần trong `src/engine/`, đối chiếu trực tiếp với bộ test của `bamboo-core` (236/236 test cases pass tuyệt đối).

### 3.3. Cơ chế Lùi (Backtrack) & Biến đổi Âm tiết
- **Đổi dấu / Xoá dấu**:
  - `a` + `s` → `á`
  - Gõ tiếp `f` → đổi sang `à` (chỉ gửi 1 `Backspace` + `à`)
  - Gõ tiếp `z` → xoá dấu về `a`
  - Gõ tiếp `z` lần nữa → lùi (undo) trả lại chữ `az` nguyên gốc.
- **Tác động kép (Double-key Undo)**:
  - `dd` → `đ`, gõ thêm `d` (`ddd`) → hoàn tác thành `dd`.
  - `aa` → `â`, gõ thêm `a` (`aaa`) → hoàn tác thành `aa`.
- **Cụm nguyên âm kép `uow` & `w` độc lập**:
  - `uow` → `ươ` (ví dụ `nuowcs` → `nước`, `nguwowif` → `người`, `uowcs` → `ước`), trong khi `Thuow` → `Thuơ` và `quow` → `quơ` theo đúng quy tắc âm vị học tiếng Việt của `bamboo-core`.
  - `w` gõ độc lập ở đầu âm tiết (Telex) → `ư` (`w` → `ư`, `ww` → `w`).
- **Tự động khôi phục từ tiếng Anh (`autoRestoreEnglish`)**:
  - Khi âm tiết kết thúc bằng dấu cách hoặc dấu câu mà cấu trúc `C-V-C` không hợp lệ trong tiếng Việt (ví dụ: `"windows"`, `"search"`, `"function"`, `"string"`), `SessionProcessor` tự động phát chuỗi `Backspace` và hoàn trả chính xác từ tiếng Anh gốc.

---

## 4. Chất lượng Gõ, Chống Nháy Chữ & Cơ chế Thoát An toàn

1. **Chống nháy chữ (Zero-Flicker Batching)**:
   - Khi sửa từ (ví dụ `hoaf` từ `hoa` → `hoà`), `SessionProcessor::computeDiff` chỉ xoá đúng hậu tố thay đổi (xoá 1 ký tự `a`, phát `à`, giữ nguyên `ho`).
   - `UinputEmitter::emitDiff` gom toàn bộ chuỗi phím `Backspace` và chuỗi phím mới vào một lô liên tiếp, không xen `usleep` (trừ khi ứng dụng cụ thể cấu hình `delay_us > 0`).
2. **Tương thích hoàn hảo với `keyd` và các trình remap phím cấp thấp**:
   - `VNSnowKey Virtual Keyboard` sử dụng mã định danh `Vendor=0x0FAC, Product=0x534B ("VNSnowKey Virtual Keyboard")`. Mã `Vendor=0x0FAC` giúp các daemon remap cấp thấp như `keyd` (khi cấu hình `[ids] *`) nhận biết đây là bàn phím ảo và không chiếm quyền ngược lại thiết bị đầu ra của VNSnowKey, trong khi `EvdevCapture` của VNSnowKey phân biệt thiết bị của chính nó qua `Product=0x534B` và tên thiết bị.
3. **Không bao giờ kẹt phím (Zero Stuck Keys) & Thoát an toàn**:
   - Trước khi gọi `EVIOCGRAB` trên bất kỳ bàn phím nào, `EvdevCapture::waitForAllKeysReleased` kiểm tra bitmap `EVIOCGKEY` cho đến khi mọi phím vật lý đều đã nhả. Điều này ngăn chặn lỗi kinh điển trên Wayland: phím `Enter` (khi gõ lệnh chạy daemon) bị kẹt lặp vô tận vì sự kiện nhả phím bị nuốt sau khi grab.
   - `main_daemon.cpp` cài đặt trình xử lý tín hiệu async-signal-safe cho `SIGINT`, `SIGTERM`, `SIGHUP`, `SIGQUIT`, `SIGSEGV`, `SIGABRT`. Ngay cả khi tiến trình bị kill hoặc crash, hàm khẩn cấp sẽ lập tức gửi sự kiện nhả toàn bộ phím đang nhấn trên `uinput` và gọi `EVIOCGRAB, 0` trên mọi descriptor `evdev`.

---

## 5. Mô hình Bảo mật (Security & Privacy Model)

Việc đọc `/dev/input/event*` và ghi `/dev/uinput` yêu cầu quyền truy cập thiết bị đầu vào (tương đương khả năng đọc phím vật lý). Để giảm thiểu tối đa bề mặt tấn công:
1. **Không chạy dưới quyền `root`**: `vnsnowkeyd` chạy như một **systemd user service** của người dùng hiện tại, sử dụng quyền nhóm `input` và thẻ `uaccess` của `systemd-logind` (`packaging/70-vnsnowkey-uinput.rules`).
2. **Không kết nối mạng**: Toàn bộ mã nguồn không sử dụng bất kỳ socket mạng (`AF_INET`, `AF_INET6`) nào.
3. **Tuyệt đối không ghi nội dung phím ra đĩa**:
   - Tính năng thống kê gõ (mặc định tắt) chỉ tăng bộ đếm số nguyên trong bộ nhớ RAM (`wordsTyped_`, `keysTyped_`).
   - Chế độ debug (mặc định tắt) tự động che (`[REDACTED_KEY]`) mọi mã phím chữ và số, chỉ ghi trạng thái chuyển đổi hệ thống.
