# VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
# SPDX-License-Identifier: GPL-3.0-or-later

CXX      ?= g++
CC       ?= gcc
MOC      ?= $(shell for p in \
	/usr/lib/qt6/moc \
	/usr/lib/qt6/libexec/moc \
	/usr/lib64/qt6/libexec/moc \
	"$$(pkg-config --variable=libexecdir Qt6Core 2>/dev/null)/moc" \
	"$$(command -v moc-qt6 2>/dev/null)" \
	"$$(command -v moc 2>/dev/null)"; do \
	if [ -x "$$p" ]; then echo "$$p"; break; fi; done)
WAYLAND_SCANNER ?= wayland-scanner
PREFIX   ?= /usr
DESTDIR  ?=
BUILD_DIR = build

CXXFLAGS ?= -O2 -g
CFLAGS   ?= -O2 -g
override CXXFLAGS += -std=c++20 -Wall -Wextra -fPIC
override CFLAGS   += -Wall -Wextra -fPIC
ifeq ($(SANITIZE),1)
override CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
override CFLAGS   += -fsanitize=address,undefined -fno-omit-frame-pointer
override LDFLAGS  += -fsanitize=address,undefined
endif

BASE_CFLAGS := -Isrc $(shell pkg-config --cflags xkbcommon libevdev)
BASE_LIBS   := $(shell pkg-config --libs xkbcommon libevdev) -lpthread

IM_CFLAGS   := -I$(BUILD_DIR) $(shell pkg-config --cflags wayland-client libsystemd)
IM_LIBS     := $(shell pkg-config --libs wayland-client libsystemd)

QT_CORE_CFLAGS := $(shell pkg-config --cflags Qt6Core Qt6DBus)
QT_CORE_LIBS   := $(shell pkg-config --libs Qt6Core Qt6DBus)

HAVE_KF6_SNI ?= $(shell [ -f /usr/include/KF6/KStatusNotifierItem/KStatusNotifierItem ] && echo 1 || echo 0)
ifeq ($(HAVE_KF6_SNI),1)
QT_GUI_CFLAGS  := $(shell pkg-config --cflags Qt6Core Qt6DBus Qt6Gui Qt6Widgets Qt6Svg) -I/usr/include/KF6/KStatusNotifierItem -DVNSNOWKEY_HAVE_KF6_SNI=1
QT_GUI_LIBS    := $(shell pkg-config --libs Qt6Core Qt6DBus Qt6Gui Qt6Widgets Qt6Svg) -lKF6StatusNotifierItem
GUI_SNI_OBJS   :=
else
QT_GUI_CFLAGS  := $(shell pkg-config --cflags Qt6Core Qt6DBus Qt6Gui Qt6Widgets Qt6Svg)
QT_GUI_LIBS    := $(shell pkg-config --libs Qt6Core Qt6DBus Qt6Gui Qt6Widgets Qt6Svg)
GUI_SNI_OBJS   := $(BUILD_DIR)/sni_tray.o $(BUILD_DIR)/moc_sni_tray.o
endif

ENGINE_OBJS := \
	$(BUILD_DIR)/bamboo_utils.o \
	$(BUILD_DIR)/rules_parser.o \
	$(BUILD_DIR)/spelling.o \
	$(BUILD_DIR)/flattener.o \
	$(BUILD_DIR)/bamboo_engine.o \
	$(BUILD_DIR)/charset_encoder.o \
	$(BUILD_DIR)/macro_table.o \
	$(BUILD_DIR)/session_processor.o

CORE_IO_OBJS := \
	$(BUILD_DIR)/config.o \
	$(BUILD_DIR)/xkb_overlay.o \
	$(BUILD_DIR)/uinput_emitter.o \
	$(BUILD_DIR)/evdev_capture.o

DAEMON_OBJS := \
	$(BUILD_DIR)/focus_tracker.o \
	$(BUILD_DIR)/game_mode_monitor.o \
	$(BUILD_DIR)/daemon_server.o \
	$(BUILD_DIR)/moc_daemon_server.o

GUI_OBJS := \
	$(GUI_SNI_OBJS) \
	$(BUILD_DIR)/tray_controller.o \
	$(BUILD_DIR)/settings_window.o \
	$(BUILD_DIR)/main_gui.o \
	$(BUILD_DIR)/moc_tray_controller.o \
	$(BUILD_DIR)/moc_settings_window.o

BINARIES := \
	$(BUILD_DIR)/vnsnowkeyd \
	$(BUILD_DIR)/vnsnowkey \
	$(BUILD_DIR)/vnsnowkey-gui \
	$(BUILD_DIR)/vnsnowkey-im

TEST_BINARIES := \
	$(BUILD_DIR)/test_bamboo_engine \
	$(BUILD_DIR)/test_uinput_harness \
	$(BUILD_DIR)/test_e2e_daemon \
	$(BUILD_DIR)/test_app_integration \
	$(BUILD_DIR)/test_gui_single_instance

.PHONY: all tests test sanitize clean install

all: $(BINARIES) $(TEST_BINARIES)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Engine objects
$(BUILD_DIR)/%.o: src/engine/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) -c $< -o $@

# Config, Input, Output objects
$(BUILD_DIR)/config.o: src/config/config.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/xkb_overlay.o: src/output/xkb_overlay.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/uinput_emitter.o: src/output/uinput_emitter.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/evdev_capture.o: src/input/evdev_capture.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) -c $< -o $@

# Focus & Daemon objects
$(BUILD_DIR)/focus_tracker.o: src/focus/focus_tracker.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(QT_CORE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/game_mode_monitor.o: src/focus/game_mode_monitor.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/daemon_server.o: src/daemon/daemon_server.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(QT_CORE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/main_daemon.o: src/daemon/main_daemon.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(QT_CORE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/main_cli.o: src/cli/main_cli.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(QT_CORE_CFLAGS) -c $< -o $@

# Wayland Input Method bridge (zwp_input_method_v1)
$(BUILD_DIR)/input-method-unstable-v1-client-protocol.h: src/im/input-method-unstable-v1.xml | $(BUILD_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(BUILD_DIR)/input-method-unstable-v1-client-protocol.c: src/im/input-method-unstable-v1.xml | $(BUILD_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(BUILD_DIR)/input-method-unstable-v1-client-protocol.o: $(BUILD_DIR)/input-method-unstable-v1-client-protocol.c $(BUILD_DIR)/input-method-unstable-v1-client-protocol.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(IM_CFLAGS) -c $< -o $@

$(BUILD_DIR)/main_im.o: src/im/main_im.cpp $(BUILD_DIR)/input-method-unstable-v1-client-protocol.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(IM_CFLAGS) -c $< -o $@

# MOC generation
$(BUILD_DIR)/moc_daemon_server.cpp: src/daemon/daemon_server.hpp | $(BUILD_DIR)
	$(MOC) $(QT_CORE_CFLAGS) $< -o $@

$(BUILD_DIR)/moc_tray_controller.cpp: src/gui/tray_controller.hpp | $(BUILD_DIR)
	$(MOC) $(QT_GUI_CFLAGS) $< -o $@

$(BUILD_DIR)/moc_settings_window.cpp: src/gui/settings_window.hpp | $(BUILD_DIR)
	$(MOC) $(QT_GUI_CFLAGS) $< -o $@

$(BUILD_DIR)/moc_sni_tray.cpp: src/gui/sni_tray.hpp | $(BUILD_DIR)
	$(MOC) $(QT_GUI_CFLAGS) $< -o $@

$(BUILD_DIR)/moc_daemon_server.o: $(BUILD_DIR)/moc_daemon_server.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(QT_CORE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/moc_tray_controller.o: $(BUILD_DIR)/moc_tray_controller.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(QT_GUI_CFLAGS) -c $< -o $@

$(BUILD_DIR)/moc_settings_window.o: $(BUILD_DIR)/moc_settings_window.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(QT_GUI_CFLAGS) -c $< -o $@

$(BUILD_DIR)/moc_sni_tray.o: $(BUILD_DIR)/moc_sni_tray.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(QT_GUI_CFLAGS) -c $< -o $@

# GUI objects
$(BUILD_DIR)/sni_tray.o: src/gui/sni_tray.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(QT_GUI_CFLAGS) -c $< -o $@

$(BUILD_DIR)/tray_controller.o: src/gui/tray_controller.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(QT_GUI_CFLAGS) -c $< -o $@

$(BUILD_DIR)/settings_window.o: src/gui/settings_window.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(QT_GUI_CFLAGS) -c $< -o $@

$(BUILD_DIR)/main_gui.o: src/gui/main_gui.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(QT_GUI_CFLAGS) -c $< -o $@

# Test objects
$(BUILD_DIR)/test_bamboo_engine.o: tests/test_bamboo_engine.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_uinput_harness.o: tests/test_uinput_harness.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_e2e_daemon.o: tests/test_e2e_daemon.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_app_integration.o: tests/test_app_integration.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(QT_GUI_CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_gui_single_instance.o: tests/test_gui_single_instance.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(QT_CORE_CFLAGS) $(if $(filter 1,$(HAVE_KF6_SNI)),-DVNSNOWKEY_HAVE_KF6_SNI=1) -c $< -o $@

# Link targets
$(BUILD_DIR)/vnsnowkeyd: $(ENGINE_OBJS) $(CORE_IO_OBJS) $(DAEMON_OBJS) $(BUILD_DIR)/main_daemon.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(BASE_LIBS) $(QT_CORE_LIBS) -o $@

$(BUILD_DIR)/vnsnowkey: $(ENGINE_OBJS) $(CORE_IO_OBJS) $(DAEMON_OBJS) $(BUILD_DIR)/main_cli.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(BASE_LIBS) $(QT_CORE_LIBS) -o $@

$(BUILD_DIR)/vnsnowkey-gui: $(ENGINE_OBJS) $(BUILD_DIR)/config.o $(BUILD_DIR)/xkb_overlay.o $(GUI_OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(BASE_LIBS) $(QT_GUI_LIBS) -o $@

$(BUILD_DIR)/vnsnowkey-im: $(BUILD_DIR)/main_im.o $(BUILD_DIR)/input-method-unstable-v1-client-protocol.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(IM_LIBS) -o $@

$(BUILD_DIR)/test_bamboo_engine: $(BUILD_DIR)/test_bamboo_engine.o $(ENGINE_OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(BASE_LIBS) -o $@

$(BUILD_DIR)/test_uinput_harness: $(BUILD_DIR)/test_uinput_harness.o $(ENGINE_OBJS) $(CORE_IO_OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(BASE_LIBS) -o $@

$(BUILD_DIR)/test_e2e_daemon: $(BUILD_DIR)/test_e2e_daemon.o $(ENGINE_OBJS) $(CORE_IO_OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(BASE_LIBS) -o $@

$(BUILD_DIR)/test_app_integration: $(BUILD_DIR)/test_app_integration.o $(ENGINE_OBJS) $(CORE_IO_OBJS) $(BUILD_DIR)/focus_tracker.o $(BUILD_DIR)/game_mode_monitor.o $(GUI_SNI_OBJS) $(BUILD_DIR)/tray_controller.o $(BUILD_DIR)/settings_window.o $(BUILD_DIR)/moc_tray_controller.o $(BUILD_DIR)/moc_settings_window.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(BASE_LIBS) $(QT_GUI_LIBS) -o $@

$(BUILD_DIR)/test_gui_single_instance: $(BUILD_DIR)/test_gui_single_instance.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(QT_CORE_LIBS) -o $@

tests: $(TEST_BINARIES)

test: all
	./$(BUILD_DIR)/test_bamboo_engine
	./$(BUILD_DIR)/test_uinput_harness
	./$(BUILD_DIR)/test_e2e_daemon
	QT_QPA_PLATFORM=offscreen ./$(BUILD_DIR)/test_app_integration
	./$(BUILD_DIR)/test_gui_single_instance
	QT_QPA_PLATFORM=offscreen ./$(BUILD_DIR)/vnsnowkey-gui --test-icons

sanitize:
	$(MAKE) clean
	$(MAKE) -j$$(nproc) SANITIZE=1 all
	./$(BUILD_DIR)/test_bamboo_engine
	./$(BUILD_DIR)/test_uinput_harness
	./$(BUILD_DIR)/test_e2e_daemon
	QT_QPA_PLATFORM=offscreen ./$(BUILD_DIR)/test_app_integration
	./$(BUILD_DIR)/test_gui_single_instance
	QT_QPA_PLATFORM=offscreen ./$(BUILD_DIR)/vnsnowkey-gui --test-icons

install: $(BINARIES)
	install -Dm755 $(BUILD_DIR)/vnsnowkeyd "$(DESTDIR)$(PREFIX)/bin/vnsnowkeyd"
	install -Dm755 $(BUILD_DIR)/vnsnowkey "$(DESTDIR)$(PREFIX)/bin/vnsnowkey"
	install -Dm755 $(BUILD_DIR)/vnsnowkey-gui "$(DESTDIR)$(PREFIX)/bin/vnsnowkey-gui"
	install -Dm755 $(BUILD_DIR)/vnsnowkey-im "$(DESTDIR)$(PREFIX)/bin/vnsnowkey-im"
	install -Dm644 packaging/70-vnsnowkey-uinput.rules "$(DESTDIR)$(PREFIX)/lib/udev/rules.d/70-vnsnowkey-uinput.rules"
	install -Dm644 packaging/vnsnowkey-uinput.conf "$(DESTDIR)$(PREFIX)/lib/modules-load.d/vnsnowkey-uinput.conf"
	install -Dm644 packaging/vnsnowkeyd.service "$(DESTDIR)$(PREFIX)/lib/systemd/user/vnsnowkeyd.service"
	install -Dm644 packaging/vnsnowkey-gui.service "$(DESTDIR)$(PREFIX)/lib/systemd/user/vnsnowkey-gui.service"
	install -Dm644 packaging/org.vnsnowkey.App.desktop "$(DESTDIR)$(PREFIX)/share/applications/org.vnsnowkey.App.desktop"
	install -Dm644 packaging/org.vnsnowkey.InputMethod.desktop "$(DESTDIR)$(PREFIX)/share/applications/org.vnsnowkey.InputMethod.desktop"
	install -Dm644 packaging/kwin-script/vnsnowkey-focus.js "$(DESTDIR)$(PREFIX)/share/vnsnowkey/kwin/vnsnowkey-focus.js"
	install -Dm644 packaging/gnome-shell-extension/vnsnowkey-focus@vnsnowkey.org/metadata.json "$(DESTDIR)$(PREFIX)/share/gnome-shell/extensions/vnsnowkey-focus@vnsnowkey.org/metadata.json"
	install -Dm644 packaging/gnome-shell-extension/vnsnowkey-focus@vnsnowkey.org/extension.js "$(DESTDIR)$(PREFIX)/share/gnome-shell/extensions/vnsnowkey-focus@vnsnowkey.org/extension.js"
	install -Dm644 assets/icons/vnsnowkey.svg "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/vnsnowkey.svg"
	install -Dm644 assets/icons/vnsnowkey.png "$(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/vnsnowkey.png"
	install -Dm644 assets/icons/vnsnowkey-vi-symbolic.svg "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/status/vnsnowkey-vi-symbolic.svg"
	install -Dm644 assets/icons/vnsnowkey-en-symbolic.svg "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/status/vnsnowkey-en-symbolic.svg"
	install -Dm644 assets/icons/vnsnowkey.png "$(DESTDIR)$(PREFIX)/share/vnsnowkey/icons/vnsnowkey.png"
	install -Dm644 assets/icons/vnsnowkey.svg "$(DESTDIR)$(PREFIX)/share/vnsnowkey/icons/vnsnowkey.svg"
	install -Dm644 assets/icons/vnsnowkey-vi-dark.svg "$(DESTDIR)$(PREFIX)/share/vnsnowkey/icons/vnsnowkey-vi-dark.svg"
	install -Dm644 assets/icons/vnsnowkey-vi-light.svg "$(DESTDIR)$(PREFIX)/share/vnsnowkey/icons/vnsnowkey-vi-light.svg"
	install -Dm644 assets/icons/vnsnowkey-en-dark.svg "$(DESTDIR)$(PREFIX)/share/vnsnowkey/icons/vnsnowkey-en-dark.svg"
	install -Dm644 assets/icons/vnsnowkey-en-light.svg "$(DESTDIR)$(PREFIX)/share/vnsnowkey/icons/vnsnowkey-en-light.svg"
	install -Dm644 assets/icons/vnsnowkey-vi-symbolic.svg "$(DESTDIR)$(PREFIX)/share/vnsnowkey/icons/vnsnowkey-vi-symbolic.svg"
	install -Dm644 assets/icons/vnsnowkey-en-symbolic.svg "$(DESTDIR)$(PREFIX)/share/vnsnowkey/icons/vnsnowkey-en-symbolic.svg"
	install -Dm644 LICENSE "$(DESTDIR)$(PREFIX)/share/licenses/vnsnowkey/LICENSE"
	install -Dm644 CREDITS "$(DESTDIR)$(PREFIX)/share/licenses/vnsnowkey/CREDITS"

clean:
	rm -rf $(BUILD_DIR)
