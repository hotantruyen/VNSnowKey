/*
 * VNSnowKey - Vietnamese Input Method for Arch Linux + KDE Plasma (Wayland)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "../config/config.hpp"
#include "../engine/session_processor.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDBusInterface>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>

namespace vnsnowkey {

class HotkeyCaptureWidget : public QWidget {
    Q_OBJECT

public:
    explicit HotkeyCaptureWidget(const QString& placeholder = QString(), QWidget* parent = nullptr);
    ~HotkeyCaptureWidget() override;

    void setHotkey(const QString& hotkey);
    QString hotkey() const { return hotkey_; }

Q_SIGNALS:
    void hotkeyChanged(const QString& hotkey);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private Q_SLOTS:
    void onCaptureClicked();
    void onClearClicked();

private:
    void startRecording();
    void stopRecording();
    void updateDisplay();

    QString placeholder_;
    QString hotkey_;
    bool recording_ = false;
    uint8_t heldModMask_ = 0;
    uint8_t peakMods_ = 0;

    QPushButton* captureBtn_ = nullptr;
    QToolButton* clearBtn_ = nullptr;
};

class SettingsWindow : public QMainWindow {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.vnsnowkey.Gui")

public:
    explicit SettingsWindow(QWidget* parent = nullptr);
    ~SettingsWindow() override = default;

    void showAndRaise();
    void updateDaemonLiveStatus(const QString& mode, const QString& inputMethod, bool safeMode);

    bool isDirty() const { return isDirty_; }
    QPushButton* applyButton() const { return applyBtn_; }

public Q_SLOTS:
    Q_SCRIPTABLE void ActivateSettings();
    Q_SCRIPTABLE bool IsSettingsVisible() const;

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private Q_SLOTS:
    void onUiValueChanged();
    void onApplyAndHotReload();
    void onResetDefaults();
    void onAddAppProfileRow();
    void onRemoveAppProfileRow();
    void onAddMacroRow();
    void onRemoveMacroRow();
    void onImportMacros();
    void onExportMacros();
    void onConvertTextSample();
    void onConvertClipboardNow();
    void onBackupConfig();
    void onRestoreConfig();
    void onRefreshDiagnostics();
    void onToggleDaemonModeClicked();
    void onPanicOrResumeClicked();

private:
    void setupUi();
    QWidget* createGeneralAndTypingPage();
    QWidget* createShortcutsPage();
    QWidget* createAppsPage();
    QWidget* createMacrosPage();
    QWidget* createAdvancedPage();
    QWidget* createDiagnosticsPage();

    void loadConfigIntoUi();
    void applyConfigToUi(const Config& cfg);
    Config collectDraftFromUi() const;
    void updateDirtyState();
    void updateShortcutHint();
    void refreshThemeIcons();
    void syncSystemdAutostart(bool enabled, bool runSystemctl);

    Config config_;
    Config baselineConfig_;
    std::string configPath_;
    bool loadingUi_ = false;
    bool isDirty_ = false;
    bool hasAppliedOnce_ = false;
    bool lastAutostartState_ = true;
    QDBusInterface* dbus_ = nullptr;

    QListWidget* navList_ = nullptr;
    QStackedWidget* pagesStack_ = nullptr;
    QLabel* headerBadgeLabel_ = nullptr;
    bool headerBadgeIsVi_ = true;
    QLabel* headerStatusLabel_ = nullptr;
    QLabel* dirtyStatusLabel_ = nullptr;
    QPushButton* headerToggleBtn_ = nullptr;
    QPushButton* headerPanicBtn_ = nullptr;
    QPushButton* applyBtn_ = nullptr;
    QPushButton* resetBtn_ = nullptr;

    // Page 1: Chung & Kiểu gõ
    QComboBox* defaultModeCombo_ = nullptr;
    QCheckBox* osdNotificationCheck_ = nullptr;
    QCheckBox* autostartCheck_ = nullptr;
    QComboBox* inputMethodCombo_ = nullptr;
    QComboBox* charsetCombo_ = nullptr;
    QCheckBox* modernToneCheck_ = nullptr;
    QCheckBox* freeToneCheck_ = nullptr;
    QCheckBox* spellCheck_ = nullptr;
    QCheckBox* autoRestoreCheck_ = nullptr;
    QCheckBox* shorthandInitCheck_ = nullptr;
    QCheckBox* shorthandFinalCheck_ = nullptr;
    QLineEdit* liveTestRawEdit_ = nullptr;
    QLineEdit* liveTestOutputEdit_ = nullptr;

    // Page 2: Phím tắt
    HotkeyCaptureWidget* scTogglePrimary_ = nullptr;
    HotkeyCaptureWidget* scToggleSecondary_ = nullptr;
    HotkeyCaptureWidget* scToggleExtra_ = nullptr;
    HotkeyCaptureWidget* scRestoreWord_ = nullptr;
    HotkeyCaptureWidget* scPanicSafeMode_ = nullptr;
    QLabel* shortcutHintLabel_ = nullptr;

    // Page 3: Ứng dụng
    QCheckBox* rememberPerAppCheck_ = nullptr;
    QCheckBox* disableInPasswordCheck_ = nullptr;
    QTableWidget* appProfilesTable_ = nullptr;
    QComboBox* presetAppCombo_ = nullptr;

    // Page 4: Macro & Chuyển mã
    QCheckBox* macroEnabledCheck_ = nullptr;
    QCheckBox* macroAutoCapCheck_ = nullptr;
    QTableWidget* macrosTable_ = nullptr;
    QComboBox* convFromCharsetCombo_ = nullptr;
    QComboBox* convToCharsetCombo_ = nullptr;
    QPlainTextEdit* convSourceEdit_ = nullptr;
    QPlainTextEdit* convResultEdit_ = nullptr;

    // Page 5: Nâng cao
    QComboBox* outputMethodCombo_ = nullptr;
    QSpinBox* defaultDelaySpin_ = nullptr;
    QSpinBox* clipboardRestoreSpin_ = nullptr;
    QCheckBox* resetMouseClickCheck_ = nullptr;
    QCheckBox* resetFocusChangeCheck_ = nullptr;
    QCheckBox* resetNavKeysCheck_ = nullptr;
    QSpinBox* bufferTimeoutSpin_ = nullptr;
    QCheckBox* typingStatsCheck_ = nullptr;
    QCheckBox* debugRedactedCheck_ = nullptr;
    QLabel* statsSummaryLabel_ = nullptr;

    // Page 6: Chẩn đoán
    QPlainTextEdit* diagnosticsOutputEdit_ = nullptr;
};

} // namespace vnsnowkey
