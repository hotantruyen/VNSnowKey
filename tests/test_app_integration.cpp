/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Multi-Application Widget, Browser URL Bar Autocomplete, GUI Settings & KWin D-Bus Integration Test
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "../src/config/config.hpp"
#include "../src/engine/bamboo_utils.hpp"
#include "../src/engine/charset_encoder.hpp"
#include "../src/engine/rules_parser.hpp"
#include "../src/engine/session_processor.hpp"
#include "../src/focus/focus_tracker.hpp"
#include "../src/focus/game_mode_monitor.hpp"
#include "../src/focus/xwayland_env.hpp"
#include "../src/gui/settings_window.hpp"
#include "../src/gui/tray_controller.hpp"
#include "../src/output/uinput_emitter.hpp"
#include "../src/output/xkb_overlay.hpp"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

using namespace vnsnowkey;
using namespace vnsnowkey::bamboo;

namespace {

int g_passed = 0;
int g_failed = 0;

void checkEq(const std::string& actual, const std::string& expected, const char* expr, int line) {
    if (actual == expected) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "[FAIL] Line " << line << " (" << expr << "): got [" << actual
                  << "], expected [" << expected << "]\n";
    }
}

void checkBool(bool actual, bool expected, const char* expr, int line) {
    if (actual == expected) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "[FAIL] Line " << line << " (" << expr << "): got ["
                  << (actual ? "true" : "false") << "], expected ["
                  << (expected ? "true" : "false") << "]\n";
    }
}

#define EXPECT_STR_EQ(a, b) checkEq((a), (b), #a, __LINE__)
#define EXPECT_BOOL_EQ(a, b) checkBool((a), (b), #a, __LINE__)

// Simulate a Browser URL Bar (Firefox / Chromium address bar) with inline autocomplete selection:
// Whenever the user types a character that matches a prefix of "https://vietnam.gov.vn",
// the browser appends the remaining suffix and selects (highlights) the suffix.
// Therefore, when a single Backspace arrives while a selection is active, the first Backspace
// only deletes the highlighted autocomplete suffix, and the next Backspace deletes the character!
class SimulatedBrowserUrlBar {
public:
    QLineEdit lineEdit;
    std::string autocompleteSuggestion = "vietnam.gov.vn";

    void applyInlineAutocomplete() {
        std::string cur = lineEdit.text().toStdString();
        if (!cur.empty() && cur.size() < autocompleteSuggestion.size() &&
            autocompleteSuggestion.rfind(cur, 0) == 0) {
            int typedLen = QString::fromStdString(cur).length();
            QString full = QString::fromStdString(autocompleteSuggestion);
            lineEdit.setText(full);
            lineEdit.setSelection(typedLen, full.length() - typedLen);
        }
    }

    void sendDelete() {
        if (lineEdit.hasSelectedText()) {
            // KEY_DELETE removes the selected autocomplete suffix without deleting any character before cursor!
            int start = lineEdit.selectionStart();
            QString t = lineEdit.text();
            t.remove(start, lineEdit.selectedText().length());
            lineEdit.setText(t);
            lineEdit.setCursorPosition(start);
        } else {
            lineEdit.del();
        }
    }

    void sendBackspace() {
        if (lineEdit.hasSelectedText()) {
            // Browser URL bar behavior: first Backspace removes only the selected autocomplete suffix!
            int start = lineEdit.selectionStart();
            QString t = lineEdit.text();
            t.remove(start, lineEdit.selectedText().length());
            lineEdit.setText(t);
            lineEdit.setCursorPosition(start);
        } else {
            lineEdit.backspace();
        }
    }

    void insertText(const QString& s, bool triggerAutocomplete = true) {
        if (lineEdit.hasSelectedText()) {
            int start = lineEdit.selectionStart();
            QString t = lineEdit.text();
            t.remove(start, lineEdit.selectedText().length());
            lineEdit.setText(t);
            lineEdit.setCursorPosition(start);
        }
        lineEdit.insert(s);
        if (triggerAutocomplete) {
            applyInlineAutocomplete();
        }
    }
};

void testBrowserUrlBarAutocompleteFix() {
    SimulatedBrowserUrlBar urlBar;
    SessionProcessor sp;
    SessionOptions opts;
    opts.inputMethodName = "Telex";
    sp.setOptions(opts);

    // 1. User types "vieetj" in a browser URL bar that autocompletes "vietnam.gov.vn" on "v", "vi", "vie"
    // With autocompleteFix = true, VNSnowKey sends KEY_DELETE before Backspaces to eliminate autocomplete selection
    const std::string raw = "vieetj";
    for (char32_t c : utf8ToU32(raw)) {
        KeyActionDiff diff = sp.processPrintableKey(c);
        if (diff.passThrough) {
            urlBar.insertText(QString::fromStdString(u32CharToUtf8(c)), true);
        } else {
            // When autocompleteFix is enabled, KEY_DELETE cancels inline autocomplete suggestion cleanly:
            urlBar.sendDelete();
            for (int i = 0; i < diff.backspaces; ++i) {
                urlBar.sendBackspace();
            }
            urlBar.insertText(QString::fromStdString(u32ToUtf8(diff.textToEmit)), true);
        }
    }
    EXPECT_STR_EQ(urlBar.lineEdit.text().toStdString(), "việt");

    // 2. Critical bug reproduction: typing 'd' (browser autocompletes "docs.google.com") + 'd' -> must produce 'đ', NOT 'dđ'!
    urlBar.lineEdit.clear();
    urlBar.autocompleteSuggestion = "docs.google.com";
    sp.reset();
    // Type first 'd'
    KeyActionDiff diff1 = sp.processPrintableKey(U'd');
    urlBar.insertText("d", true);
    EXPECT_STR_EQ(urlBar.lineEdit.text().toStdString(), "docs.google.com");
    EXPECT_BOOL_EQ(urlBar.lineEdit.hasSelectedText(), true); // "[ocs.google.com]" is highlighted
    // Type second 'd' -> with KEY_DELETE autocompleteFix, selection is deleted, 'd' is backspaced, and 'đ' is typed!
    KeyActionDiff diff2 = sp.processPrintableKey(U'd');
    EXPECT_BOOL_EQ(diff2.backspaces == 1, true);
    urlBar.sendDelete(); // UinputEmitter sends KEY_DELETE
    for (int i = 0; i < diff2.backspaces; ++i) {
        urlBar.sendBackspace();
    }
    urlBar.insertText(QString::fromStdString(u32ToUtf8(diff2.textToEmit)), true);
    EXPECT_STR_EQ(urlBar.lineEdit.text().toStdString(), "đ"); // Must be "đ", NOT "dđ"!

    // Now test Backspace inside browser URL bar while inline autocomplete highlight is active!
    // User types "vie" -> browser selects "tnam.gov.vn" ("vie[tnam.gov.vn]").
    // Physical Backspace in browser URL bar:
    //   BUG without autocompleteFix on passThrough Backspace: 1 Backspace only clears selection "[tnam.gov.vn]" leaving "vie" while engine drops to "vi"!
    //   FIXED with autocompleteFix: passThrough Backspace sends KEY_DELETE first + deletes 'e', keeping URL bar ("vi") synced with engine ("vi")!
    urlBar.lineEdit.clear();
    urlBar.autocompleteSuggestion = "vietnam.gov.vn";
    sp.reset();
    auto simulateUrlBarKey = [&](char32_t c, bool isBackspace, bool autocompleteFixEnabled) {
        KeyActionDiff diff = isBackspace ? sp.processBackspace() : sp.processPrintableKey(c);
        if (diff.passThrough) {
            if (isBackspace) {
                if (autocompleteFixEnabled) {
                    urlBar.sendDelete();
                }
                urlBar.sendBackspace();
            } else {
                urlBar.insertText(QString::fromStdString(u32CharToUtf8(c)), true);
            }
        } else {
            if (autocompleteFixEnabled) {
                urlBar.sendDelete();
            }
            for (int i = 0; i < diff.backspaces; ++i) {
                urlBar.sendBackspace();
            }
            urlBar.insertText(QString::fromStdString(u32ToUtf8(diff.textToEmit)), true);
        }
    };

    // Type "vie" -> inline autocomplete shows "vie[tnam.gov.vn]"
    for (char32_t c : utf8ToU32("vie")) simulateUrlBarKey(c, false, true);
    EXPECT_BOOL_EQ(urlBar.lineEdit.hasSelectedText(), true);
    // Physical Backspace with autocompleteFix -> must leave "vi", NOT "vie"!
    simulateUrlBarKey(0, true, true);
    // Verify continuing with "eets" produces "viết" -> Backspace deletes 't' ("viế") + "jt" -> "việt"
    for (char32_t c : utf8ToU32("eets")) simulateUrlBarKey(c, false, true);
    EXPECT_STR_EQ(urlBar.lineEdit.text().toStdString(), "viết");
    simulateUrlBarKey(0, true, true);
    simulateUrlBarKey(U'j', false, true);
    simulateUrlBarKey(U't', false, true);
    EXPECT_STR_EQ(urlBar.lineEdit.text().toStdString(), "việt");
}

void testQtWidgetsMultiLineEditorAndSettingsWindow(const std::string& tmpDir) {
    ::setenv("XDG_CONFIG_HOME", tmpDir.c_str(), 1);

    // 1. Test Qt6 QPlainTextEdit receiving multi-sentence Vietnamese input
    QPlainTextEdit editor;
    SessionProcessor sp;
    SessionOptions opts;
    opts.inputMethodName = "Telex";
    opts.autoRestoreEnglish = true;
    sp.setOptions(opts);

    const std::string input = "Chaof buooir sangs treen KDE Plasma Wayland!\nGox tieengs Vieetj trong Qt6 vaf Xwayland.";
    for (char32_t c : utf8ToU32(input)) {
        if (c == U'\n') {
            sp.reset();
            editor.insertPlainText(QStringLiteral("\n"));
            continue;
        }
        KeyActionDiff diff = sp.processPrintableKey(c);
        if (diff.passThrough) {
            editor.insertPlainText(QString::fromStdString(u32CharToUtf8(c)));
        } else {
            for (int i = 0; i < diff.backspaces; ++i) {
                editor.textCursor().deletePreviousChar();
            }
            editor.insertPlainText(QString::fromStdString(u32ToUtf8(diff.textToEmit)));
        }
    }
    EXPECT_STR_EQ(
        editor.toPlainText().toStdString(),
        "Chào buổi sáng trên KDE Plasma Wayland!\nGõ tiếng Việt trong Qt6 và Xwayland.");

    // 2. Test instantiating SettingsWindow & TrayController without relying on distro icon themes
    const QString prevIconTheme = QIcon::themeName();
    QIcon::setThemeName(QStringLiteral("vnsnowkey-nonexistent-theme"));
    SettingsWindow win;
    TrayController tray(&win);
    win.updateDaemonLiveStatus(QStringLiteral("vi"), QStringLiteral("Telex"), false);
    EXPECT_BOOL_EQ(win.windowTitle().contains(QStringLiteral("VNSnowKey")), true);

    QListWidget* navSidebar = win.findChild<QListWidget*>(QStringLiteral("navSidebar"));
    EXPECT_BOOL_EQ(navSidebar != nullptr && navSidebar->count() == 7, true);
    if (navSidebar) {
        for (int i = 0; i < navSidebar->count(); ++i) {
            QListWidgetItem* item = navSidebar->item(i);
            EXPECT_BOOL_EQ(item != nullptr && !item->icon().isNull() &&
                               !item->icon().pixmap(18, 18).isNull(),
                           true);
        }
    }

    // Header badge must be visible/non-null on both light (e.g. Fedora) and dark (e.g. Ubuntu) palettes
    QPalette lightPal = win.palette();
    lightPal.setColor(QPalette::Window, QColor(246, 245, 244));
    win.setPalette(lightPal);
    win.updateDaemonLiveStatus(QStringLiteral("vi"), QStringLiteral("Telex"), false);
    QFrame* hdrBar = win.findChild<QFrame*>(QStringLiteral("headerBar"));
    QLabel* hdrBadge = hdrBar ? hdrBar->findChildren<QLabel*>().value(0, nullptr) : nullptr;
    EXPECT_BOOL_EQ(hdrBadge != nullptr && !hdrBadge->pixmap().isNull(), true);
    QIcon::setThemeName(prevIconTheme);

    // Verify all engine input methods & charsets are present in SettingsWindow without broken IDs
    auto combos = win.findChildren<QComboBox*>();
    bool hasSimpleTelex = false, hasBrokenSimpleTelex = false, hasTelex2 = false, hasTelexVniViqr = false;
    bool hasTcvn3Abc = false, hasBrokenTcvn3 = false, hasUnicodeComposite = false, hasCp1258 = false;
    for (QComboBox* cb : combos) {
        if (cb->findData(QStringLiteral("Simple Telex")) >= 0) hasSimpleTelex = true;
        if (cb->findData(QStringLiteral("SimpleTelex")) >= 0) hasBrokenSimpleTelex = true;
        if (cb->findData(QStringLiteral("Telex 2")) >= 0) hasTelex2 = true;
        if (cb->findData(QStringLiteral("Telex + VNI + VIQR")) >= 0) hasTelexVniViqr = true;
        if (cb->findData(QStringLiteral("TCVN3 (ABC)")) >= 0) hasTcvn3Abc = true;
        if (cb->findData(QStringLiteral("TCVN3")) >= 0) hasBrokenTcvn3 = true;
        if (cb->findData(QStringLiteral("Unicode tổ hợp")) >= 0) hasUnicodeComposite = true;
        if (cb->findData(QStringLiteral("Windows 1258 codepage")) >= 0) hasCp1258 = true;
    }
    EXPECT_BOOL_EQ(hasSimpleTelex, true);
    EXPECT_BOOL_EQ(hasBrokenSimpleTelex, false);
    EXPECT_BOOL_EQ(hasTelex2, true);
    EXPECT_BOOL_EQ(hasTelexVniViqr, true);
    EXPECT_BOOL_EQ(hasTcvn3Abc, true);
    EXPECT_BOOL_EQ(hasBrokenTcvn3, false);
    EXPECT_BOOL_EQ(hasUnicodeComposite, true);
    EXPECT_BOOL_EQ(hasCp1258, true);

    // Config::fromToml normalizes legacy SimpleTelex/TCVN3 names and migrates v1 profiles
    Config migrated = Config::fromToml(
        "[general]\ninput_method = \"SimpleTelex\"\noutput_charset = \"TCVN3\"\n"
        "[[app_profiles]]\napp_class = \"firefox\"\nmode = \"default\"\n");
    EXPECT_STR_EQ(migrated.inputMethod, "Simple Telex");
    EXPECT_STR_EQ(migrated.outputCharset, "TCVN3 (ABC)");
    EXPECT_BOOL_EQ(migrated.configVersion == Config::kCurrentConfigVersion, true);
    const AppProfile* migratedMc = migrated.findProfileForApp("Minecraft* 26.3");
    EXPECT_BOOL_EQ(migratedMc != nullptr && migratedMc->mode == AppMode::GameMode &&
                       migratedMc->delayUs == 2000,
                   true);

    // Explicit legacy config migrating to v4 sets browser autocomplete_fix = true (KEY_DELETE atomic workaround)
    // and default_delay_us = 0 -> 1200.
    Config v2Custom = Config::fromToml(
        "[general]\nconfig_version = 2\n"
        "[output]\ndefault_delay_us = 0\n"
        "[[app_profiles]]\napp_class = \"firefox\"\nmode = \"default\"\nautocomplete_fix = true\n"
        "[[app_profiles]]\napp_class = \"brave\"\nmode = \"default\"\nautocomplete_fix = true\n");
    EXPECT_BOOL_EQ(v2Custom.findProfileForApp("minecraft") == nullptr, true);
    EXPECT_BOOL_EQ(v2Custom.defaultDelayUs == 0, true);
    const AppProfile* v2Ff = v2Custom.findProfileForApp("firefox");
    const AppProfile* v2Br = v2Custom.findProfileForApp("brave");
    EXPECT_BOOL_EQ(v2Ff != nullptr && v2Ff->autocompleteFix, true);
    EXPECT_BOOL_EQ(v2Br != nullptr && v2Br->autocompleteFix, true);
    EXPECT_BOOL_EQ(Config::createDefault().defaultDelayUs == 0, true);

    // 3. Test Dirty Tracking on "Lưu & Áp dụng" button (must be disabled when unchanged!)
    QPushButton* applyBtn = win.applyButton();
    EXPECT_BOOL_EQ(applyBtn != nullptr, true);
    if (applyBtn) {
        EXPECT_BOOL_EQ(win.isDirty(), false);
        EXPECT_BOOL_EQ(applyBtn->isEnabled(), false);

        auto checks = win.findChildren<QCheckBox*>();
        EXPECT_BOOL_EQ(!checks.isEmpty(), true);
        if (!checks.isEmpty()) {
            bool orig = checks.first()->isChecked();
            checks.first()->setChecked(!orig);
            EXPECT_BOOL_EQ(win.isDirty(), true);
            EXPECT_BOOL_EQ(applyBtn->isEnabled(), true);

            // Reverting back without applying must dim the Save button again
            checks.first()->setChecked(orig);
            EXPECT_BOOL_EQ(win.isDirty(), false);
            EXPECT_BOOL_EQ(applyBtn->isEnabled(), false);

            // Change again, click Apply -> saves & dims button again
            checks.first()->setChecked(!orig);
            EXPECT_BOOL_EQ(applyBtn->isEnabled(), true);
            applyBtn->click();
            EXPECT_BOOL_EQ(win.isDirty(), false);
            EXPECT_BOOL_EQ(applyBtn->isEnabled(), false);
        }
    }
}

void testKWinFocusTrackerAndPerAppLanguageMemory() {
    Config cfg = Config::createDefault();
    cfg.rememberPerAppLang = true;
    cfg.bypassPasswordDialogs = true;
    AppProfile konsoleProf;
    konsoleProf.appClass = "konsole";
    konsoleProf.mode = AppMode::AlwaysEn;
    cfg.appProfiles.push_back(konsoleProf);

    FocusTracker tracker;
    bool callbackCalled = false;
    bool lastLangState = true;
    tracker.setOnFocusChanged([&](const WindowFocusInfo&, bool newVie, bool) {
        callbackCalled = true;
        lastLangState = newVie;
    });

    // 1. Focus Firefox (default VI), user switches to VI
    tracker.onWindowFocusChanged("org.mozilla.firefox", "firefox", "Mozilla Firefox", "w1", cfg, true);
    tracker.recordAppLanguageState(true);
    EXPECT_BOOL_EQ(callbackCalled, true);

    // 2. Focus Konsole (configured AlwaysEn in default profiles!)
    tracker.onWindowFocusChanged("org.kde.konsole", "konsole", "bash — Konsole", "w2", cfg, true);
    EXPECT_BOOL_EQ(lastLangState, false);

    // 3. Switch back to Firefox -> remembers VI!
    tracker.onWindowFocusChanged("org.mozilla.firefox", "firefox", "Mozilla Firefox", "w1", cfg, false);
    EXPECT_BOOL_EQ(lastLangState, true);

    // 4. Focus a Password Authentication dialog (ksshaskpass / polkit) -> auto-bypasses to EN!
    tracker.onWindowFocusChanged("org.kde.polkit-kde-authentication-agent-1", "polkit", "Authentication Required", "w3", cfg, true);
    EXPECT_BOOL_EQ(tracker.currentFocus().isPasswordDialog, true);
    EXPECT_BOOL_EQ(lastLangState, false);

    // 5. Normal browser/chat tab title containing "Xác thực" or "Nhập mật khẩu" must NOT disable Vietnamese!
    tracker.onWindowFocusChanged("org.mozilla.firefox", "firefox", "Xác thực OTP - Firefox", "w1", cfg, false);
    EXPECT_BOOL_EQ(tracker.currentFocus().isPasswordDialog, false);
    EXPECT_BOOL_EQ(lastLangState, true);

    tracker.onWindowFocusChanged("Zalo", "zalo", "Nhập mật khẩu sao lưu - Zalo", "w4", cfg, true);
    EXPECT_BOOL_EQ(tracker.currentFocus().isPasswordDialog, false);
    EXPECT_BOOL_EQ(lastLangState, true);

    // Unlisted normal apps (Dolphin, Kate, Thunderbird, Zalo.exe) must NOT trigger caption password detection
    tracker.onWindowFocusChanged("org.kde.dolphin", "dolphin", "Xác thực chứng chỉ — Dolphin", "w4b", cfg, true);
    EXPECT_BOOL_EQ(tracker.currentFocus().isPasswordDialog, false);
    EXPECT_BOOL_EQ(lastLangState, true);

    tracker.onWindowFocusChanged("org.kde.kate", "kate", "Nhập mật khẩu.txt — Kate", "w4c", cfg, true);
    EXPECT_BOOL_EQ(tracker.currentFocus().isPasswordDialog, false);
    EXPECT_BOOL_EQ(lastLangState, true);

    // System wrapper dialog / unclassed dialog with auth caption still triggers password bypass cleanly
    tracker.onWindowFocusChanged("org.kde.kdialog", "kdialog", "Nhập mật khẩu root", "w4d", cfg, true);
    EXPECT_BOOL_EQ(tracker.currentFocus().isPasswordDialog, true);
    EXPECT_BOOL_EQ(lastLangState, false);

    // Transient empty-class non-password popup must NOT clear currentFocus or flip VI/EN
    tracker.onWindowFocusChanged("org.kde.kate", "kate", "Notes.txt", "w4c", cfg, false);
    EXPECT_BOOL_EQ(lastLangState, true);
    tracker.onWindowFocusChanged("", "", "Popup", "w_pop", cfg, true);
    EXPECT_STR_EQ(tracker.currentFocus().appClass, "org.kde.kate");

    // 6. Entering Konsole (AlwaysEn) or toggling EN inside a Default app with rememberPerAppLang=true
    // must NOT leak EN into subsequent brand-new Default apps!
    tracker.onWindowFocusChanged("org.kde.konsole", "konsole", "bash — Konsole", "w2", cfg, true);
    EXPECT_BOOL_EQ(lastLangState, false);
    tracker.recordAppLanguageState(false, true);
    tracker.onWindowFocusChanged("libreoffice-writer", "soffice.bin", "Document1 - LibreOffice Writer", "w5", cfg, false);
    EXPECT_BOOL_EQ(lastLangState, true);

    // 7. Manual toggle to EN in one Default app (kate) with rememberPerAppLang=true preserves VI for brand-new kwrite
    tracker.onWindowFocusChanged("org.kde.kate", "kate", "Notes.txt", "w4c", cfg, false);
    tracker.recordAppLanguageState(false, true);
    tracker.onWindowFocusChanged("org.kde.kwrite", "kwrite", "Untitled", "w6", cfg, false);
    EXPECT_BOOL_EQ(lastLangState, true);
}

void testGameModeSystemSignalFusionAndMonitor(const std::string& tmpDir) {
    // 1. Inactive GameMode must never activate chat mode
    {
        GameSignalSnapshot snap;
        snap.gameModeActive = false;
        snap.hasX11WindowFocus = true;
        snap.x11CursorVisible = true;
        auto dec = evaluateGameChatDecision(snap);
        EXPECT_BOOL_EQ(dec.chatActive, false);
        EXPECT_BOOL_EQ(dec.source == GameSignalSource::None, true);
    }

    // 2. XWayland focus + hidden cursor (3D camera locked) => raw gameplay
    {
        GameSignalSnapshot snap;
        snap.gameModeActive = true;
        snap.hasX11WindowFocus = true;
        snap.x11CursorVisible = false;
        auto dec = evaluateGameChatDecision(snap);
        EXPECT_BOOL_EQ(dec.chatActive, false);
        EXPECT_BOOL_EQ(dec.source == GameSignalSource::XWaylandCursor, true);
    }

    // 3. XWayland focus + visible cursor (Chat / Book / Sign / Inventory) => Vietnamese chat
    {
        GameSignalSnapshot snap;
        snap.gameModeActive = true;
        snap.hasX11WindowFocus = true;
        snap.x11CursorVisible = true;
        auto dec = evaluateGameChatDecision(snap);
        EXPECT_BOOL_EQ(dec.chatActive, true);
        EXPECT_BOOL_EQ(dec.source == GameSignalSource::XWaylandCursor, true);
    }

    // 4. Wayland-native focus + KWin VirtualKeyboard text-input transitions
    {
        GameSignalSnapshot snap;
        snap.gameModeActive = true;
        snap.hasX11WindowFocus = false;
        snap.hasKWinTextInputTransition = true;
        snap.kwinTextInputSupported = true;
        auto decOpen = evaluateGameChatDecision(snap);
        EXPECT_BOOL_EQ(decOpen.chatActive, true);
        EXPECT_BOOL_EQ(decOpen.source == GameSignalSource::KWinTextInput, true);

        snap.kwinTextInputSupported = false;
        auto decClose = evaluateGameChatDecision(snap);
        EXPECT_BOOL_EQ(decClose.chatActive, false);
        EXPECT_BOOL_EQ(decClose.source == GameSignalSource::KWinTextInput, true);
    }

    // 4b. Wayland-native focus + standalone zwp_input_method_v1 bridge (vnsnowkey-im)
    {
        GameSignalSnapshot snap;
        snap.gameModeActive = true;
        snap.hasX11WindowFocus = false;
        snap.hasWaylandImBridge = true;
        snap.waylandImTextInputActive = false;
        auto dec3d = evaluateGameChatDecision(snap);
        EXPECT_BOOL_EQ(dec3d.chatActive, false);
        EXPECT_BOOL_EQ(dec3d.source == GameSignalSource::WaylandInputMethod, true);

        snap.waylandImTextInputActive = true;
        auto decChat = evaluateGameChatDecision(snap);
        EXPECT_BOOL_EQ(decChat.chatActive, true);
        EXPECT_BOOL_EQ(decChat.source == GameSignalSource::WaylandInputMethod, true);

        // X11 XFixes cursor remains primary for XWayland games even when IM bridge is alive
        snap.hasX11WindowFocus = true;
        snap.x11CursorVisible = false;
        auto decX11Override = evaluateGameChatDecision(snap);
        EXPECT_BOOL_EQ(decX11Override.chatActive, false);
        EXPECT_BOOL_EQ(decX11Override.source == GameSignalSource::XWaylandCursor, true);
    }

    // 5. GameModeMonitor callback lifecycle (unset DISPLAY avoids touching user display during offscreen test)
    const char* oldDisp = ::getenv("DISPLAY");
    std::string savedDisp = oldDisp ? oldDisp : "";
    ::unsetenv("DISPLAY");
    {
        GameModeMonitor mon;
        bool lastChat = false;
        GameSignalSource lastSrc = GameSignalSource::None;
        int cbCount = 0;
        mon.setOnStateChanged([&](bool chatActive, GameSignalSource src) {
            lastChat = chatActive;
            lastSrc = src;
            ++cbCount;
        });
        mon.start();
        mon.setKWinTextInputState(false, false);
        mon.setGameModeActive(true, "minecraft");
        EXPECT_BOOL_EQ(mon.isGameModeActive(), true);
        EXPECT_BOOL_EQ(mon.isChatActive(), false);

        // Transition KWin text input ON -> enters Chat mode
        mon.setKWinTextInputState(true, true);
        EXPECT_BOOL_EQ(mon.isChatActive(), true);
        EXPECT_BOOL_EQ(lastChat, true);
        EXPECT_BOOL_EQ(lastSrc == GameSignalSource::KWinTextInput, true);
        EXPECT_BOOL_EQ(cbCount >= 1, true);

        // Transition KWin text input OFF -> returns to 3D raw pass-through
        mon.setKWinTextInputState(false, true);
        EXPECT_BOOL_EQ(mon.isChatActive(), false);
        EXPECT_BOOL_EQ(lastChat, false);

        // Standalone Wayland IM bridge (vnsnowkey-im) overrides KWin property & clears cleanly on exit
        mon.setWaylandImState(true, true);
        EXPECT_BOOL_EQ(mon.isWaylandImBridgeAlive(), true);
        EXPECT_BOOL_EQ(mon.isChatActive(), true);
        EXPECT_BOOL_EQ(lastSrc == GameSignalSource::WaylandInputMethod, true);

        mon.setWaylandImState(false, true);
        EXPECT_BOOL_EQ(mon.isChatActive(), false);
        EXPECT_BOOL_EQ(lastSrc == GameSignalSource::WaylandInputMethod, true);

        mon.setWaylandImState(false, false);
        EXPECT_BOOL_EQ(mon.isWaylandImBridgeAlive(), false);
        EXPECT_BOOL_EQ(mon.isChatActive(), false);

        // Compositor-native cursor visibility (GNOME Meta.CursorTracker via D-Bus extension)
        {
            GameSignalSnapshot gnomeSnap;
            gnomeSnap.gameModeActive = true;
            gnomeSnap.hasDesktopCursorSignal = true;
            gnomeSnap.desktopCursorVisible = false;
            gnomeSnap.hasX11WindowFocus = true;
            gnomeSnap.x11CursorVisible = true;
            auto dec3dGnome = evaluateGameChatDecision(gnomeSnap);
            EXPECT_BOOL_EQ(dec3dGnome.chatActive, false);
            EXPECT_BOOL_EQ(dec3dGnome.source == GameSignalSource::DesktopCursor, true);

            gnomeSnap.desktopCursorVisible = true;
            auto decChatGnome = evaluateGameChatDecision(gnomeSnap);
            EXPECT_BOOL_EQ(decChatGnome.chatActive, true);
            EXPECT_BOOL_EQ(decChatGnome.source == GameSignalSource::DesktopCursor, true);

            // If XWayland game hides its X11 cursor while DesktopCursor reports true,
            // gameplay raw WASD pass-through must win.
            gnomeSnap.x11CursorVisible = false;
            auto decX11HiddenWins = evaluateGameChatDecision(gnomeSnap);
            EXPECT_BOOL_EQ(decX11HiddenWins.chatActive, false);
            EXPECT_BOOL_EQ(decX11HiddenWins.source == GameSignalSource::XWaylandCursor, true);
        }

        mon.setGameModeActive(false, "");
        mon.setCompositorIsGnome(true);
        mon.setGameModeActive(true, "minecraft");
        // GNOME Meta.CursorTracker.get_pointer_visible() reports true even in 3D games;
        // unconfirmed DesktopCursor(true) MUST NOT trap WASD in chat mode before a hidden
        // cursor (false) is actually observed in the active GameMode session.
        mon.setDesktopCursorState(true, true);
        EXPECT_BOOL_EQ(mon.isChatActive(), false);
        EXPECT_BOOL_EQ(lastSrc != GameSignalSource::DesktopCursor, true);

        // Once a hidden cursor is observed, DesktopCursor tracks 3D (false) <-> Chat/UI (true)
        mon.setDesktopCursorState(false, true);
        EXPECT_BOOL_EQ(mon.isChatActive(), false);
        EXPECT_BOOL_EQ(lastSrc == GameSignalSource::DesktopCursor, true);
        mon.setDesktopCursorState(true, true);
        EXPECT_BOOL_EQ(mon.isChatActive(), true);
        EXPECT_BOOL_EQ(lastSrc == GameSignalSource::DesktopCursor, true);
        // Typing inside GameMode chat protects against transient pointer auto-hide mid-word,
        // while pressing Enter/Esc immediately ends grace so WASD resumes cleanly.
        mon.setDesktopCursorState(true, true);
        EXPECT_BOOL_EQ(mon.isChatActive(), true);
        mon.notifyChatKeystroke(false);
        mon.setDesktopCursorState(false, true);
        EXPECT_BOOL_EQ(mon.isChatActive(), true);
        mon.notifyChatKeystroke(true);
        mon.setDesktopCursorState(false, true);
        EXPECT_BOOL_EQ(mon.isChatActive(), false);
        EXPECT_BOOL_EQ(lastSrc == GameSignalSource::DesktopCursor, true);

        // If DesktopCursor stays stuck at true after Enter/Esc (e.g. QEMU/Mutter cursor sync),
        // notifyChatKeystroke(true) must still release WASD gameplay, and transient <300ms false
        // flickers must not erase cursorExitOverride.
        mon.setDesktopCursorState(true, true);
        EXPECT_BOOL_EQ(mon.isChatActive(), true);
        mon.notifyChatKeystroke(true);
        EXPECT_BOOL_EQ(mon.isChatActive(), false);
        mon.setDesktopCursorState(false, true);
        mon.setDesktopCursorState(true, true);
        EXPECT_BOOL_EQ(mon.isChatActive(), false);
        mon.setDesktopCursorState(false, false);
        mon.setCompositorIsGnome(false);

        // Switching focus to a non-GameMode app disables monitoring cleanly
        mon.setGameModeActive(false, "firefox");
        EXPECT_BOOL_EQ(mon.isGameModeActive(), false);
        EXPECT_BOOL_EQ(mon.isChatActive(), false);
        mon.stop();
    }
    if (!savedDisp.empty()) {
        ::setenv("DISPLAY", savedDisp.c_str(), 1);
    }

    // 6. Verify SettingsWindow minecraft preset populates "game_mode" and persists it
    {
        ::setenv("XDG_CONFIG_HOME", tmpDir.c_str(), 1);
        std::string cfgPath = Config::defaultConfigPath();
        Config::createDefault().saveToFile(cfgPath);
        SettingsWindow win;

        QComboBox* presetCombo = nullptr;
        for (auto* cb : win.findChildren<QComboBox*>()) {
            if (cb->findData(QStringLiteral("minecraft")) >= 0) {
                presetCombo = cb;
                break;
            }
        }
        EXPECT_BOOL_EQ(presetCombo != nullptr, true);
        auto* tbl = win.findChild<QTableWidget*>(QStringLiteral("appProfilesTable"));
        EXPECT_BOOL_EQ(tbl != nullptr, true);

        if (presetCombo && tbl) {
            int beforeRows = tbl->rowCount();
            presetCombo->setCurrentIndex(presetCombo->findData(QStringLiteral("minecraft")));
            QPushButton* addBtn = nullptr;
            for (auto* btn : win.findChildren<QPushButton*>()) {
                if (btn->text() == QStringLiteral("+ Thêm") && btn->parentWidget() == presetCombo->parentWidget()) {
                    addBtn = btn;
                    break;
                }
            }
            QPushButton* applyBtn = win.applyButton();
            EXPECT_BOOL_EQ(addBtn != nullptr, true);
            EXPECT_BOOL_EQ(applyBtn != nullptr, true);
            if (addBtn && applyBtn) {
                addBtn->click();
                EXPECT_BOOL_EQ(tbl->rowCount() == beforeRows + 1, true);
                if (tbl->rowCount() == beforeRows + 1) {
                    EXPECT_BOOL_EQ(tbl->item(beforeRows, 0) != nullptr &&
                                       tbl->item(beforeRows, 0)->text() == QStringLiteral("minecraft"),
                                   true);
                    EXPECT_BOOL_EQ(tbl->item(beforeRows, 1) != nullptr &&
                                       tbl->item(beforeRows, 1)->text() == QStringLiteral("game_mode"),
                                   true);
                }
                applyBtn->click();
                Config reloaded = Config::loadFromFile(cfgPath);
                const AppProfile* mcProf = reloaded.findProfileForApp("Minecraft* 1.21");
                EXPECT_BOOL_EQ(mcProf != nullptr && mcProf->mode == AppMode::GameMode, true);
            }
        }
    }
}

void testMultiDesktopFocusBackendsAndParsers() {
    // 1. Desktop backend auto-detection across KDE, GNOME, Hyprland, Sway, X11
    EXPECT_BOOL_EQ(
        detectDesktopBackend("KDE", "wayland", "", "", "wayland-0", ":0") == DesktopBackend::KWin,
        true);
    EXPECT_BOOL_EQ(
        detectDesktopBackend("ubuntu:GNOME", "wayland", "", "", "wayland-0", ":0") == DesktopBackend::GnomeShell,
        true);
    EXPECT_BOOL_EQ(
        detectDesktopBackend("GNOME", "x11", "", "", "", ":0") == DesktopBackend::X11,
        true);
    EXPECT_BOOL_EQ(
        detectDesktopBackend("Hyprland", "wayland", "abc123", "", "wayland-1", ":0") == DesktopBackend::Hyprland,
        true);
    EXPECT_BOOL_EQ(
        detectDesktopBackend("sway", "wayland", "", "/run/user/1000/sway-ipc.sock", "wayland-1", "") == DesktopBackend::Sway,
        true);
    EXPECT_BOOL_EQ(
        detectDesktopBackend("XFCE", "x11", "", "", "", ":0") == DesktopBackend::X11,
        true);
    EXPECT_BOOL_EQ(
        detectDesktopBackend("", "", "", "", "", "") == DesktopBackend::None,
        true);

    // 2. Hyprland socket2 line parser (preserving commas inside window caption)
    {
        std::string cls, cap;
        EXPECT_BOOL_EQ(
            parseHyprlandActiveWindowLine("activewindow>>firefox,Docs, Part 2 — Mozilla Firefox", cls, cap),
            true);
        EXPECT_BOOL_EQ(cls == "firefox", true);
        EXPECT_BOOL_EQ(cap == "Docs, Part 2 — Mozilla Firefox", true);
        EXPECT_BOOL_EQ(parseHyprlandActiveWindowLine("workspace>>2", cls, cap), false);
    }

    // 3. Hyprland activewindow JSON parser
    {
        std::string cls, cap, wid;
        EXPECT_BOOL_EQ(
            parseHyprlandActiveWindowJson(
                R"({"address":"0x1234","class":"minecraft","title":"Minecraft* 26.3 - Chơi đơn"})",
                cls, cap, wid),
            true);
        EXPECT_BOOL_EQ(cls == "minecraft", true);
        EXPECT_BOOL_EQ(wid == "0x1234", true);
    }

    // 4. Sway i3-IPC window focus event parser (Wayland app_id + XWayland window_properties.class)
    {
        std::string cls, name, cap, wid;
        EXPECT_BOOL_EQ(
            parseSwayWindowEventJson(
                R"({"change":"focus","container":{"id":42,"app_id":"org.gnome.Nautilus","name":"Files"}})",
                cls, name, cap, wid),
            true);
        EXPECT_BOOL_EQ(cls == "org.gnome.Nautilus", true);
        EXPECT_BOOL_EQ(cap == "Files", true);
        EXPECT_BOOL_EQ(wid == "42", true);

        EXPECT_BOOL_EQ(
            parseSwayWindowEventJson(
                R"({"change":"focus","container":{"id":77,"app_id":null,"window_properties":{"class":"Code","instance":"code"},"name":"VS Code"}})",
                cls, name, cap, wid),
            true);
        EXPECT_BOOL_EQ(cls == "Code", true);
        EXPECT_BOOL_EQ(name == "code", true);

        EXPECT_BOOL_EQ(
            parseSwayWindowEventJson(R"({"change":"close","container":{"id":77}})", cls, name, cap, wid),
            false);
    }

    // 5. Fedora/GNOME on-demand XWayland XAUTHORITY & socket discovery helper
    {
        const std::string xwTmp = "/tmp/vnsnowkey_xw_test_" + std::to_string(::getpid());
        const std::string rtDir = xwTmp + "/run";
        const std::string homeDir = xwTmp + "/home";
        const std::string x11Dir = xwTmp + "/x11";
        std::filesystem::create_directories(rtDir);
        std::filesystem::create_directories(homeDir);
        std::filesystem::create_directories(x11Dir);

        {
            std::ofstream f(homeDir + "/.Xauthority");
            f << "home-auth";
        }
        EXPECT_STR_EQ(xwayland_env::findBestXAuthorityFile(rtDir, homeDir), homeDir + "/.Xauthority");

        {
            std::ofstream f1(rtDir + "/.mutter-Xwaylandauth.OLD111");
            f1 << "old";
        }
        std::filesystem::last_write_time(
            rtDir + "/.mutter-Xwaylandauth.OLD111",
            std::filesystem::file_time_type::clock::now() - std::chrono::seconds(10));
        {
            std::ofstream f2(rtDir + "/.mutter-Xwaylandauth.NEW222");
            f2 << "new";
        }
        EXPECT_STR_EQ(xwayland_env::findBestXAuthorityFile(rtDir, homeDir), rtDir + "/.mutter-Xwaylandauth.NEW222");

        {
            std::ofstream xs(x11Dir + "/X0");
            xs << "";
        }
        EXPECT_STR_EQ(xwayland_env::discoverX11SocketDisplay(x11Dir), ":0");

        const char* prevQpa = std::getenv("QT_QPA_PLATFORM");
        std::string savedQpa = prevQpa ? std::string(prevQpa) : std::string();
        if (std::getenv("VNSNOWKEY_ONLY_DEVICES") == nullptr) {
            ::setenv("QT_QPA_PLATFORM", "wayland", 1);
            EXPECT_BOOL_EQ(xwayland_env::isIsolatedTestEnv(), false);
            ::setenv("QT_QPA_PLATFORM", "offscreen", 1);
            EXPECT_BOOL_EQ(xwayland_env::isIsolatedTestEnv(), true);
            if (!savedQpa.empty()) {
                ::setenv("QT_QPA_PLATFORM", savedQpa.c_str(), 1);
            } else {
                ::unsetenv("QT_QPA_PLATFORM");
            }
        }

        std::filesystem::remove_all(xwTmp);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    const std::string tmpDir = "/tmp/vnsnowkey_app_test_" + std::to_string(::getpid());
    std::filesystem::create_directories(tmpDir);

    testBrowserUrlBarAutocompleteFix();
    testQtWidgetsMultiLineEditorAndSettingsWindow(tmpDir);
    testKWinFocusTrackerAndPerAppLanguageMemory();
    testGameModeSystemSignalFusionAndMonitor(tmpDir);
    testMultiDesktopFocusBackendsAndParsers();

    std::filesystem::remove_all(tmpDir);
    std::cout << "Multi-App Widget, URL Bar & Focus Tracker Tests Summary: "
              << g_passed << " passed, " << g_failed << " failed.\n";
    return g_failed == 0 ? 0 : 1;
}
