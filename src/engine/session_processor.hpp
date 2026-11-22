/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "bamboo_engine.hpp"
#include "charset_encoder.hpp"
#include "macro_table.hpp"
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace vnsnowkey::bamboo {

struct SessionOptions {
    bool vietnameseEnabled = true;
    std::string inputMethodName = "Telex";
    std::string outputCharset = CHARSET_UNICODE;
    bool freeToneMarking = true;
    bool modernToneStyle = true;          // true: hoà, thuỷ; false: hòa, thủy
    bool spellCheckEnabled = true;
    bool autoRestoreEnglish = false;      // restore "windows", "search", etc.
    bool shorthandInitialConsonant = false; // cc->ch, gg->gi, kk->kh, nn->ng, qq->qu, pp->ph, tt->th
    bool shorthandFinalConsonant = false;   // g->ng, h->nh, k->ch
    bool macroEnabled = true;
    bool macroAutoCase = true;
};

struct KeyActionDiff {
    bool passThrough = false;     // true if caller should just forward the raw key event
    int backspaces = 0;           // number of Backspace taps to emit before replacement
    std::u32string textToEmit;    // replacement Unicode characters to emit (or encoded if non-Unicode)
    std::string encodedToEmit;    // UTF-8 string (in target charset) to emit
    bool wasTransformed = false;  // true if Vietnamese transformation / backtrack / macro occurred
    bool wasMacroExpanded = false;
    bool wasRestored = false;
};

struct SessionStats {
    uint64_t totalKeysProcessed = 0;
    uint64_t vietnameseTransforms = 0;
    uint64_t backtracksCount = 0;
    uint64_t englishRestoresCount = 0;
    uint64_t macrosExpandedCount = 0;
};

class SessionProcessor {
public:
    SessionProcessor();

    void setOptions(const SessionOptions& opts);
    const SessionOptions& getOptions() const { return options_; }

    void setVietnameseEnabled(bool enabled);
    bool isVietnameseEnabled() const { return options_.vietnameseEnabled; }

    MacroTable& macroTable() { return macroTable_; }
    const MacroTable& macroTable() const { return macroTable_; }

    // Process a printable character typed by user
    KeyActionDiff processPrintableKey(char32_t ch);

    // Process Backspace key pressed by user
    KeyActionDiff processBackspace();

    // Track cursor movement across recently committed words (Left/Right Arrow & Delete)
    void moveCursorLeft();
    void moveCursorRight();
    void processDelete();

    // Restore current word to raw English keystrokes (e.g. on Esc or shortcut)
    KeyActionDiff restoreCurrentWord();

    // Reset internal word buffer (on mouse click, focus change, arrow keys, timeout, Ctrl/Alt/Super)
    void reset();

    // Handle left mouse click when resetOnMouseClick is disabled: clear committed history and
    // raw-locked English state, and arm post-click divergence guard on any active Vietnamese buffer.
    void onMouseClickWithoutReset();

    // Inspect current displayed word & raw buffer
    const std::u32string& currentDisplayedWord() const { return displayedWord_; }
    const std::u32string& currentRawKeys() const { return rawKeys_; }
    size_t cursorInWord() const { return cursorInWord_; }
    bool hasUndoableHistory() const { return !leftTokens_.empty(); }

    const SessionStats& stats() const { return stats_; }
    void resetStats() { stats_ = SessionStats{}; }

    // Compute minimal LCP diff between oldStr and newStr
    static KeyActionDiff computeDiff(std::u32string_view oldStr, std::u32string_view newStr, const std::string& charset);

private:
    struct CommittedToken {
        bool isDelimiter = false;
        char32_t delimiter = 0;
        std::u32string rawKeys;
        std::u32string displayedWord;
        std::vector<uint8_t> keyForcedEnglish;
        bool wordLockedRaw = false;
        size_t restoredAtRawLen = 0;
        size_t peakRawLenSinceLock = 0;
    };
    static constexpr size_t kMaxHistoryTokens = 64;

    void clearActiveWord();
    void pushLeftToken(CommittedToken tok);
    void pushRightFront(CommittedToken tok);
    void restoreTokenToActive(CommittedToken tok, size_t cursorPos);
    void loadDisplayedFragment(std::u32string_view vieFrag, bool keepLockedRaw);
    CommittedToken makeTokenFromDisplayed(std::u32string_view vieFrag, bool keepLockedRaw) const;
    std::u32string reconstructRawKeysForFragment(std::u32string_view vieFrag, bool& outOk) const;
    void rebuildEngineFromOptions();
    void clearPostClickBoundary();
    std::u32string postClickDisplayedTail() const;
    KeyActionDiff replayPostClickPrintable(
        std::u32string_view newPostRaw, std::u32string_view oldPostDisp, char32_t lastCh);
    bool dropPreClickIfActiveForCursorEdit();
    std::u32string evaluateBufferWithFeatures(
        bool isWordComplete,
        bool* outRestored = nullptr,
        bool* outSuppressLastKey = nullptr,
        bool* outValidSingleSyllable = nullptr) const;
    bool shouldAutoRestoreInProgress(const BambooEngine& eng, std::u32string_view raw, std::u32string_view vie) const;

    SessionOptions options_;
    BambooEngine engine_;
    MacroTable macroTable_;

    std::u32string rawKeys_;
    std::u32string displayedWord_;
    std::vector<uint8_t> keyForcedEnglish_;
    bool wordLockedRaw_ = false;
    size_t restoredAtRawLen_ = 0;
    size_t peakRawLenSinceLock_ = 0;
    size_t cursorInWord_ = 0;
    bool hasPostClickBoundary_ = false;
    std::u32string preClickRaw_;
    std::u32string preClickDisplayed_;
    std::u32string postClickAddedRaw_;
    std::vector<CommittedToken> leftTokens_;
    std::deque<CommittedToken> rightTokens_;
    SessionStats stats_;
};

} // namespace vnsnowkey::bamboo
