/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "settings_window.hpp"
#include "tray_controller.hpp"
#include "../engine/bamboo_utils.hpp"
#include "../engine/charset_encoder.hpp"
#include "../engine/macro_table.hpp"
#include "../engine/rules_parser.hpp"
#include "../output/xkb_overlay.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDBusConnection>
#include <QDBusReply>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPainter>
#include <QProcess>
#include <QSet>
#include <QStyledItemDelegate>
#include <QSvgRenderer>
#include <QTextStream>
#include <QVBoxLayout>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <unistd.h>

namespace vnsnowkey {

namespace {

constexpr const char* kSidebarIconSvgs[6] = {
    // 0: General & Typing (Keyboard)
    R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="CURRENT" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><rect x="2.5" y="5.5" width="19" height="13" rx="2.5"/><path d="M6.5 9.5h1M10 9.5h1M13.5 9.5h1M17 9.5h.5M7 14.5h10"/></svg>)SVG",
    // 1: Shortcuts (Command symbol)
    R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="CURRENT" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><path d="M18 3a3 3 0 0 0-3 3v12a3 3 0 0 0 3 3 3 3 0 0 0 3-3 3 3 0 0 0-3-3H6a3 3 0 0 0-3 3 3 3 0 0 0 3 3 3 3 0 0 0 3-3V6a3 3 0 0 0-3-3 3 3 0 0 0-3 3 3 3 0 0 0 3 3h12a3 3 0 0 0 3-3 3 3 0 0 0-3-3z"/></svg>)SVG",
    // 2: Applications (2x2 App Grid)
    R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="CURRENT" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><rect x="3.5" y="3.5" width="7" height="7" rx="1.6"/><rect x="13.5" y="3.5" width="7" height="7" rx="1.6"/><rect x="3.5" y="13.5" width="7" height="7" rx="1.6"/><rect x="13.5" y="13.5" width="7" height="7" rx="1.6"/></svg>)SVG",
    // 3: Macros & Charset (Text lines + quick-expand bolt)
    R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="CURRENT" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><path d="M3.5 7h8.5M3.5 12h6M3.5 17h8M16.5 6.5L13 12h4.2l-2.7 5.5"/></svg>)SVG",
    // 4: Advanced (Tuning sliders)
    R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="CURRENT" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><path d="M4 6.5h16M4 12h16M4 17.5h16"/><circle cx="8.5" cy="6.5" r="2" fill="CURRENT"/><circle cx="15.5" cy="12" r="2" fill="CURRENT"/><circle cx="10.5" cy="17.5" r="2" fill="CURRENT"/></svg>)SVG",
    // 5: Diagnostics (System Doctor shield + pulse)
    R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="CURRENT" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2.8L4.5 5.8v5.5c0 4.6 3.2 8.6 7.5 9.9 4.3-1.3 7.5-5.3 7.5-9.9V5.8L12 2.8z"/><path d="M7.5 12h2l1.4-2.8 1.9 5.4 1.4-2.6h2.3"/></svg>)SVG"
};

QPixmap renderSvgTinted(const char* svgTmpl, const QColor& color, int size) {
    QByteArray svgBytes(svgTmpl);
    svgBytes.replace("CURRENT", color.name(QColor::HexRgb).toUtf8());
    QSvgRenderer renderer(svgBytes);
    QPixmap px(size, size);
    px.fill(Qt::transparent);
    if (renderer.isValid()) {
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&p, QRectF(0, 0, size, size));
        p.end();
    }
    return px;
}

QIcon buildThemedSidebarIcon(int index, const QColor& normalColor, const QColor& selectedColor) {
    QIcon icon;
    if (index < 0 || index >= 6) return icon;
    for (int sz : {18, 24, 32}) {
        const QPixmap norm = renderSvgTinted(kSidebarIconSvgs[index], normalColor, sz);
        const QPixmap sel = renderSvgTinted(kSidebarIconSvgs[index], selectedColor, sz);
        icon.addPixmap(norm, QIcon::Normal, QIcon::Off);
        icon.addPixmap(norm, QIcon::Active, QIcon::Off);
        icon.addPixmap(sel, QIcon::Selected, QIcon::Off);
        icon.addPixmap(sel, QIcon::Selected, QIcon::On);
    }
    return icon;
}

class AppModeDelegate : public QStyledItemDelegate {
public:
    explicit AppModeDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override {
        auto* combo = new QComboBox(parent);
        combo->addItem(QStringLiteral("default"));
        combo->addItem(QStringLiteral("always_vi"));
        combo->addItem(QStringLiteral("always_en"));
        combo->addItem(QStringLiteral("excluded"));
        combo->addItem(QStringLiteral("game_mode"));
        return combo;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        if (auto* combo = qobject_cast<QComboBox*>(editor)) {
            int idx = combo->findText(index.data(Qt::EditRole).toString().trimmed());
            combo->setCurrentIndex(idx >= 0 ? idx : 0);
        }
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override {
        if (auto* combo = qobject_cast<QComboBox*>(editor)) {
            model->setData(index, combo->currentText(), Qt::EditRole);
        }
    }
};

QFrame* makeCard(QWidget* parent, const QString& title = QString(), QLayout* innerLayout = nullptr) {
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("settingsCard"));
    auto* outer = new QVBoxLayout(frame);
    outer->setContentsMargins(12, 10, 12, 10);
    outer->setSpacing(8);
    if (!title.isEmpty()) {
        auto* lbl = new QLabel(title, frame);
        lbl->setObjectName(QStringLiteral("cardTitle"));
        outer->addWidget(lbl);
    }
    if (innerLayout) {
        outer->addLayout(innerLayout);
    }
    return frame;
}

uint8_t qtKeyToModBit(int qtKey) {
    switch (qtKey) {
        case Qt::Key_Control: return kModCtrl;
        case Qt::Key_Alt:
        case Qt::Key_AltGr:   return kModAlt;
        case Qt::Key_Shift:   return kModShift;
        case Qt::Key_Meta:
        case Qt::Key_Super_L:
        case Qt::Key_Super_R: return kModSuper;
        default: return 0;
    }
}

uint8_t qtModsToMask(Qt::KeyboardModifiers m) {
    uint8_t mask = 0;
    if (m & Qt::ControlModifier) mask |= kModCtrl;
    if (m & Qt::AltModifier)     mask |= kModAlt;
    if (m & Qt::ShiftModifier)   mask |= kModShift;
    if (m & Qt::MetaModifier)    mask |= kModSuper;
    return mask;
}

uint16_t qtKeyToEvdevCode(int qtKey) {
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        return Config::hotkeyNameToEvdevCode(std::string(1, static_cast<char>('A' + (qtKey - Qt::Key_A))));
    }
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
        return Config::hotkeyNameToEvdevCode(std::string(1, static_cast<char>('0' + (qtKey - Qt::Key_0))));
    }
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12) {
        return Config::hotkeyNameToEvdevCode("F" + std::to_string(1 + (qtKey - Qt::Key_F1)));
    }
    switch (qtKey) {
        case Qt::Key_Space:        return KEY_SPACE;
        case Qt::Key_Tab:
        case Qt::Key_Backtab:      return KEY_TAB;
        case Qt::Key_Return:
        case Qt::Key_Enter:        return KEY_ENTER;
        case Qt::Key_Escape:       return KEY_ESC;
        case Qt::Key_Backspace:    return KEY_BACKSPACE;
        case Qt::Key_Delete:       return KEY_DELETE;
        case Qt::Key_Insert:       return KEY_INSERT;
        case Qt::Key_Home:         return KEY_HOME;
        case Qt::Key_End:          return KEY_END;
        case Qt::Key_PageUp:       return KEY_PAGEUP;
        case Qt::Key_PageDown:     return KEY_PAGEDOWN;
        case Qt::Key_Left:         return KEY_LEFT;
        case Qt::Key_Right:        return KEY_RIGHT;
        case Qt::Key_Up:           return KEY_UP;
        case Qt::Key_Down:         return KEY_DOWN;
        case Qt::Key_Pause:        return KEY_PAUSE;
        case Qt::Key_Print:
        case Qt::Key_SysReq:       return KEY_PRINT;
        case Qt::Key_ScrollLock:   return KEY_SCROLLLOCK;
        case Qt::Key_Minus:        return KEY_MINUS;
        case Qt::Key_Equal:        return KEY_EQUAL;
        case Qt::Key_BracketLeft:  return KEY_LEFTBRACE;
        case Qt::Key_BracketRight: return KEY_RIGHTBRACE;
        case Qt::Key_Backslash:    return KEY_BACKSLASH;
        case Qt::Key_Semicolon:    return KEY_SEMICOLON;
        case Qt::Key_Apostrophe:   return KEY_APOSTROPHE;
        case Qt::Key_QuoteLeft:    return KEY_GRAVE;
        case Qt::Key_Comma:        return KEY_COMMA;
        case Qt::Key_Period:       return KEY_DOT;
        case Qt::Key_Slash:        return KEY_SLASH;
        default:                   return 0;
    }
}

bool isStandaloneSpecialKey(int qtKey) {
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12) return true;
    switch (qtKey) {
        case Qt::Key_Escape:
        case Qt::Key_Pause:
        case Qt::Key_Print:
        case Qt::Key_SysReq:
        case Qt::Key_ScrollLock:
        case Qt::Key_Insert:
            return true;
        default:
            return false;
    }
}

} // namespace

// --- HotkeyCaptureWidget ---

HotkeyCaptureWidget::HotkeyCaptureWidget(const QString& placeholder, QWidget* parent)
    : QWidget(parent), placeholder_(placeholder) {
    setFocusPolicy(Qt::StrongFocus);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    captureBtn_ = new QPushButton(this);
    captureBtn_->setObjectName(QStringLiteral("hotkeyCaptureBtn"));
    captureBtn_->setMinimumWidth(190);
    captureBtn_->setCursor(Qt::PointingHandCursor);
    connect(captureBtn_, &QPushButton::clicked, this, &HotkeyCaptureWidget::onCaptureClicked);

    clearBtn_ = new QToolButton(this);
    clearBtn_->setText(QStringLiteral("✕"));
    clearBtn_->setToolTip(QStringLiteral("Bỏ gán phím tắt này"));
    clearBtn_->setFixedSize(26, 26);
    connect(clearBtn_, &QToolButton::clicked, this, &HotkeyCaptureWidget::onClearClicked);

    layout->addWidget(captureBtn_, 1);
    layout->addWidget(clearBtn_);
    updateDisplay();
}

HotkeyCaptureWidget::~HotkeyCaptureWidget() {
    stopRecording();
}

void HotkeyCaptureWidget::setHotkey(const QString& hotkey) {
    const QString canonical = QString::fromStdString(Config::canonicalizeHotkey(hotkey.toStdString()));
    hotkey_ = canonical;
    stopRecording();
    updateDisplay();
}

void HotkeyCaptureWidget::onCaptureClicked() {
    if (recording_) {
        stopRecording();
    } else {
        startRecording();
    }
}

void HotkeyCaptureWidget::onClearClicked() {
    stopRecording();
    if (!hotkey_.isEmpty()) {
        hotkey_.clear();
        updateDisplay();
        Q_EMIT hotkeyChanged(hotkey_);
    }
}

void HotkeyCaptureWidget::startRecording() {
    recording_ = true;
    heldModMask_ = 0;
    peakMods_ = 0;
    setFocus(Qt::OtherFocusReason);
    grabKeyboard();
    updateDisplay();
}

void HotkeyCaptureWidget::stopRecording() {
    if (!recording_) return;
    recording_ = false;
    heldModMask_ = 0;
    peakMods_ = 0;
    releaseKeyboard();
    updateDisplay();
}

void HotkeyCaptureWidget::updateDisplay() {
    if (recording_) {
        if (peakMods_ != 0) {
            const QString partial = QString::fromStdString(Config::formatHotkey(peakMods_, 0));
            captureBtn_->setText(QStringLiteral("%1 + ... (thả để chọn 2 modifier)").arg(partial));
        } else {
            captureBtn_->setText(QStringLiteral("Nhấn tổ hợp phím..."));
        }
        captureBtn_->setProperty("recording", true);
    } else if (hotkey_.isEmpty()) {
        captureBtn_->setText(placeholder_.isEmpty() ? QStringLiteral("Chưa gán (nhấn để đặt)") : placeholder_);
        captureBtn_->setProperty("recording", false);
    } else {
        captureBtn_->setText(hotkey_);
        captureBtn_->setProperty("recording", false);
    }
    clearBtn_->setEnabled(!hotkey_.isEmpty() || recording_);
}

void HotkeyCaptureWidget::focusOutEvent(QFocusEvent* event) {
    stopRecording();
    QWidget::focusOutEvent(event);
}

void HotkeyCaptureWidget::keyPressEvent(QKeyEvent* event) {
    if (!recording_) {
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
    if (event->isAutoRepeat()) return;

    const uint8_t modBit = qtKeyToModBit(event->key());
    const uint8_t mods = static_cast<uint8_t>(heldModMask_ | qtModsToMask(event->modifiers()) | modBit);
    if (modBit != 0) {
        heldModMask_ = mods;
        peakMods_ |= mods;
        updateDisplay();
        return;
    }

    const uint16_t evCode = qtKeyToEvdevCode(event->key());
    if (evCode == 0) {
        return;
    }
    if (mods == 0 && !isStandaloneSpecialKey(event->key())) {
        // Ignore bare printable keys so users cannot break normal typing
        return;
    }

    const QString formatted = QString::fromStdString(Config::formatHotkey(mods, evCode));
    if (!formatted.isEmpty()) {
        const bool changed = (hotkey_ != formatted);
        hotkey_ = formatted;
        stopRecording();
        if (changed) {
            Q_EMIT hotkeyChanged(hotkey_);
        }
    }
}

void HotkeyCaptureWidget::keyReleaseEvent(QKeyEvent* event) {
    if (!recording_) {
        QWidget::keyReleaseEvent(event);
        return;
    }
    event->accept();

    const uint8_t modBit = qtKeyToModBit(event->key());
    if (modBit != 0) {
        heldModMask_ = static_cast<uint8_t>(heldModMask_ & ~modBit);
        const bool hasAtLeastTwoMods = (peakMods_ != 0) && ((peakMods_ & (peakMods_ - 1)) != 0);
        if (heldModMask_ == 0 && hasAtLeastTwoMods) {
            const QString formatted = QString::fromStdString(Config::formatHotkey(peakMods_, 0));
            const bool changed = (!formatted.isEmpty() && hotkey_ != formatted);
            if (!formatted.isEmpty()) {
                hotkey_ = formatted;
            }
            stopRecording();
            if (changed) {
                Q_EMIT hotkeyChanged(hotkey_);
            }
        }
    }
}

// --- SettingsWindow ---

SettingsWindow::SettingsWindow(QWidget* parent)
    : QMainWindow(parent),
      config_(Config::createDefault()),
      baselineConfig_(Config::createDefault()),
      configPath_(Config::defaultConfigPath()) {
    setWindowTitle(QStringLiteral("VNSnowKey - Bộ gõ Tiếng Việt"));
    resize(780, 540);
    setMinimumSize(680, 460);
    setWindowIcon(TrayController::buildAdaptiveMonochromeIcon(true));

    dbus_ = new QDBusInterface(
        QStringLiteral("org.vnsnowkey.Daemon"),
        QStringLiteral("/org/vnsnowkey/Daemon"),
        QStringLiteral("org.vnsnowkey.Daemon"),
        QDBusConnection::sessionBus(),
        this);

    setupUi();
    loadConfigIntoUi();
    onRefreshDiagnostics();
}

void SettingsWindow::showAndRaise() {
    loadConfigIntoUi();
    onRefreshDiagnostics();
    show();
    raise();
    activateWindow();
}

void SettingsWindow::closeEvent(QCloseEvent* event) {
    hide();
    event->ignore();
}

void SettingsWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event && (event->type() == QEvent::PaletteChange ||
                  event->type() == QEvent::ApplicationPaletteChange ||
                  event->type() == QEvent::StyleChange)) {
        refreshThemeIcons();
    }
}

void SettingsWindow::refreshThemeIcons() {
    const QColor normalFg = palette().color(QPalette::WindowText);
    const QColor selectedFg = palette().color(QPalette::HighlightedText);
    if (navList_) {
        for (int i = 0; i < navList_->count() && i < 6; ++i) {
            if (QListWidgetItem* item = navList_->item(i)) {
                item->setIcon(buildThemedSidebarIcon(i, normalFg, selectedFg));
            }
        }
    }
    if (headerBadgeLabel_) {
        const bool isDarkWindow = (palette().color(QPalette::Window).lightness() < 128);
        headerBadgeLabel_->setPixmap(
            TrayController::buildAdaptiveMonochromeIcon(headerBadgeIsVi_, isDarkWindow, false)
                .pixmap(20, 20));
    }
}

void SettingsWindow::updateDaemonLiveStatus(const QString& mode, const QString& inputMethod, bool safeMode) {
    if (!headerStatusLabel_) return;
    headerBadgeIsVi_ = (!safeMode && mode == QStringLiteral("vi"));
    refreshThemeIcons();
    if (safeMode) {
        headerStatusLabel_->setText(
            QStringLiteral("<b>Safe Mode (EN)</b> · %1").arg(inputMethod));
        if (headerPanicBtn_) headerPanicBtn_->setText(QStringLiteral("Tiếp tục"));
    } else if (mode == QStringLiteral("vi")) {
        headerStatusLabel_->setText(
            QStringLiteral("<b>Tiếng Việt (VI)</b> · %1").arg(inputMethod));
        if (headerPanicBtn_) headerPanicBtn_->setText(QStringLiteral("Safe Mode"));
    } else {
        headerStatusLabel_->setText(
            QStringLiteral("<b>Tiếng Anh (EN)</b> · %1").arg(inputMethod));
        if (headerPanicBtn_) headerPanicBtn_->setText(QStringLiteral("Safe Mode"));
    }
}

void SettingsWindow::setupUi() {
    // Palette-aware KDE Breeze styling that automatically adapts to Light & Dark themes
    setStyleSheet(QStringLiteral(
        "QFrame#settingsCard {"
        "  background: palette(base);"
        "  border: 1px solid palette(mid);"
        "  border-radius: 8px;"
        "}"
        "QFrame#headerBar {"
        "  background: palette(alternate-base);"
        "  border: 1px solid palette(mid);"
        "  border-radius: 8px;"
        "}"
        "QLabel#cardTitle {"
        "  font-weight: 600;"
        "  color: palette(window-text);"
        "}"
        "QListWidget#navSidebar {"
        "  background: palette(base);"
        "  border: 1px solid palette(mid);"
        "  border-radius: 8px;"
        "  padding: 4px;"
        "  outline: 0;"
        "}"
        "QListWidget#navSidebar::item {"
        "  padding: 7px 10px;"
        "  border-radius: 6px;"
        "  margin: 1px 0px;"
        "}"
        "QListWidget#navSidebar::item:selected {"
        "  background: palette(highlight);"
        "  color: palette(highlighted-text);"
        "  font-weight: 600;"
        "}"
        "QPushButton#applySettingsButton {"
        "  background: palette(highlight);"
        "  color: palette(highlighted-text);"
        "  font-weight: 600;"
        "  padding: 6px 16px;"
        "  border-radius: 6px;"
        "  border: 1px solid palette(highlight);"
        "}"
        "QPushButton#applySettingsButton:disabled {"
        "  background: palette(button);"
        "  color: palette(mid);"
        "  border: 1px solid palette(mid);"
        "  font-weight: 400;"
        "}"
    ));

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    // Compact live status header bar
    auto* topBar = new QFrame(central);
    topBar->setObjectName(QStringLiteral("headerBar"));
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(10, 6, 10, 6);
    topLayout->setSpacing(8);

    headerBadgeLabel_ = new QLabel(topBar);
    headerStatusLabel_ = new QLabel(QStringLiteral("Đang kiểm tra daemon..."), topBar);
    headerToggleBtn_ = new QPushButton(QStringLiteral("Đổi VI / EN"), topBar);
    headerPanicBtn_ = new QPushButton(QStringLiteral("Safe Mode"), topBar);
    connect(headerToggleBtn_, &QPushButton::clicked, this, &SettingsWindow::onToggleDaemonModeClicked);
    connect(headerPanicBtn_, &QPushButton::clicked, this, &SettingsWindow::onPanicOrResumeClicked);

    topLayout->addWidget(headerBadgeLabel_);
    topLayout->addWidget(headerStatusLabel_, 1);
    topLayout->addWidget(headerToggleBtn_);
    topLayout->addWidget(headerPanicBtn_);
    rootLayout->addWidget(topBar);

    // Main split: left sidebar + stacked settings pages
    auto* bodyLayout = new QHBoxLayout();
    bodyLayout->setSpacing(10);

    navList_ = new QListWidget(central);
    navList_->setObjectName(QStringLiteral("navSidebar"));
    navList_->setFixedWidth(192);
    navList_->setIconSize(QSize(18, 18));

    pagesStack_ = new QStackedWidget(central);

    struct PageSpec {
        const char* label;
        QWidget* widget;
    };
    const PageSpec pages[] = {
        {"Chung & Kiểu gõ", createGeneralAndTypingPage()},
        {"Phím tắt",        createShortcutsPage()},
        {"Ứng dụng",        createAppsPage()},
        {"Macro & Mã chữ",  createMacrosPage()},
        {"Nâng cao",        createAdvancedPage()},
        {"Chẩn đoán",       createDiagnosticsPage()},
    };

    for (const auto& p : pages) {
        auto* item = new QListWidgetItem(QString::fromUtf8(p.label), navList_);
        item->setSizeHint(QSize(176, 34));
        pagesStack_->addWidget(p.widget);
    }
    refreshThemeIcons();
    connect(navList_, &QListWidget::currentRowChanged, pagesStack_, &QStackedWidget::setCurrentIndex);
    navList_->setCurrentRow(0);

    bodyLayout->addWidget(navList_);
    bodyLayout->addWidget(pagesStack_, 1);
    rootLayout->addLayout(bodyLayout, 1);

    // Bottom action bar
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(8);

    resetBtn_ = new QPushButton(QStringLiteral("Khôi phục mặc định"), central);
    dirtyStatusLabel_ = new QLabel(central);
    applyBtn_ = new QPushButton(QStringLiteral("Lưu && Áp dụng"), central);
    applyBtn_->setObjectName(QStringLiteral("applySettingsButton"));
    applyBtn_->setEnabled(false);

    connect(resetBtn_, &QPushButton::clicked, this, &SettingsWindow::onResetDefaults);
    connect(applyBtn_, &QPushButton::clicked, this, &SettingsWindow::onApplyAndHotReload);

    bottomLayout->addWidget(resetBtn_);
    bottomLayout->addWidget(dirtyStatusLabel_, 1);
    bottomLayout->addWidget(applyBtn_);
    rootLayout->addLayout(bottomLayout);
}

QWidget* SettingsWindow::createGeneralAndTypingPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // Card 1: General
    auto* genForm = new QFormLayout();
    genForm->setHorizontalSpacing(14);
    genForm->setVerticalSpacing(6);

    defaultModeCombo_ = new QComboBox(page);
    defaultModeCombo_->addItem(QStringLiteral("Tiếng Việt (VI)"), QStringLiteral("vi"));
    defaultModeCombo_->addItem(QStringLiteral("Tiếng Anh (EN)"), QStringLiteral("en"));
    genForm->addRow(QStringLiteral("Chế độ khởi đầu:"), defaultModeCombo_);

    auto* genChecksRow = new QHBoxLayout();
    autostartCheck_ = new QCheckBox(QStringLiteral("Tự khởi động cùng hệ thống"), page);
    osdNotificationCheck_ = new QCheckBox(QStringLiteral("Thông báo OSD khi đổi EN/VI"), page);
    genChecksRow->addWidget(autostartCheck_);
    genChecksRow->addWidget(osdNotificationCheck_);
    genChecksRow->addStretch();
    genForm->addRow(QString(), genChecksRow);

    layout->addWidget(makeCard(page, QStringLiteral("Hoạt động chung"), genForm));

    // Card 2: Vietnamese Typing Engine
    auto* imBox = new QVBoxLayout();
    imBox->setSpacing(8);

    auto* imTopRow = new QFormLayout();
    imTopRow->setHorizontalSpacing(14);
    imTopRow->setVerticalSpacing(6);

    auto* methodCharsetRow = new QHBoxLayout();
    inputMethodCombo_ = new QComboBox(page);
    const std::vector<std::pair<QString, QString>> imItems = {
        {QStringLiteral("Telex (mặc định)"), QStringLiteral("Telex")},
        {QStringLiteral("Simple Telex"), QStringLiteral("Simple Telex")},
        {QStringLiteral("VNI"), QStringLiteral("VNI")},
        {QStringLiteral("VIQR"), QStringLiteral("VIQR")},
        {QStringLiteral("Telex 2"), QStringLiteral("Telex 2")},
        {QStringLiteral("Telex W"), QStringLiteral("Telex W")},
        {QStringLiteral("Telex + VNI"), QStringLiteral("Telex + VNI")},
        {QStringLiteral("Telex + VNI + VIQR"), QStringLiteral("Telex + VNI + VIQR")},
        {QStringLiteral("Microsoft layout"), QStringLiteral("Microsoft layout")},
        {QStringLiteral("VNI Bàn phím tiếng Pháp"), QStringLiteral("VNI Bàn phím tiếng Pháp")},
    };
    const auto& imDefs = bamboo::getInputMethodDefinitions();
    for (const auto& [label, id] : imItems) {
        if (imDefs.find(id.toStdString()) != imDefs.end()) {
            inputMethodCombo_->addItem(label, id);
        }
    }
    for (const auto& [name, _] : imDefs) {
        QString qname = QString::fromStdString(name);
        if (inputMethodCombo_->findData(qname) < 0) {
            inputMethodCombo_->addItem(qname, qname);
        }
    }

    charsetCombo_ = new QComboBox(page);
    for (const std::string& csName : bamboo::getCharsetNames()) {
        QString qcs = QString::fromStdString(csName);
        QString display = (csName == bamboo::CHARSET_UNICODE)
                              ? QStringLiteral("Unicode (dựng sẵn)")
                              : qcs;
        charsetCombo_->addItem(display, qcs);
    }

    methodCharsetRow->addWidget(inputMethodCombo_, 1);
    methodCharsetRow->addWidget(new QLabel(QStringLiteral("Bảng mã:"), page));
    methodCharsetRow->addWidget(charsetCombo_, 1);
    imTopRow->addRow(QStringLiteral("Kiểu gõ:"), methodCharsetRow);
    imBox->addLayout(imTopRow);

    auto* optsGrid = new QGridLayout();
    optsGrid->setHorizontalSpacing(12);
    optsGrid->setVerticalSpacing(4);
    modernToneCheck_ = new QCheckBox(QStringLiteral("Đặt dấu kiểu mới (oà, uý)"), page);
    freeToneCheck_ = new QCheckBox(QStringLiteral("Gõ dấu tự do ở cuối từ"), page);
    spellCheck_ = new QCheckBox(QStringLiteral("Kiểm tra chính tả"), page);
    autoRestoreCheck_ = new QCheckBox(QStringLiteral("Tự trả từ tiếng Anh"), page);
    shorthandInitCheck_ = new QCheckBox(QStringLiteral("Tắt phụ âm đầu (cc→ch...)"), page);
    shorthandFinalCheck_ = new QCheckBox(QStringLiteral("Tắt phụ âm cuối (gg→ng...)"), page);

    optsGrid->addWidget(modernToneCheck_, 0, 0);
    optsGrid->addWidget(freeToneCheck_, 0, 1);
    optsGrid->addWidget(spellCheck_, 1, 0);
    optsGrid->addWidget(autoRestoreCheck_, 1, 1);
    optsGrid->addWidget(shorthandInitCheck_, 2, 0);
    optsGrid->addWidget(shorthandFinalCheck_, 2, 1);
    imBox->addLayout(optsGrid);

    layout->addWidget(makeCard(page, QStringLiteral("Bộ gõ tiếng Việt (BambooEngine)"), imBox));

    // Card 3: Inline quick test
    auto* testRow = new QHBoxLayout();
    liveTestRawEdit_ = new QLineEdit(page);
    liveTestRawEdit_->setPlaceholderText(QStringLiteral("VD: vieejts nam"));
    liveTestOutputEdit_ = new QLineEdit(page);
    liveTestOutputEdit_->setReadOnly(true);
    liveTestOutputEdit_->setPlaceholderText(QStringLiteral("Kết quả xem trước"));

    connect(liveTestRawEdit_, &QLineEdit::textChanged, this, [this](const QString& raw) {
        bamboo::SessionProcessor proc;
        Config previewCfg = collectDraftFromUi();
        bamboo::SessionOptions opts;
        opts.vietnameseEnabled = true;
        opts.inputMethodName = previewCfg.inputMethod;
        opts.modernToneStyle = previewCfg.modernToneStyle;
        opts.freeToneMarking = previewCfg.freeToneMarking;
        opts.spellCheckEnabled = previewCfg.spellCheckEnabled;
        opts.autoRestoreEnglish = previewCfg.autoRestoreEnglish;
        opts.shorthandInitialConsonant = previewCfg.shorthandInitialConsonant;
        opts.shorthandFinalConsonant = previewCfg.shorthandFinalConsonant;
        opts.macroEnabled = previewCfg.macroEnabled;
        opts.macroAutoCase = previewCfg.macroAutoCase;
        proc.setOptions(opts);
        proc.macroTable().setMacros(previewCfg.macros);
        std::u32string screen;
        for (char32_t ch : raw.toStdU32String()) {
            bamboo::KeyActionDiff diff = proc.processPrintableKey(ch);
            if (diff.passThrough) {
                screen.push_back(ch);
            } else {
                for (int i = 0; i < diff.backspaces && !screen.empty(); ++i) {
                    screen.pop_back();
                }
                screen.append(diff.textToEmit);
            }
        }
        liveTestOutputEdit_->setText(QString::fromStdU32String(screen));
    });

    testRow->addWidget(liveTestRawEdit_, 1);
    testRow->addWidget(new QLabel(QStringLiteral("→"), page));
    testRow->addWidget(liveTestOutputEdit_, 1);
    layout->addWidget(makeCard(page, QStringLiteral("Thử nhanh bộ gõ"), testRow));

    layout->addStretch();

    connect(defaultModeCombo_, &QComboBox::currentIndexChanged, this, &SettingsWindow::onUiValueChanged);
    connect(autostartCheck_, &QCheckBox::toggled, this, &SettingsWindow::onUiValueChanged);
    connect(osdNotificationCheck_, &QCheckBox::toggled, this, &SettingsWindow::onUiValueChanged);
    connect(inputMethodCombo_, &QComboBox::currentIndexChanged, this, &SettingsWindow::onUiValueChanged);
    connect(charsetCombo_, &QComboBox::currentIndexChanged, this, &SettingsWindow::onUiValueChanged);
    for (auto* cb : {modernToneCheck_, freeToneCheck_, spellCheck_, autoRestoreCheck_,
                     shorthandInitCheck_, shorthandFinalCheck_}) {
        connect(cb, &QCheckBox::toggled, this, &SettingsWindow::onUiValueChanged);
    }
    return page;
}

QWidget* SettingsWindow::createShortcutsPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* box = new QVBoxLayout();
    box->setSpacing(8);

    auto* hint = new QLabel(
        QStringLiteral("Nhấn vào ô rồi bấm tổ hợp phím mong muốn (gõ modifier+phím, hoặc 2+ modifier như Ctrl+Shift, Alt+Shift). Bấm ✕ để tắt phím tắt."),
        page);
    hint->setWordWrap(true);
    box->addWidget(hint);

    auto* form = new QFormLayout();
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(8);

    scTogglePrimary_ = new HotkeyCaptureWidget(QStringLiteral("Chưa gán"), page);
    scToggleSecondary_ = new HotkeyCaptureWidget(QStringLiteral("Chưa gán"), page);
    scToggleExtra_ = new HotkeyCaptureWidget(QStringLiteral("Tuỳ chọn (VD: Alt+Z)"), page);
    scRestoreWord_ = new HotkeyCaptureWidget(QStringLiteral("Tuỳ chọn (VD: Esc)"), page);
    scPanicSafeMode_ = new HotkeyCaptureWidget(QStringLiteral("VD: Ctrl+Alt+Pause"), page);

    form->addRow(QStringLiteral("Đổi VI ↔ EN (chính):"), scTogglePrimary_);
    form->addRow(QStringLiteral("Đổi VI ↔ EN (phụ 1):"), scToggleSecondary_);
    form->addRow(QStringLiteral("Đổi VI ↔ EN (phụ 2):"), scToggleExtra_);
    form->addRow(QStringLiteral("Khôi phục từ gốc:"), scRestoreWord_);
    form->addRow(QStringLiteral("Tạm dừng khẩn cấp:"), scPanicSafeMode_);
    box->addLayout(form);

    shortcutHintLabel_ = new QLabel(page);
    shortcutHintLabel_->setWordWrap(true);
    box->addWidget(shortcutHintLabel_);

    layout->addWidget(makeCard(page, QStringLiteral("Phím tắt toàn cục (bắt trực tiếp từ evdev)"), box));

    auto* cliLabel = new QLabel(
        QStringLiteral("Phím tắt hệ thống / Script: <code>vnsnowkey ctl toggle | vi | en | status | reload</code>"),
        page);
    cliLabel->setTextFormat(Qt::RichText);
    cliLabel->setWordWrap(true);
    auto* cliBox = new QVBoxLayout();
    cliBox->addWidget(cliLabel);
    layout->addWidget(makeCard(page, QStringLiteral("Điều khiển qua D-Bus & CLI"), cliBox));
    layout->addStretch();

    for (auto* w : {scTogglePrimary_, scToggleSecondary_, scToggleExtra_, scRestoreWord_, scPanicSafeMode_}) {
        connect(w, &HotkeyCaptureWidget::hotkeyChanged, this, &SettingsWindow::onUiValueChanged);
    }
    return page;
}

QWidget* SettingsWindow::createAppsPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* optsRow = new QHBoxLayout();
    rememberPerAppCheck_ = new QCheckBox(QStringLiteral("Ghi nhớ EN/VI theo từng cửa sổ"), page);
    disableInPasswordCheck_ = new QCheckBox(QStringLiteral("Tự tắt ở ô mật khẩu"), page);
    optsRow->addWidget(rememberPerAppCheck_);
    optsRow->addWidget(disableInPasswordCheck_);
    optsRow->addStretch();
    layout->addWidget(makeCard(page, QStringLiteral("Ngữ cảnh cửa sổ"), optsRow));

    auto* tblBox = new QVBoxLayout();
    tblBox->setSpacing(6);

    auto* btnRow = new QHBoxLayout();
    presetAppCombo_ = new QComboBox(page);
    presetAppCombo_->addItem(QStringLiteral("firefox"), QStringLiteral("firefox"));
    presetAppCombo_->addItem(QStringLiteral("chromium"), QStringLiteral("chromium"));
    presetAppCombo_->addItem(QStringLiteral("org.gnome.Terminal"), QStringLiteral("org.gnome.Terminal"));
    presetAppCombo_->addItem(QStringLiteral("org.kde.konsole"), QStringLiteral("org.kde.konsole"));
    presetAppCombo_->addItem(QStringLiteral("code (VS Code)"), QStringLiteral("code"));
    presetAppCombo_->addItem(QStringLiteral("org.telegram.desktop"), QStringLiteral("org.telegram.desktop"));
    presetAppCombo_->addItem(QStringLiteral("libreoffice"), QStringLiteral("libreoffice"));
    presetAppCombo_->addItem(QStringLiteral("minecraft (Game Mode)"), QStringLiteral("minecraft"));

    auto* addProfBtn = new QPushButton(QStringLiteral("+ Thêm"), page);
    auto* delProfBtn = new QPushButton(QStringLiteral("− Xoá dòng"), page);
    connect(addProfBtn, &QPushButton::clicked, this, &SettingsWindow::onAddAppProfileRow);
    connect(delProfBtn, &QPushButton::clicked, this, &SettingsWindow::onRemoveAppProfileRow);

    btnRow->addWidget(new QLabel(QStringLiteral("Mẫu:"), page));
    btnRow->addWidget(presetAppCombo_, 1);
    btnRow->addWidget(addProfBtn);
    btnRow->addWidget(delProfBtn);
    tblBox->addLayout(btnRow);

    appProfilesTable_ = new QTableWidget(0, 5, page);
    appProfilesTable_->setObjectName(QStringLiteral("appProfilesTable"));
    appProfilesTable_->setHorizontalHeaderLabels(
        {QStringLiteral("App Class"), QStringLiteral("Chế độ"), QStringLiteral("Output"),
         QStringLiteral("Delay (µs)"), QStringLiteral("Sửa URL")});
    appProfilesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    appProfilesTable_->setItemDelegateForColumn(1, new AppModeDelegate(appProfilesTable_));
    appProfilesTable_->setAlternatingRowColors(true);
    appProfilesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tblBox->addWidget(appProfilesTable_, 1);

    auto* modeHint = new QLabel(
        QStringLiteral("Mẹo cột Chế độ (nháy đúp để chọn): game_mode — đi thẳng phím khi chơi 3D; tự bật tiếng Việt khi mở chat/UI (nhận diện con trỏ GNOME/XWayland, Wayland IM bridge, hoặc phím mở chat T / / <-> Enter / Esc)."),
        page);
    modeHint->setWordWrap(true);
    modeHint->setObjectName(QStringLiteral("HintLabel"));
    tblBox->addWidget(modeHint);

    layout->addWidget(makeCard(page, QStringLiteral("Profile & Loại trừ từng ứng dụng"), tblBox), 1);

    connect(rememberPerAppCheck_, &QCheckBox::toggled, this, &SettingsWindow::onUiValueChanged);
    connect(disableInPasswordCheck_, &QCheckBox::toggled, this, &SettingsWindow::onUiValueChanged);
    connect(appProfilesTable_, &QTableWidget::itemChanged, this, &SettingsWindow::onUiValueChanged);
    return page;
}

QWidget* SettingsWindow::createMacrosPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* macroBox = new QVBoxLayout();
    macroBox->setSpacing(6);

    auto* topRow = new QHBoxLayout();
    macroEnabledCheck_ = new QCheckBox(QStringLiteral("Bật gõ tắt"), page);
    macroAutoCapCheck_ = new QCheckBox(QStringLiteral("Tự viết hoa"), page);
    auto* addMacroBtn = new QPushButton(QStringLiteral("+ Thêm"), page);
    auto* delMacroBtn = new QPushButton(QStringLiteral("− Xoá"), page);
    auto* importMacroBtn = new QPushButton(QStringLiteral("Nhập..."), page);
    auto* exportMacroBtn = new QPushButton(QStringLiteral("Xuất..."), page);
    connect(addMacroBtn, &QPushButton::clicked, this, &SettingsWindow::onAddMacroRow);
    connect(delMacroBtn, &QPushButton::clicked, this, &SettingsWindow::onRemoveMacroRow);
    connect(importMacroBtn, &QPushButton::clicked, this, &SettingsWindow::onImportMacros);
    connect(exportMacroBtn, &QPushButton::clicked, this, &SettingsWindow::onExportMacros);

    topRow->addWidget(macroEnabledCheck_);
    topRow->addWidget(macroAutoCapCheck_);
    topRow->addStretch();
    topRow->addWidget(addMacroBtn);
    topRow->addWidget(delMacroBtn);
    topRow->addWidget(importMacroBtn);
    topRow->addWidget(exportMacroBtn);
    macroBox->addLayout(topRow);

    macrosTable_ = new QTableWidget(0, 2, page);
    macrosTable_->setHorizontalHeaderLabels({QStringLiteral("Từ viết tắt"), QStringLiteral("Thay thế")});
    macrosTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    macrosTable_->setAlternatingRowColors(true);
    macrosTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    macroBox->addWidget(macrosTable_, 1);

    layout->addWidget(makeCard(page, QStringLiteral("Bảng gõ tắt (Macro)"), macroBox), 1);

    auto* convBox = new QVBoxLayout();
    convBox->setSpacing(6);
    auto* csRow = new QHBoxLayout();
    convFromCharsetCombo_ = new QComboBox(page);
    convToCharsetCombo_ = new QComboBox(page);
    for (const std::string& csName : bamboo::getCharsetNames()) {
        QString cs = QString::fromStdString(csName);
        convFromCharsetCombo_->addItem(cs, cs);
        convToCharsetCombo_->addItem(cs, cs);
    }
    convToCharsetCombo_->addItem(QStringLiteral("Bỏ dấu (ASCII)"), QStringLiteral("ASCII"));
    convFromCharsetCombo_->setCurrentIndex(1);

    auto* convSampleBtn = new QPushButton(QStringLiteral("Chuyển thử"), page);
    auto* convClipboardBtn = new QPushButton(QStringLiteral("Chuyển Clipboard"), page);
    connect(convSampleBtn, &QPushButton::clicked, this, &SettingsWindow::onConvertTextSample);
    connect(convClipboardBtn, &QPushButton::clicked, this, &SettingsWindow::onConvertClipboardNow);

    csRow->addWidget(convFromCharsetCombo_);
    csRow->addWidget(new QLabel(QStringLiteral("→"), page));
    csRow->addWidget(convToCharsetCombo_);
    csRow->addStretch();
    csRow->addWidget(convSampleBtn);
    csRow->addWidget(convClipboardBtn);
    convBox->addLayout(csRow);

    auto* textRow = new QHBoxLayout();
    convSourceEdit_ = new QPlainTextEdit(page);
    convSourceEdit_->setMaximumHeight(80);
    convSourceEdit_->setPlaceholderText(QStringLiteral("Văn bản nguồn..."));
    convResultEdit_ = new QPlainTextEdit(page);
    convResultEdit_->setMaximumHeight(80);
    convResultEdit_->setReadOnly(true);
    convResultEdit_->setPlaceholderText(QStringLiteral("Kết quả..."));
    textRow->addWidget(convSourceEdit_, 1);
    textRow->addWidget(convResultEdit_, 1);
    convBox->addLayout(textRow);

    layout->addWidget(makeCard(page, QStringLiteral("Công cụ chuyển bảng mã"), convBox));

    connect(macroEnabledCheck_, &QCheckBox::toggled, this, &SettingsWindow::onUiValueChanged);
    connect(macroAutoCapCheck_, &QCheckBox::toggled, this, &SettingsWindow::onUiValueChanged);
    connect(macrosTable_, &QTableWidget::itemChanged, this, &SettingsWindow::onUiValueChanged);
    return page;
}

QWidget* SettingsWindow::createAdvancedPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* outForm = new QFormLayout();
    outForm->setHorizontalSpacing(14);
    outForm->setVerticalSpacing(6);

    outputMethodCombo_ = new QComboBox(page);
    outputMethodCombo_->addItem(QStringLiteral("XKB 8-Level Keymap (khuyên dùng)"), QStringLiteral("xkb_keymap"));
    outputMethodCombo_->addItem(QStringLiteral("Clipboard + Ctrl+V"), QStringLiteral("clipboard_ctrl_v"));
    outputMethodCombo_->addItem(QStringLiteral("Clipboard + Ctrl+Shift+V"), QStringLiteral("clipboard_ctrl_shift_v"));
    outputMethodCombo_->addItem(QStringLiteral("Ctrl+Shift+U + Hex"), QStringLiteral("ctrl_shift_u"));
    outForm->addRow(QStringLiteral("Xuất Unicode:"), outputMethodCombo_);

    auto* timingRow = new QHBoxLayout();
    defaultDelaySpin_ = new QSpinBox(page);
    defaultDelaySpin_->setRange(0, 20000);
    defaultDelaySpin_->setSuffix(QStringLiteral(" µs"));
    clipboardRestoreSpin_ = new QSpinBox(page);
    clipboardRestoreSpin_->setRange(20, 2000);
    clipboardRestoreSpin_->setSuffix(QStringLiteral(" ms"));
    bufferTimeoutSpin_ = new QSpinBox(page);
    bufferTimeoutSpin_->setRange(500, 60000);
    bufferTimeoutSpin_->setSingleStep(500);
    bufferTimeoutSpin_->setSuffix(QStringLiteral(" ms"));
    timingRow->addWidget(new QLabel(QStringLiteral("Delay:"), page));
    timingRow->addWidget(defaultDelaySpin_);
    timingRow->addWidget(new QLabel(QStringLiteral("Clipboard:"), page));
    timingRow->addWidget(clipboardRestoreSpin_);
    timingRow->addWidget(new QLabel(QStringLiteral("Hết hạn từ:"), page));
    timingRow->addWidget(bufferTimeoutSpin_);
    outForm->addRow(QStringLiteral("Độ trễ / Bộ đệm:"), timingRow);

    resetMouseClickCheck_ = new QCheckBox(QStringLiteral("Reset khi click chuột"), page);
    resetFocusChangeCheck_ = new QCheckBox(QStringLiteral("Reset khi đổi cửa sổ"), page);
    resetNavKeysCheck_ = new QCheckBox(
        QStringLiteral("Reset ở phím điều hướng (↑ ↓ Home End Enter Tab Esc; ← → giữ buffer để sửa)"),
        page);
    auto* rstRow = new QHBoxLayout();
    rstRow->addWidget(resetMouseClickCheck_);
    rstRow->addWidget(resetFocusChangeCheck_);
    rstRow->addStretch();
    outForm->addRow(QStringLiteral("Ngắt từ:"), rstRow);
    outForm->addRow(QString(), resetNavKeysCheck_);

    layout->addWidget(makeCard(page, QStringLiteral("Cơ chế xuất phím & Đồng bộ buffer"), outForm));

    auto* privBox = new QVBoxLayout();
    privBox->setSpacing(6);
    auto* privChecks = new QHBoxLayout();
    typingStatsCheck_ = new QCheckBox(QStringLiteral("Thống kê số từ trong RAM (không lưu đĩa)"), page);
    debugRedactedCheck_ = new QCheckBox(QStringLiteral("Debug log che nội dung gõ"), page);
    privChecks->addWidget(typingStatsCheck_);
    privChecks->addWidget(debugRedactedCheck_);
    privChecks->addStretch();
    privBox->addLayout(privChecks);

    statsSummaryLabel_ = new QLabel(QStringLiteral("Thống kê phiên (RAM): chưa kết nối daemon"), page);
    privBox->addWidget(statsSummaryLabel_);

    auto* backupRow = new QHBoxLayout();
    auto* backupBtn = new QPushButton(QStringLiteral("Sao lưu cấu hình..."), page);
    auto* restoreBtn = new QPushButton(QStringLiteral("Khôi phục từ file..."), page);
    connect(backupBtn, &QPushButton::clicked, this, &SettingsWindow::onBackupConfig);
    connect(restoreBtn, &QPushButton::clicked, this, &SettingsWindow::onRestoreConfig);
    backupRow->addWidget(backupBtn);
    backupRow->addWidget(restoreBtn);
    backupRow->addStretch();
    privBox->addLayout(backupRow);

    layout->addWidget(makeCard(page, QStringLiteral("Quyền riêng tư & Sao lưu"), privBox));
    layout->addStretch();

    connect(outputMethodCombo_, &QComboBox::currentIndexChanged, this, &SettingsWindow::onUiValueChanged);
    connect(defaultDelaySpin_, &QSpinBox::valueChanged, this, &SettingsWindow::onUiValueChanged);
    connect(clipboardRestoreSpin_, &QSpinBox::valueChanged, this, &SettingsWindow::onUiValueChanged);
    connect(bufferTimeoutSpin_, &QSpinBox::valueChanged, this, &SettingsWindow::onUiValueChanged);
    connect(resetMouseClickCheck_, &QCheckBox::toggled, this, &SettingsWindow::onUiValueChanged);
    connect(resetFocusChangeCheck_, &QCheckBox::toggled, this, &SettingsWindow::onUiValueChanged);
    connect(resetNavKeysCheck_, &QCheckBox::toggled, this, &SettingsWindow::onUiValueChanged);
    connect(typingStatsCheck_, &QCheckBox::toggled, this, &SettingsWindow::onUiValueChanged);
    connect(debugRedactedCheck_, &QCheckBox::toggled, this, &SettingsWindow::onUiValueChanged);
    return page;
}

QWidget* SettingsWindow::createDiagnosticsPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* diagBox = new QVBoxLayout();
    diagBox->setSpacing(6);
    diagnosticsOutputEdit_ = new QPlainTextEdit(page);
    diagnosticsOutputEdit_->setReadOnly(true);
    diagBox->addWidget(diagnosticsOutputEdit_, 1);

    auto* botRow = new QHBoxLayout();
    auto* verLbl = new QLabel(QStringLiteral("<b>VNSnowKey v1.0.0</b> (GPLv3) · Engine: BambooEngine Core (MIT)"), page);
    auto* refreshDiagBtn = new QPushButton(QStringLiteral("Kiểm tra lại"), page);
    connect(refreshDiagBtn, &QPushButton::clicked, this, &SettingsWindow::onRefreshDiagnostics);
    botRow->addWidget(verLbl, 1);
    botRow->addWidget(refreshDiagBtn);
    diagBox->addLayout(botRow);

    layout->addWidget(makeCard(page, QStringLiteral("System Doctor — Quyền evdev / uinput & Daemon"), diagBox), 1);
    return page;
}

void SettingsWindow::loadConfigIntoUi() {
    config_ = Config::loadFromFile(configPath_);
    applyConfigToUi(config_);
    baselineConfig_ = collectDraftFromUi();
    config_ = baselineConfig_;
    lastAutostartState_ = config_.autostart;
    syncSystemdAutostart(config_.autostart, false);
    updateDirtyState();
}

void SettingsWindow::applyConfigToUi(const Config& cfg) {
    loadingUi_ = true;

    int modeIdx = defaultModeCombo_->findData(cfg.vietnameseEnabled
                                              ? QStringLiteral("vi")
                                              : QStringLiteral("en"));
    if (modeIdx >= 0) defaultModeCombo_->setCurrentIndex(modeIdx);
    osdNotificationCheck_->setChecked(cfg.osdEnabled);
    autostartCheck_->setChecked(cfg.autostart);

    int imIdx = inputMethodCombo_->findData(QString::fromStdString(cfg.inputMethod));
    if (imIdx >= 0) inputMethodCombo_->setCurrentIndex(imIdx);
    int csIdx = charsetCombo_->findData(QString::fromStdString(cfg.outputCharset));
    if (csIdx >= 0) charsetCombo_->setCurrentIndex(csIdx);
    modernToneCheck_->setChecked(cfg.modernToneStyle);
    freeToneCheck_->setChecked(cfg.freeToneMarking);
    spellCheck_->setChecked(cfg.spellCheckEnabled);
    autoRestoreCheck_->setChecked(cfg.autoRestoreEnglish);
    shorthandInitCheck_->setChecked(cfg.shorthandInitialConsonant);
    shorthandFinalCheck_->setChecked(cfg.shorthandFinalConsonant);

    scTogglePrimary_->setHotkey(QString::fromStdString(cfg.shortcuts.togglePrimary));
    scToggleSecondary_->setHotkey(QString::fromStdString(cfg.shortcuts.toggleSecondary));
    scToggleExtra_->setHotkey(QString::fromStdString(cfg.shortcuts.toggleExtra));
    scRestoreWord_->setHotkey(QString::fromStdString(cfg.shortcuts.restoreWord));
    scPanicSafeMode_->setHotkey(QString::fromStdString(cfg.shortcuts.panicSafeMode));

    rememberPerAppCheck_->setChecked(cfg.rememberPerAppLang);
    disableInPasswordCheck_->setChecked(cfg.bypassPasswordDialogs);

    appProfilesTable_->setRowCount(0);
    for (const auto& prof : cfg.appProfiles) {
        int r = appProfilesTable_->rowCount();
        appProfilesTable_->insertRow(r);
        appProfilesTable_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(prof.appClass)));
        appProfilesTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(Config::appModeToString(prof.mode))));
        appProfilesTable_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(Config::outputMethodToString(prof.outputMethod))));
        appProfilesTable_->setItem(r, 3, new QTableWidgetItem(QString::number(prof.delayUs)));
        appProfilesTable_->setItem(r, 4, new QTableWidgetItem(prof.autocompleteFix ? QStringLiteral("true") : QStringLiteral("false")));
    }

    macroEnabledCheck_->setChecked(cfg.macroEnabled);
    macroAutoCapCheck_->setChecked(cfg.macroAutoCase);

    macrosTable_->setRowCount(0);
    for (const auto& [k, v] : cfg.macros) {
        int r = macrosTable_->rowCount();
        macrosTable_->insertRow(r);
        macrosTable_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(k)));
        macrosTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(v)));
    }

    int outIdx = outputMethodCombo_->findData(
        QString::fromStdString(Config::outputMethodToString(cfg.defaultOutputMethod)));
    if (outIdx >= 0) outputMethodCombo_->setCurrentIndex(outIdx);
    defaultDelaySpin_->setValue(cfg.defaultDelayUs);
    clipboardRestoreSpin_->setValue(cfg.clipboardRestoreDelayMs);
    resetMouseClickCheck_->setChecked(cfg.resetOnMouseClick);
    resetFocusChangeCheck_->setChecked(cfg.resetOnFocusChange);
    resetNavKeysCheck_->setChecked(cfg.resetOnNavigationKeys);
    bufferTimeoutSpin_->setValue(cfg.bufferTimeoutMs);
    typingStatsCheck_->setChecked(cfg.statsEnabled);
    debugRedactedCheck_->setChecked(cfg.debugRedacted);

    loadingUi_ = false;
    updateShortcutHint();
}

Config SettingsWindow::collectDraftFromUi() const {
    Config draft = baselineConfig_;
    draft.vietnameseEnabled = (defaultModeCombo_->currentData().toString() != QStringLiteral("en"));
    draft.osdEnabled = osdNotificationCheck_->isChecked();
    draft.autostart = autostartCheck_->isChecked();

    draft.inputMethod = inputMethodCombo_->currentData().toString().toStdString();
    draft.outputCharset = charsetCombo_->currentData().toString().toStdString();
    draft.modernToneStyle = modernToneCheck_->isChecked();
    draft.freeToneMarking = freeToneCheck_->isChecked();
    draft.spellCheckEnabled = spellCheck_->isChecked();
    draft.autoRestoreEnglish = autoRestoreCheck_->isChecked();
    draft.shorthandInitialConsonant = shorthandInitCheck_->isChecked();
    draft.shorthandFinalConsonant = shorthandFinalCheck_->isChecked();

    draft.shortcuts.togglePrimary = scTogglePrimary_->hotkey().toStdString();
    draft.shortcuts.toggleSecondary = scToggleSecondary_->hotkey().toStdString();
    draft.shortcuts.toggleExtra = scToggleExtra_->hotkey().toStdString();
    draft.shortcuts.restoreWord = scRestoreWord_->hotkey().toStdString();
    draft.shortcuts.panicSafeMode = scPanicSafeMode_->hotkey().toStdString();

    draft.rememberPerAppLang = rememberPerAppCheck_->isChecked();
    draft.bypassPasswordDialogs = disableInPasswordCheck_->isChecked();

    draft.defaultOutputMethod = Config::stringToOutputMethod(
        outputMethodCombo_->currentData().toString().toStdString());
    draft.defaultDelayUs = defaultDelaySpin_->value();
    draft.clipboardRestoreDelayMs = clipboardRestoreSpin_->value();
    draft.resetOnMouseClick = resetMouseClickCheck_->isChecked();
    draft.resetOnFocusChange = resetFocusChangeCheck_->isChecked();
    draft.resetOnNavigationKeys = resetNavKeysCheck_->isChecked();
    draft.bufferTimeoutMs = bufferTimeoutSpin_->value();
    draft.statsEnabled = typingStatsCheck_->isChecked();
    draft.debugRedacted = debugRedactedCheck_->isChecked();

    draft.appProfiles.clear();
    for (int r = 0; r < appProfilesTable_->rowCount(); ++r) {
        auto* itemClass = appProfilesTable_->item(r, 0);
        if (!itemClass || itemClass->text().trimmed().isEmpty()) continue;
        AppProfile prof;
        prof.appClass = itemClass->text().trimmed().toLower().toStdString();
        if (auto* itemMode = appProfilesTable_->item(r, 1)) {
            prof.mode = Config::stringToAppMode(itemMode->text().trimmed().toStdString());
        }
        if (auto* itemOut = appProfilesTable_->item(r, 2)) {
            prof.outputMethod = Config::stringToOutputMethod(itemOut->text().trimmed().toStdString());
            prof.useCustomOutputMethod = (prof.outputMethod != draft.defaultOutputMethod);
        }
        if (auto* itemDelay = appProfilesTable_->item(r, 3)) {
            prof.delayUs = itemDelay->text().trimmed().toInt();
        }
        if (auto* itemAuto = appProfilesTable_->item(r, 4)) {
            prof.autocompleteFix = (itemAuto->text().trimmed().toLower() == QStringLiteral("true"));
        }
        draft.appProfiles.push_back(prof);
    }

    draft.macroEnabled = macroEnabledCheck_->isChecked();
    draft.macroAutoCase = macroAutoCapCheck_->isChecked();

    draft.macros.clear();
    for (int r = 0; r < macrosTable_->rowCount(); ++r) {
        auto* kItem = macrosTable_->item(r, 0);
        auto* vItem = macrosTable_->item(r, 1);
        if (!kItem || !vItem) continue;
        std::string k = kItem->text().trimmed().toStdString();
        std::string v = vItem->text().toStdString();
        if (!k.empty() && !v.empty()) {
            draft.macros[k] = v;
        }
    }

    return draft;
}

void SettingsWindow::updateShortcutHint() {
    if (!shortcutHintLabel_) return;
    QSet<QString> seen;
    QStringList dups;
    for (const QString& hk : {scTogglePrimary_->hotkey(), scToggleSecondary_->hotkey(),
                              scToggleExtra_->hotkey(), scRestoreWord_->hotkey(),
                              scPanicSafeMode_->hotkey()}) {
        if (hk.isEmpty()) continue;
        if (seen.contains(hk) && !dups.contains(hk)) {
            dups << hk;
        }
        seen.insert(hk);
    }
    if (!dups.isEmpty()) {
        shortcutHintLabel_->setText(
            QStringLiteral("Lưu ý: Tổ hợp <b>%1</b> đang được gán cho nhiều hành động.").arg(dups.join(QStringLiteral(", "))));
    } else {
        shortcutHintLabel_->setText(QStringLiteral("Tất cả tổ hợp phím tắt hợp lệ."));
    }
}

void SettingsWindow::updateDirtyState() {
    Config draft = collectDraftFromUi();
    isDirty_ = (draft.toToml() != baselineConfig_.toToml());
    if (applyBtn_) {
        applyBtn_->setEnabled(isDirty_);
    }
    if (dirtyStatusLabel_) {
        dirtyStatusLabel_->setText(isDirty_
                                       ? QStringLiteral("● Có thay đổi chưa lưu")
                                       : QStringLiteral("Đã đồng bộ"));
    }
}

void SettingsWindow::ActivateSettings() {
    showAndRaise();
}

bool SettingsWindow::IsSettingsVisible() const {
    return isVisible();
}

void SettingsWindow::syncSystemdAutostart(bool enabled, bool runSystemctl) {
    // Keep systemd user services as the ONLY autostart mechanism; remove any legacy XDG autostart .desktop
    const QString legacyDesktop = QDir::homePath() + QStringLiteral("/.config/autostart/org.vnsnowkey.Autostart.desktop");
    if (QFileInfo::exists(legacyDesktop)) {
        QFile::remove(legacyDesktop);
    }

    if (runSystemctl && qgetenv("QT_QPA_PLATFORM") != QByteArrayLiteral("offscreen")) {
        const QString action = enabled ? QStringLiteral("enable") : QStringLiteral("disable");
        QProcess::startDetached(
            QStringLiteral("systemctl"),
            {QStringLiteral("--user"), action, QStringLiteral("vnsnowkeyd.service"), QStringLiteral("vnsnowkey-gui.service")});
    }
}

void SettingsWindow::onUiValueChanged() {
    if (loadingUi_) return;
    updateShortcutHint();
    updateDirtyState();
}

void SettingsWindow::onApplyAndHotReload() {
    if (loadingUi_) return;
    Config draft = collectDraftFromUi();
    config_ = draft;
    config_.saveToFile(configPath_);

    const bool autostartChanged = !hasAppliedOnce_ || (config_.autostart != lastAutostartState_);
    syncSystemdAutostart(config_.autostart, autostartChanged);
    lastAutostartState_ = config_.autostart;
    hasAppliedOnce_ = true;

    if (dbus_ && dbus_->isValid()) {
        dbus_->call(QStringLiteral("ReloadConfig"));
    }

    baselineConfig_ = draft;
    updateDirtyState();
}

void SettingsWindow::onResetDefaults() {
    applyConfigToUi(Config::createDefault());
    updateDirtyState();
}

void SettingsWindow::onAddAppProfileRow() {
    loadingUi_ = true;
    int r = appProfilesTable_->rowCount();
    appProfilesTable_->insertRow(r);
    QString preset = presetAppCombo_->currentData().toString();
    if (preset.isEmpty()) preset = QStringLiteral("org.example.app");
    QString defaultMode = (preset == QStringLiteral("minecraft"))
                              ? QStringLiteral("game_mode")
                              : QStringLiteral("default");
    appProfilesTable_->setItem(r, 0, new QTableWidgetItem(preset));
    appProfilesTable_->setItem(r, 1, new QTableWidgetItem(defaultMode));
    appProfilesTable_->setItem(r, 2, new QTableWidgetItem(outputMethodCombo_->currentData().toString()));
    appProfilesTable_->setItem(r, 3, new QTableWidgetItem(QStringLiteral("0")));
    appProfilesTable_->setItem(r, 4, new QTableWidgetItem(QStringLiteral("false")));
    loadingUi_ = false;
    onUiValueChanged();
}

void SettingsWindow::onRemoveAppProfileRow() {
    int r = appProfilesTable_->currentRow();
    if (r >= 0) {
        appProfilesTable_->removeRow(r);
        onUiValueChanged();
    }
}

void SettingsWindow::onAddMacroRow() {
    loadingUi_ = true;
    int r = macrosTable_->rowCount();
    macrosTable_->insertRow(r);
    macrosTable_->setItem(r, 0, new QTableWidgetItem(QStringLiteral("tuviettat")));
    macrosTable_->setItem(r, 1, new QTableWidgetItem(QStringLiteral("cụm từ thay thế")));
    loadingUi_ = false;
    onUiValueChanged();
}

void SettingsWindow::onRemoveMacroRow() {
    int r = macrosTable_->currentRow();
    if (r >= 0) {
        macrosTable_->removeRow(r);
        onUiValueChanged();
    }
}

void SettingsWindow::onImportMacros() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Chọn file Macro để Import"), QDir::homePath(),
        QStringLiteral("Macro Files (*.txt *.conf *.ini);;All Files (*)"));
    if (path.isEmpty()) return;

    Config draft = collectDraftFromUi();
    bamboo::MacroTable current;
    current.setMacros(draft.macros);
    if (current.importFromFile(path.toStdString())) {
        draft.macros = current.getMacros();
        applyConfigToUi(draft);
        updateDirtyState();
    }
}

void SettingsWindow::onExportMacros() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Chọn nơi lưu file Macro Export"), QDir::homePath() + QStringLiteral("/vnsnowkey-macros.txt"),
        QStringLiteral("Macro Files (*.txt);;All Files (*)"));
    if (path.isEmpty()) return;

    Config draft = collectDraftFromUi();
    bamboo::MacroTable current;
    current.setMacros(draft.macros);
    current.exportToFile(path.toStdString());
}

void SettingsWindow::onConvertTextSample() {
    const std::string src = convSourceEdit_->toPlainText().toStdString();
    const std::string fromCs = convFromCharsetCombo_->currentData().toString().toStdString();
    const std::string toCs = convToCharsetCombo_->currentData().toString().toStdString();
    std::string out;
    if (toCs == "ASCII") {
        out = bamboo::convertCharset(fromCs, bamboo::CHARSET_UNICODE, src, true);
    } else {
        out = bamboo::convertCharset(fromCs, toCs, src, false);
    }
    convResultEdit_->setPlainText(QString::fromStdString(out));
}

void SettingsWindow::onConvertClipboardNow() {
    const QString fromCs = convFromCharsetCombo_->currentData().toString();
    const QString toCs = convToCharsetCombo_->currentData().toString();
    if (dbus_ && dbus_->isValid()) {
        dbus_->call(QStringLiteral("ConvertClipboard"), fromCs, toCs, toCs == QStringLiteral("ASCII"));
    }
    if (QClipboard* clip = QApplication::clipboard()) {
        const std::string src = clip->text().toStdString();
        std::string out;
        if (toCs == QStringLiteral("ASCII")) {
            out = bamboo::convertCharset(fromCs.toStdString(), bamboo::CHARSET_UNICODE, src, true);
        } else {
            out = bamboo::convertCharset(fromCs.toStdString(), toCs.toStdString(), src, false);
        }
        clip->setText(QString::fromStdString(out));
        convResultEdit_->setPlainText(QString::fromStdString(out));
    }
}

void SettingsWindow::onBackupConfig() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Sao lưu cấu hình VNSnowKey"),
        QDir::homePath() + QStringLiteral("/vnsnowkey-config-backup.toml"),
        QStringLiteral("TOML Config (*.toml);;All Files (*)"));
    if (path.isEmpty()) return;
    collectDraftFromUi().saveToFile(path.toStdString());
}

void SettingsWindow::onRestoreConfig() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Khôi phục cấu hình VNSnowKey"),
        QDir::homePath(),
        QStringLiteral("TOML Config (*.toml);;All Files (*)"));
    if (path.isEmpty()) return;
    bool ok = false;
    Config loaded = Config::loadFromFile(path.toStdString(), &ok);
    if (ok) {
        applyConfigToUi(loaded);
        onApplyAndHotReload();
    }
}

void SettingsWindow::onToggleDaemonModeClicked() {
    if (dbus_ && dbus_->isValid()) {
        dbus_->call(QStringLiteral("Toggle"));
        onRefreshDiagnostics();
    }
}

void SettingsWindow::onPanicOrResumeClicked() {
    if (!dbus_ || !dbus_->isValid()) return;
    QDBusReply<QString> reply = dbus_->call(QStringLiteral("GetStatus"));
    if (reply.isValid() && reply.value().contains(QStringLiteral("safe_mode=true"))) {
        dbus_->call(QStringLiteral("ResumeFromPanic"));
    } else {
        dbus_->call(QStringLiteral("PanicStop"));
    }
    onRefreshDiagnostics();
}

void SettingsWindow::onRefreshDiagnostics() {
    QString report;
    QTextStream ts(&report);
    ts << "=== CHẨN ĐOÁN HỆ THỐNG VNSNOWKEY (SYSTEM DOCTOR) ===\n\n";

    // 1. Check /dev/uinput
    int ufd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufd >= 0) {
        ::close(ufd);
        ts << "[OK] Quyền ghi /dev/uinput: Đạt (Có thể tạo bàn phím ảo uinput)\n";
    } else {
        ts << "[LỖI] Không thể mở /dev/uinput! Chạy lệnh khắc phục:\n"
           << "      sudo modprobe uinput\n"
           << "      sudo usermod -aG input $USER\n"
           << "      sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=misc --subsystem-match=input\n";
    }

    // 2. Check /dev/input/event*
    int readableEvents = 0;
    QDir devInput(QStringLiteral("/dev/input"));
    for (const QString& entry : devInput.entryList({QStringLiteral("event*")}, QDir::System | QDir::Files)) {
        const QString fullPath = QStringLiteral("/dev/input/") + entry;
        int fd = ::open(fullPath.toStdString().c_str(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            ++readableEvents;
            ::close(fd);
        }
    }
    if (readableEvents > 0) {
        ts << "[OK] Quyền đọc /dev/input/event*: Đạt (" << readableEvents << " thiết bị đọc được)\n";
    } else {
        ts << "[LỖI] Không thể đọc /dev/input/event*! Hãy thêm user vào nhóm 'input':\n"
           << "      sudo usermod -aG input $USER && newgrp input\n";
    }

    // 3. Check XKB Overlay
    if (XkbOverlay::isOverlayInstalled()) {
        ts << "[OK] XKB 8-Level Vietnamese Overlay (~/.config/xkb/symbols/us): Đã cài đặt\n";
    } else {
        ts << "[CHÚ Ý] XKB Overlay chưa được tạo (vnsnowkeyd sẽ tự động cài khi khởi chạy).\n";
    }

    // 4. Check Daemon status over D-Bus
    if (dbus_ && dbus_->isValid()) {
        QDBusReply<QString> statusReply = dbus_->call(QStringLiteral("GetStatus"));
        if (statusReply.isValid()) {
            ts << "\n=== TRẠNG THÁI DAEMON TRỰC TIẾP (org.vnsnowkey.Daemon) ===\n"
               << statusReply.value();

            QString mode = QStringLiteral("vi");
            QString im = QStringLiteral("Telex");
            bool safe = false;
            QString words = QStringLiteral("0");
            QString keys = QStringLiteral("0");
            for (const QString& line : statusReply.value().split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                int eq = line.indexOf(QLatin1Char('='));
                if (eq <= 0) continue;
                QString k = line.left(eq).trimmed();
                QString v = line.mid(eq + 1).trimmed();
                if (k == QStringLiteral("mode")) mode = v;
                else if (k == QStringLiteral("input_method")) im = v;
                else if (k == QStringLiteral("safe_mode")) safe = (v == QStringLiteral("true"));
                else if (k == QStringLiteral("words_typed")) words = v;
                else if (k == QStringLiteral("keys_typed")) keys = v;
            }
            updateDaemonLiveStatus(mode, im, safe);
            if (statsSummaryLabel_) {
                statsSummaryLabel_->setText(
                    QStringLiteral("Thống kê phiên hiện tại (RAM): <b>%1</b> từ / <b>%2</b> phím")
                        .arg(words, keys));
            }
        } else {
            ts << "\n[CHÚ Ý] Daemon vnsnowkeyd hiện chưa chạy. Khởi động bằng:\n"
               << "        systemctl --user start vnsnowkeyd.service\n";
        }
    }

    if (diagnosticsOutputEdit_) {
        diagnosticsOutputEdit_->setPlainText(report);
    }
}

} // namespace vnsnowkey
