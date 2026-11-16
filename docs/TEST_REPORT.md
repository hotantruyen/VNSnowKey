# Báo cáo Kiểm thử & Nghiệm thu VNSnowKey (`docs/TEST_REPORT.md`)

Báo cáo này ghi nhận **trung thực 100%** những hạng mục đã được kiểm thử tự động thực tế trên máy **Arch Linux (`7.3.1-1-cachyos`) + KDE Plasma 6.8.2 (Wayland)**, các lỗi thực tế đã phát hiện và khắc phục trong quá trình chạy test, cùng danh sách kiểm tra thủ công (Manual Test Checklist) cho các tác vụ tương tác trực tiếp trên màn hình desktop.

---

## 1. Tổng hợp Kết quả Kiểm thử Tự động (Automated Test Suites)

Toàn bộ các bộ kiểm thử đều được biên dịch và chạy cùng **`AddressSanitizer` + `UndefinedBehaviorSanitizer` (`-fsanitize=address,undefined -Wall -Wextra`)**:

| Bộ kiểm thử | Lệnh thực thi | Số lượng kiểm tra | Kết quả | Nội dung kiểm thử thực tế |
|---|---|---|---|---|
| **1. Unit Test Lõi Tiếng Việt (`BambooEngine`)** | `./build/test_bamboo_engine` | **258 checks** | **258 PASS / 0 FAIL** | Đối chiếu 1:1 với `bamboo-core`: Telex, Simple Telex, VNI, VIQR; từ đơn, từ ghép; lùi đổi dấu/xoá dấu (`as`→`á`, `asf`→`à`, `asfz`→`a`, `asfzz`→`az`); Backspace giữ word buffer (`toans\b`→`toá`, `toans\bf`→`toà`, `vieets\b`→`viế`, `vieets\bj`→`việ`, `yeens\bf`→`yề`, xoá liên tiếp, xoá hết từ rồi gõ mới, viết hoa, kiểm tra chiều dài buffer khớp màn hình sau từng phím); `dd`→`đ`, `ddd`→`dd`; `uow`→`ươ`, `Thuow`→`Thuơ`, `quow`→`quơ`; `w` đứng riêng → `ư`, `ww`→`w`; `oa`, `uy`, `qu`, `gi`; viết hoa `Shift`/`CapsLock`; đặt dấu kiểu cũ/mới (`hòa/hoà`, `thủy/thuỷ`); kiểm tra chính tả & tự khôi phục từ tiếng Anh (`windows`, `search`, `world`); gõ tắt phụ âm đầu/cuối; Macro tự đổi hoa/thường & Import/Export; chuyển mã 2 chiều (`Unicode`, `TCVN3`, `VNI Windows`, `Unicode tổ hợp`, `VIQR`). |
| **2. Test Kernel `uinput` → `evdev` Loopback, XKB Overlay, Stress & Fuzz** | `./build/test_uinput_harness` | **436 checks** | **436 PASS / 0 FAIL** | - Kiểm tra toàn bộ **134 ký tự tiếng Việt** (67 thường + 67 hoa) qua `libxkbcommon` với cả `CapsLock OFF` và `CapsLock ON`.<br>- Tạo thiết bị thật trên `/dev/uinput`, mở `/dev/input/eventX` thật và giữ `EVIOCGRAB`, bơm phím qua kernel và đọc lại.<br>- **Stress Test tốc độ cao**: 212 phím liên tiếp ở tốc độ **≥ 160–230 phím/giây** (yêu cầu ≥ 15 phím/giây), đúng thứ tự 100%, không mất/đảo chữ.<br>- **Random Fuzz Test**: bơm 2.000 phím ngẫu nhiên + Backspace liên tục, xác nhận **0 phím bị kẹt (`hasAnyPressedKey() == false`)**.<br>- Kiểm tra chống race condition khi khôi phục Clipboard. |
| **3. Test End-to-End Daemon + D-Bus CLI + Phím tắt + Chuột + Backspace + `SIGTERM`** | `./build/test_e2e_daemon` | **37 checks** | **37 PASS / 0 FAIL** | - Chạy trong `dbus-run-session` cô lập để không đụng daemon desktop; kiểm tra xuyên suốt từ bàn phím & chuột ảo `/dev/uinput` → `vnsnowkeyd` → đầu ra.<br>- Bấm phím tắt toàn cục `Ctrl+Shift` và `Super+Space` qua `evdev` để chuyển `VI ↔ EN`.<br>- Click chuột (`BTN_LEFT`) và phím mũi tên (`KEY_LEFT`) reset bộ đệm đúng lúc.<br>- Test Backspace E2E: `toans`+`Backspace`→`toá`+`f`→`toà`; `vieets`+`Backspace`→`viế`+`j`→`việ`; `yeens`+`Backspace`+`f`→`yề`; Backspace liên tiếp; Backspace hết từ; Backspace chữ hoa; gõ nhanh burst + Backspace; mô phỏng focus `firefox` (`autocompleteFix`) kiểm tra Backspace phát chuỗi `[KEY_SPACE, KEY_BACKSPACE, KEY_BACKSPACE]` giữ đồng bộ URL bar.<br>- Đổi `VNI`, bật/tắt `Safe Mode`, và `SIGTERM` nhả `EVIOCGRAB` sạch. |
| **4. Test Đa Ứng dụng Qt6 Widgets, URL Bar Autocomplete & Focus Tracker** | `./build/test_app_integration` | **11 checks** | **11 PASS / 0 FAIL** | - Gõ tiếng Việt nhiều dòng vào `QPlainTextEdit`.<br>- Mô phỏng hành vi ô địa chỉ trình duyệt (`SimulatedBrowserUrlBar`) bôi đen phần gợi ý sau mỗi phím, xác minh cả gõ lẫn `Backspace` đồng bộ với `autocompleteFix`.<br>- Kiểm tra `FocusTracker` tự chuyển ngôn ngữ theo ứng dụng và hộp thoại mật khẩu.<br>- Khởi tạo `SettingsWindow` (7 trang) và `TrayController` (`KStatusNotifierItem`). |
| **5. Test Single-Instance `vnsnowkey-gui` & Single Tray trên D-Bus** | `./build/test_gui_single_instance` | **24 checks** | **24 PASS / 0 FAIL** | - Chạy trong `dbus-run-session` cô lập với `MockStatusNotifierWatcher` (`org.kde.StatusNotifierWatcher`).<br>- Khởi chạy `vnsnowkey-gui --tray` lần 1, sau đó chạy tiếp lần 2 (`--settings`) và lần 3 (`--tray`): xác nhận bản chạy sau gọi `ActivateSettings()` qua D-Bus (`org.vnsnowkey.Gui`), thoát sạch (`exit 0`), và tổng số `StatusNotifierItem` đăng ký trên D-Bus **duy nhất bằng 1**.<br>- Xác nhận `vnsnowkeyd` không tự tạo `StatusNotifierItem` và không liên kết `Qt6Widgets`/`KF6StatusNotifierItem`. |
| **6. Kiểm tra Biểu tượng Khay hệ thống V/E Monochrome** | `./build/vnsnowkey-gui --test-icons` | **16 render checks** | **PASS** | Kết xuất thực tế các biểu tượng SVG đơn sắc `V` và `E` ở cả 4 kích thước `16x16`, `22x22`, `24x24`, `32x32` cho cả **Dark Theme** (chữ trắng `#f2f2f7`) và **Light Theme** (chữ đen `#232629`), kiểm tra độ tương phản và độ nét pixel. |
| **7. Đóng gói Arch Linux (`PKGBUILD`)** | `makepkg -f` | **Full package build** | **PASS** | Xây dựng gói `vnsnowkey-1.0.0-1-x86_64.pkg.tar.zst` hoàn chỉnh, chạy toàn bộ `check()` (bao gồm `test_gui_single_instance`), và giữ duy nhất cơ chế tự khởi động bằng systemd user service (`/usr/lib/systemd/user/vnsnowkey*.service`). |

---

## 2. Các Lỗi Thực tế Đã Phát hiện và Sửa trong Vòng lặp Build → Test

1. **Quy tắc `uinput_write` của nhân Linux (`drivers/input/misc/uinput.c`)**:
   - *Phát hiện*: Khi chạy `test_uinput_harness` trên `/dev/uinput` thật, lần ghi gom nhiều `struct input_event` vào một lời gọi `write()` duy nhất trả về `EINVAL` vì hàm `uinput_write()` trong nhân Linux bắt buộc mỗi lần `write()` phải đúng bằng `sizeof(struct input_event)`.
   - *Khắc phục*: Ghi từng `struct input_event` liên tiếp và kết thúc bằng `EV_SYN (SYN_REPORT)`. Nhân Linux tự gom toàn bộ `EV_KEY` trong bộ đệm thiết bị và chỉ đánh thức trình đọc `evdev` khi gặp `SYN_REPORT`, đảm bảo tính nguyên tử (không nháy chữ) và tương thích 100% với driver `uinput`.
2. **Xung đột với `/usr/bin/keyd` đang chạy trên máy người dùng**:
   - *Phát hiện*: Khi `test_uinput_harness` tạo bàn phím ảo `VNSnowKey Virtual Keyboard`, tiến trình `/usr/bin/keyd` (đang chạy quyền `root` với cấu hình `/etc/keyd/default.conf` là `[ids] *`) lập tức nhảy vào chiếm quyền (`EVIOCGRAB`) thiết bị ảo của VNSnowKey khiến `ioctl(evFd, EVIOCGRAB, 1)` trả về `EBUSY`.
   - *Khắc phục*: Qua phân tích mã máy của `/usr/bin/keyd` (`cmpw $0xfac, vendor`), `keyd` nhận diện mọi thiết bị có `Vendor ID == 0x0FAC` là bàn phím ảo và bỏ qua không grab. VNSnowKey đã đặt `kVNSnowKeyVendorId = 0x0FAC` và `kVNSnowKeyProductId = 0x534B ("VNSnowKey Virtual Keyboard")`, giúp `keyd` không chiếm thiết bị đầu ra của VNSnowKey, đồng thời VNSnowKey vẫn bắt được đầu ra từ `keyd virtual keyboard` (`0x0FAC:0x0ADE`).
3. **Cụm nguyên âm `uow` đứng đầu từ vs sau phụ âm (`Thuow` vs `uow`)**:
   - *Phát hiện*: `test_bamboo_engine` phát hiện gõ `"uow"` độc lập ra `"uơ"` thay vì `"ươ"` do bảng nguyên âm cho phép `"uơ"` (vốn chỉ hợp lệ sau phụ âm `h`, `q`, `th` như `huơ`, `quơ`, `thuơ`).
   - *Khắc phục*: Tinh chỉnh `isValidCVC` trong `src/engine/spelling.cpp` để từ chỉ có nguyên âm (`fc.empty() && lc.empty()`) chọn nhánh `"ươ"`, giúp cả `"uow"` → `"ươ"` và `"Thuow"` → `"Thuơ"` đều đạt 100%.
4. **Cờ `-fPIC` khi đóng gói bằng `makepkg`**:
   - *Phát hiện*: Khi chạy `makepkg -f`, biến môi trường `CXXFLAGS` của `makepkg.conf` ghi đè `CXXFLAGS ?=` trong `Makefile`, làm mất cờ `-fPIC` dẫn đến lỗi liên kết với `libQt6Core.so` (`copy relocation against non-copyable protected symbol`).
   - *Khắc phục*: Sử dụng `override CXXFLAGS += -std=c++20 -Wall -Wextra -fPIC` trong `Makefile`.
5. **Hiện 2 biểu tượng tray khi tự khởi động**:
   - *Phát hiện*: Cài song song `vnsnowkey-gui.service` và XDG autostart (`/etc/xdg/autostart/org.vnsnowkey.Autostart.desktop`, `~/.config/autostart/org.vnsnowkey.Autostart.desktop`), cộng với `vnsnowkey-gui` chưa kiểm tra single-instance.
   - *Khắc phục*: Chỉ giữ duy nhất systemd user service (`vnsnowkeyd.service` + `vnsnowkey-gui.service`), bỏ file XDG autostart khỏi `Makefile`/`SettingsWindow`/`vnsnowkey.install`, và bổ sung `QLockFile` + D-Bus `org.vnsnowkey.Gui` (`ActivateSettings`) trong `src/gui/main_gui.cpp`.
6. **Sai vị trí dấu sau Backspace (`toans` + Backspace + `f` → `tòa`, `vieets` + Backspace + `j` → `vịê`) & URL bar Backspace**:
   - *Phát hiện*: `computeEngineFlags()` đảo ngược cờ `EstdToneStyle` (cũ vs mới); `BambooEngine::findToneTarget()` ở nhánh `!stdStyle` bỏ sót nguyên âm có mũ/móc `ê`/`ơ` khiến `viế` bị dời dấu sang `víê`; `SessionProcessor::processBackspace()` không xoá `wordLockedRaw_` khi đệm rỗng; và `EvdevCapture` bỏ qua `autocompleteFix` khi `diff.passThrough` trên phím `KEY_BACKSPACE`.
   - *Khắc phục*: Đảo đúng cờ `!opts.modernToneStyle`, đồng nhất nhánh `U'ơ' || U'ê'` trong `findToneTarget()`, `reset()` sạch khi `rawKeys_` rỗng sau Backspace, và dẫn `KEY_BACKSPACE` có `autocompleteFix` qua `emitDiff(1, ...)`.

---

## 3. Báo cáo Trung thực về Phạm vi Test Tương tác & Checklist Kiểm tra Thủ công

### Những gì chưa test tự động trực tiếp trên cửa sổ đang mở của người dùng
Để **không làm gián đoạn bàn phím vật lý đang gõ của người dùng** và **không tự ý bắn phím vào các cửa sổ ứng dụng cá nhân đang mở** trên phiên KDE Plasma hiện tại:
- Bài test E2E của `vnsnowkeyd` đã sử dụng biến môi trường `VNSNOWKEY_ONLY_DEVICES` để chỉ chiếm quyền bàn phím vật lý ảo do bộ test tạo ra và bắt toàn bộ phím đầu ra trong bộ test.
- Chúng tôi **không giả vờ** đã tự động bật cửa sổ Firefox, LibreOffice, Telegram hay VS Code của người dùng lên để bắn phím trực tiếp vào dữ liệu của người dùng.

### Checklist Kiểm tra Thủ công cho Người dùng (Manual Verification Checklist)

Sau khi cài đặt gói (`makepkg -si`) và bật service (`systemctl --user enable --now vnsnowkeyd.service vnsnowkey-gui.service`), bạn có thể kiểm tra nhanh theo danh sách sau:

- [ ] **1. Khay hệ thống KDE Plasma (Tray Icon)**:
  - Quan sát góc phải thanh Panel KDE: thấy biểu tượng chữ **`V`** đơn sắc (trắng trên theme tối / đen trên theme sáng).
  - Click chuột trái vào biểu tượng: đổi sang chữ **`E`** (kèm thông báo OSD KDE Plasma), click lần nữa đổi về **`V`**.
  - Click chuột phải: mở menu chọn kiểu gõ (`Telex`, `VNI`, `VIQR`), bảng mã, và chọn **Cài đặt VNSnowKey...** để mở cửa sổ cấu hình Qt6.
- [ ] **2. Phím tắt toàn cục (`Ctrl+Shift` & `Super+Space`)**:
  - Nhấn `Ctrl+Shift` hoặc `Super+Space`: biểu tượng khay hệ thống đổi tức thời giữa `V` và `E`, menu Start (Kickoff) của KDE không bị bật nhầm khi bấm `Super+Space`.
- [ ] **3. Kiểm tra gõ trong KDE Kate / KWrite (Wayland Native Qt6)**:
  - Mở Kate, gõ thử: `Tieengs Vieetj ddoocj laapj - hoaf thuyr - uow - w - ddd` → Kết quả: `Tiếng Việt độc lập - hoà thuỷ - ươ - ư - dd`.
  - Thử lùi đổi dấu: gõ `a`, `s` (ra `á`), gõ tiếp `f` (đổi thành `à` mượt không nháy chữ), gõ tiếp `z` (về `a`).
  - Gõ từ tiếng Anh: `windows `, `search ` → tự động giữ nguyên `windows `, `search `.
- [ ] **4. Kiểm tra trong Konsole (Terminal)**:
  - Mặc định trong cấu hình `app_profiles`, `konsole` được đặt ở chế độ `en` để gõ lệnh shell an toàn.
  - Nhấn `Ctrl+Shift` để chuyển sang `VI` trong Konsole (hoặc chỉnh profile `konsole` thành `default` trong tab **4. Ứng dụng**), gõ `echo "Xin chaof Vieetj Nam"` → hiển thị đúng tiếng Việt, phím `Ctrl+C` / `Ctrl+D` hoạt động bình thường.
- [ ] **5. Kiểm tra trong Trình duyệt (Firefox / Chromium) & Ô địa chỉ (URL Bar)**:
  - Gõ trong ô tìm kiếm trang web: gõ nhanh câu dài ≥ 15 phím/giây.
  - Gõ trên thanh địa chỉ trình duyệt có gợi ý tự động (autocomplete): nếu thấy phím `Backspace` đầu tiên xoá phần gợi ý được bôi đen, mở `Cài đặt VNSnowKey → 4. Ứng dụng`, bật `Sửa lỗi URL Autocomplete = true` cho `firefox` / `chromium`.
- [ ] **6. Kiểm tra trong VS Code / Electron, LibreOffice, Telegram & App Xwayland**:
  - Chuyển qua lại giữa cửa sổ VS Code (đặt `EN`) và Telegram / LibreOffice (đặt `VI`): xác nhận VNSnowKey tự động nhớ và chuyển chế độ ngôn ngữ riêng cho từng cửa sổ.
  - Click chuột sang vị trí khác khi đang gõ dở một từ: xác nhận bộ đệm từ được xoá sạch, phím tiếp theo không làm nhảy dấu của từ cũ.
