# Báo cáo Kiểm thử & Nghiệm thu VNSnowKey (`docs/TEST_REPORT.md`) - v2.0.0

Báo cáo này ghi nhận các hạng mục đã được kiểm thử tự động trong dự án (bao gồm bản chạy với `AddressSanitizer` + `UndefinedBehaviorSanitizer` `-fsanitize=address,undefined -Wall -Wextra`) cùng danh sách kiểm tra thủ công cho desktop.

---

## 1. Tổng hợp Kết quả Kiểm thử Tự động (Automated Test Suites)

| Bộ kiểm thử | Lệnh thực thi | Số lượng kiểm tra | Kết quả | Nội dung kiểm thử |
|---|---|---|---|---|
| **1. Unit Test Lõi Tiếng Việt (`BambooEngine`)** | `./build/test_bamboo_engine` | **419 checks** | **419 PASS / 0 FAIL** | Đối chiếu 1:1 với `bamboo-core`: Telex, Simple Telex, VNI, VIQR; backtrack/xoá dấu (`as`→`á`, `asf`→`à`, `asfz`→`a`, `chaanss\b`→`chân`); sửa theo `Backspace` & mũi tên `←`/`→`; mở khoá chính tả khi Backspace cả khi tắt `auto_restore_english`, gõ nhanh đảo nhịp (`truongfw`, `tiengse`, `chuyense`, `nguoifw`, `dduocjw`, `truyeenf`, `truyeefn`, `truyefen`, `truyefne`, `truyenef`, `Hoof Taasn Truyeenf`→`Hồ Tấn Truyền`); tự động mở khoá `wordLockedRaw_` khi từ tiến triển thành âm tiết tiếng Việt hợp lệ; gõ tắt, Macro, chuyển mã và bảo vệ bộ đệm khi tắt `reset_on_mouse_click`. |
| **2. Test Kernel `uinput` → `evdev` Loopback, XKB Overlay, Stress & Fuzz** | `./build/test_uinput_harness` | **488 checks** | **488 PASS / 0 FAIL** | - Kiểm tra toàn bộ **134 ký tự tiếng Việt** qua `libxkbcommon` với `CapsLock OFF/ON` và parser keymap XKB sống / layout GNOME, KDE, X11.<br>- Loopback thật `/dev/uinput` → `/dev/input/eventX` với `EVIOCGRAB` và xung `forwardTapEvent` micro-hold.<br>- Stress test ≥ 160–230 phím/giây và random fuzz test 2.000 phím (**0 phím bị kẹt**).<br>- Kiểm tra round-trip TOML config & Macro import/export. |
| **3. Test End-to-End Daemon + D-Bus CLI + Game Mode + Mouse/Backspace** | `./build/test_e2e_daemon` | **125 checks** | **125 PASS / 0 FAIL** | - Chạy `vnsnowkeyd` trong `dbus-run-session` cô lập với bàn phím & chuột ảo `/dev/uinput`.<br>- Phím tắt toàn cục `Ctrl+Shift`, `Super+Space`, `Ctrl+Alt` (kèm cuộn chuột/nút phụ không bật nhầm), `CapsLock ON/OFF`, hàng đợi Async FIFO Output Worker gõ nhanh liên tục không nghỉ (~166 phím/s) & trên `Brave`/`Firefox` với nhịp `1200µs`, telemetry `xkb_auto_heals`/`stuck_mod_recoveries`/`vi_inactive_reason`, `Enter`/`Tab`/`Esc`, Game Mode, `Safe Mode`, `SIGTERM`. |
| **4. Test Đa Ứng dụng Qt6 Widgets, URL Bar & Focus Tracker** | `./build/test_app_integration` | **153 checks** | **153 PASS / 0 FAIL** | - Gõ tiếng Việt vào `QPlainTextEdit`, mô phỏng thanh tìm kiếm/URL bar trình duyệt với gợi ý autocomplete (`d` + `docs.google.com` + `d` → `đ`, loại bỏ triệt để lỗi `dđ`), giải pháp nguyên tử `KEY_DELETE` xoá gợi ý mà không chèn khoảng trắng, migrate cấu hình `v2/v3→v4`, `FocusTracker` (không tắt nhầm theo tiêu đề app thường, không rò rỉ `EN` sang app mới, bỏ qua popup rỗng), `GameModeMonitor`, `SettingsWindow` (**6 trang**) và `TrayController` (cả `HAVE_KF6_SNI=1` và `HAVE_KF6_SNI=0`). |
| **5. Test Single-Instance `vnsnowkey-gui` & Single Tray trên D-Bus** | `./build/test_gui_single_instance` | **24 checks** | **24 PASS / 0 FAIL** | - Chạy `vnsnowkey-gui` nhiều lần trong `dbus-run-session`: gọi `ActivateSettings()` qua D-Bus, tổng số `StatusNotifierItem` duy nhất bằng **1** và `vnsnowkeyd` không liên kết `Qt6Widgets`/`KF6StatusNotifierItem`. |
| **6. Kiểm tra Biểu tượng Khay hệ thống & App Icon** | `./build/vnsnowkey-gui --test-icons` | **16 render checks + App icon** | **PASS** | Kết xuất SVG đơn sắc `V`/`E` ở `16/22/24/32px` (Dark & Light) và kiểm tra nạp `vnsnowkey.png` / `vnsnowkey.svg`. |

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
7. **Lỗi gợi ý Omnibox / thanh tìm kiếm trình duyệt (`d` + `docs.google.com` + `d` → `dđ`)**:
   - *Phát hiện*: Khi gõ chữ `d` trên thanh địa chỉ hoặc ô tìm kiếm trình duyệt (Brave, Chromium, Chrome, Firefox, Edge), trình duyệt tự chèn gợi ý dạng bôi đen `d[ocs.google.com]`. Khi gõ tiếp `d` để tạo `đ`, IME phát 1 `KEY_BACKSPACE`. Trên các control soạn thảo, `Backspace` khi có vùng chọn chỉ hủy vùng bôi đen `[ocs.google.com]`, để lại `d`, sau đó IME gõ `đ` dẫn tới `dđ`. Nếu dùng `Space` + `Backspace` thì lại phá vỡ văn bản bình thường trong Google Docs hoặc ô tìm kiếm.
   - *Khắc phục*: Áp dụng giải pháp nguyên tử `KEY_DELETE`: khi `autocompleteFix = true` và con trỏ ở cuối từ, IME phát một xung `KEY_DELETE` (xoá sạch vùng chọn gợi ý bên phải con trỏ mà không làm mất ký tự bên trái và không chèn khoảng trắng), sau đó `KEY_BACKSPACE` xoá `d` và gõ `đ` hoàn hảo.
8. **Khắc phục kẹt tiếng Anh khi gõ nhanh (`truyền` bị mất dấu, phải xoá hết hoặc nhấn phím cách)**:
   - *Phát hiện*: Cờ `wordLockedRaw_` trong `SessionProcessor` từng khoá vĩnh viễn bộ đệm sang tiếng Anh khi gặp một bước trung gian tạm thời, khiến người dùng gõ tiếp từ (như `truyeenf`) vẫn bị kẹt ở tiếng Anh cho tới khi xoá hết hoặc nhấn phím cách.
   - *Khắc phục*: Loại bỏ việc kích hoạt `outRestored` khi `auto_restore_english = false`, đồng thời trang bị cơ chế tự động giải phóng (`auto-unlock`) `wordLockedRaw_ = false` ngay khi chuỗi phím tiến triển thành một âm tiết tiếng Việt hợp lệ (kiểm tra qua `extractLastSyllable` và `isValid(false)`), giúp gõ tốc độ cao các từ như `truyền`, `nghiên`, `khoảng` luôn mượt mà 100%.

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
- [ ] **4. Kiểm tra trong Konsole / Terminal**:
  - Mặc định trong cấu hình `app_profiles`, `konsole` được đặt ở chế độ `en` để gõ lệnh shell an toàn.
  - Nhấn `Ctrl+Shift` để chuyển sang `VI` trong Konsole (hoặc chỉnh profile `konsole` thành `default` trong tab **3. Ứng dụng**), gõ `echo "Xin chaof Vieetj Nam"` → hiển thị đúng tiếng Việt, phím `Ctrl+C` / `Ctrl+D` hoạt động bình thường.
- [ ] **5. Kiểm tra trong Trình duyệt (Firefox / Chromium) & Ô địa chỉ (URL Bar)**:
  - Gõ trong ô tìm kiếm trang web: gõ nhanh câu dài ≥ 15 phím/giây.
  - Gõ trên thanh địa chỉ trình duyệt có gợi ý tự động (autocomplete): nếu thấy phím `Backspace` đầu tiên xoá phần gợi ý được bôi đen, mở cửa sổ cài đặt → tab **3. Ứng dụng**, bật `Sửa lỗi URL Autocomplete = true` cho `firefox` / `chromium`.
- [ ] **6. Kiểm tra trong VS Code / Electron, LibreOffice, Telegram & App Xwayland**:
  - Chuyển qua lại giữa cửa sổ VS Code (đặt `EN`) và Telegram / LibreOffice (đặt `VI`): xác nhận VNSnowKey tự động nhớ và chuyển chế độ ngôn ngữ riêng cho từng cửa sổ.
  - Click chuột sang vị trí khác khi đang gõ dở một từ: xác nhận bộ đệm từ được xoá sạch, phím tiếp theo không làm nhảy dấu của từ cũ.
