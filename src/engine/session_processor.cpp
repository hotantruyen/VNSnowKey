/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "session_processor.hpp"
#include "bamboo_utils.hpp"
#include "rules_parser.hpp"
#include <algorithm>

namespace vnsnowkey::bamboo {

namespace {

uint32_t computeEngineFlags(const SessionOptions& opts) {
    uint32_t flags = 0;
    if (opts.freeToneMarking) flags |= EfreeToneMarking;
    // In BambooEngine, EstdToneStyle denotes legacy/old tone placement (hòa, thủy),
    // while omitting EstdToneStyle gives modern placement (hoà, thuỷ).
    if (!opts.modernToneStyle) flags |= EstdToneStyle;
    if (opts.spellCheckEnabled) flags |= EautoCorrectEnabled;
    return flags;
}

// Check if two characters form an initial consonant shorthand pair:
// cc->ch, gg->gi, kk->kh, nn->ng, qq->qu, pp->ph, tt->th
bool getInitialShorthandSecondChar(char32_t firstLower, char32_t& outSecondLower) {
    switch (firstLower) {
        case U'c': outSecondLower = U'h'; return true;
        case U'g': outSecondLower = U'i'; return true;
        case U'k': outSecondLower = U'h'; return true;
        case U'n': outSecondLower = U'g'; return true;
        case U'q': outSecondLower = U'u'; return true;
        case U'p': outSecondLower = U'h'; return true;
        case U't': outSecondLower = U'h'; return true;
        default: return false;
    }
}

// Check if a character is a final consonant shorthand trigger:
// g->ng, h->nh, k->ch
bool getFinalShorthandExpansion(char32_t lastLower, std::u32string& outExpandedLower) {
    switch (lastLower) {
        case U'g': outExpandedLower = U"ng"; return true;
        case U'h': outExpandedLower = U"nh"; return true;
        case U'k': outExpandedLower = U"ch"; return true;
        default: return false;
    }
}

} // namespace

SessionProcessor::SessionProcessor()
    : engine_(parseInputMethod(getInputMethodDefinitions(), "Telex"), EstdFlags) {
    rebuildEngineFromOptions();
}

void SessionProcessor::setOptions(const SessionOptions& opts) {
    options_ = opts;
    macroTable_.setEnabled(opts.macroEnabled);
    macroTable_.setAutoCase(opts.macroAutoCase);
    rebuildEngineFromOptions();
}

void SessionProcessor::setVietnameseEnabled(bool enabled) {
    if (options_.vietnameseEnabled != enabled) {
        options_.vietnameseEnabled = enabled;
        reset();
    }
}

void SessionProcessor::rebuildEngineFromOptions() {
    auto im = parseInputMethod(getInputMethodDefinitions(), options_.inputMethodName);
    if (im.name.empty()) {
        im = parseInputMethod(getInputMethodDefinitions(), "Telex");
    }
    engine_.setInputMethod(std::move(im));
    engine_.setFlag(computeEngineFlags(options_));
    reset();
}

void SessionProcessor::clearPostClickBoundary() {
    hasPostClickBoundary_ = false;
    preClickRaw_.clear();
    preClickDisplayed_.clear();
    postClickAddedRaw_.clear();
}

void SessionProcessor::clearActiveWord() {
    engine_.reset();
    rawKeys_.clear();
    displayedWord_.clear();
    keyForcedEnglish_.clear();
    wordLockedRaw_ = false;
    restoredAtRawLen_ = 0;
    peakRawLenSinceLock_ = 0;
    cursorInWord_ = 0;
    clearPostClickBoundary();
}

void SessionProcessor::pushLeftToken(CommittedToken tok) {
    leftTokens_.push_back(std::move(tok));
    if (leftTokens_.size() > kMaxHistoryTokens) {
        leftTokens_.erase(leftTokens_.begin(), leftTokens_.begin() + (leftTokens_.size() - kMaxHistoryTokens));
    }
}

void SessionProcessor::pushRightFront(CommittedToken tok) {
    rightTokens_.push_front(std::move(tok));
    while (rightTokens_.size() > kMaxHistoryTokens) {
        rightTokens_.pop_back();
    }
}

void SessionProcessor::restoreTokenToActive(CommittedToken tok, size_t cursorPos) {
    rawKeys_ = std::move(tok.rawKeys);
    displayedWord_ = std::move(tok.displayedWord);
    keyForcedEnglish_ = std::move(tok.keyForcedEnglish);
    if (keyForcedEnglish_.size() != rawKeys_.size()) {
        keyForcedEnglish_.resize(rawKeys_.size(), 0);
    }
    wordLockedRaw_ = tok.wordLockedRaw;
    restoredAtRawLen_ = tok.restoredAtRawLen;
    peakRawLenSinceLock_ = tok.peakRawLenSinceLock;
    cursorInWord_ = std::min(cursorPos, displayedWord_.size());
    engine_.reset();
    if (!wordLockedRaw_) {
        for (size_t i = 0; i < rawKeys_.size(); ++i) {
            const Mode km = (i < keyForcedEnglish_.size() && keyForcedEnglish_[i])
                                ? EnglishMode
                                : VietnameseMode;
            engine_.processKey(rawKeys_[i], km);
        }
    }
}

std::u32string SessionProcessor::reconstructRawKeysForFragment(std::u32string_view vieFrag, bool& outOk) const {
    outOk = false;
    if (vieFrag.empty()) {
        outOk = true;
        return {};
    }

    const InputMethod& im = engine_.getInputMethod();
    auto findMarkKey = [&](char32_t baseLower, Mark m) -> char32_t {
        if (m == Mark::None || m == Mark::Raw) return 0;
        for (const auto& rule : im.rules) {
            if (rule.effectType == EffectType::MarkTransformation &&
                rule.getMark() == m &&
                (rule.effectOn == baseLower || rule.effectOn == 0)) {
                return rule.key;
            }
        }
        return 0;
    };
    auto findToneKey = [&](Tone t) -> char32_t {
        if (t == Tone::None) return 0;
        for (const auto& rule : im.rules) {
            if (rule.effectType == EffectType::ToneTransformation && rule.getTone() == t) {
                return rule.key;
            }
        }
        return 0;
    };

    std::u32string rebuilt;
    Tone activeTone = Tone::None;
    for (char32_t c : vieFrag) {
        char32_t base = c;
        Mark m = Mark::None;
        Tone t = Tone::None;
        decomposeVietnameseChar(c, base, m, t);
        if (t != Tone::None) {
            activeTone = t;
        }
        if (!engine_.canProcessKey(toLowerChar(base))) {
            return std::u32string(vieFrag);
        }
        rebuilt.push_back(base);
        if (m != Mark::None) {
            char32_t mk = findMarkKey(toLowerChar(base), m);
            if (mk == 0) {
                return std::u32string(vieFrag);
            }
            rebuilt.push_back(isUpperChar(base) ? toUpperChar(mk) : mk);
        }
    }
    if (activeTone != Tone::None) {
        char32_t tk = findToneKey(activeTone);
        if (tk == 0) {
            return std::u32string(vieFrag);
        }
        rebuilt.push_back(tk);
    }

    BambooEngine verify(im, engine_.getFlag());
    for (char32_t k : rebuilt) {
        verify.processKey(k, VietnameseMode);
    }
    if (verify.getProcessedU32(VietnameseMode) == vieFrag) {
        outOk = true;
        return rebuilt;
    }
    return std::u32string(vieFrag);
}

SessionProcessor::CommittedToken SessionProcessor::makeTokenFromDisplayed(
    std::u32string_view vieFrag, bool keepLockedRaw) const {
    CommittedToken tok;
    tok.isDelimiter = false;
    tok.displayedWord = std::u32string(vieFrag);
    if (keepLockedRaw) {
        tok.rawKeys = tok.displayedWord;
        tok.keyForcedEnglish.assign(tok.rawKeys.size(), 0);
        tok.wordLockedRaw = true;
        return tok;
    }
    bool ok = false;
    std::u32string raw = reconstructRawKeysForFragment(vieFrag, ok);
    if (ok) {
        tok.rawKeys = std::move(raw);
        tok.wordLockedRaw = false;
    } else {
        tok.rawKeys = tok.displayedWord;
        tok.wordLockedRaw = true;
    }
    tok.keyForcedEnglish.assign(tok.rawKeys.size(), 0);
    return tok;
}

void SessionProcessor::loadDisplayedFragment(std::u32string_view vieFrag, bool keepLockedRaw) {
    if (vieFrag.empty()) {
        clearActiveWord();
        return;
    }
    CommittedToken tok = makeTokenFromDisplayed(vieFrag, keepLockedRaw);
    size_t len = tok.displayedWord.size();
    restoreTokenToActive(std::move(tok), len);
}

void SessionProcessor::reset() {
    clearActiveWord();
    leftTokens_.clear();
    rightTokens_.clear();
}

void SessionProcessor::onMouseClickWithoutReset() {
    leftTokens_.clear();
    rightTokens_.clear();

    // Repeated clicks with no typing (e.g., double-click selection) or already locked/split state
    // cannot safely continue the pre-click word buffer.
    if (hasPostClickBoundary_ || !options_.vietnameseEnabled || wordLockedRaw_ ||
        rawKeys_.empty() || displayedWord_.empty() || cursorInWord_ < displayedWord_.size()) {
        clearActiveWord();
        return;
    }

    hasPostClickBoundary_ = true;
    preClickRaw_ = rawKeys_;
    preClickDisplayed_ = displayedWord_;
    postClickAddedRaw_.clear();
}

KeyActionDiff SessionProcessor::computeDiff(
    std::u32string_view oldStr, std::u32string_view newStr, const std::string& charset) {
    KeyActionDiff diff;
    if (charset.empty() || charset == CHARSET_UNICODE) {
        size_t lcp = 0;
        const size_t minLen = std::min(oldStr.size(), newStr.size());
        while (lcp < minLen && oldStr[lcp] == newStr[lcp]) {
            ++lcp;
        }
        diff.backspaces = static_cast<int>(oldStr.size() - lcp);
        diff.textToEmit = std::u32string(newStr.substr(lcp));
        diff.encodedToEmit = u32ToUtf8(diff.textToEmit);
    } else {
        // For multi-byte legacy charsets (VNI Windows, TCVN3, VIQR, Composite Unicode),
        // compare on the encoded UTF-32 codepoints so Backspace counts match actual emitted chars.
        std::u32string oldEnc = utf8ToU32(encodeCharset(charset, u32ToUtf8(oldStr)));
        std::u32string newEnc = utf8ToU32(encodeCharset(charset, u32ToUtf8(newStr)));
        size_t lcp = 0;
        const size_t minLen = std::min(oldEnc.size(), newEnc.size());
        while (lcp < minLen && oldEnc[lcp] == newEnc[lcp]) {
            ++lcp;
        }
        diff.backspaces = static_cast<int>(oldEnc.size() - lcp);
        diff.textToEmit = std::u32string(newEnc.substr(lcp));
        diff.encodedToEmit = u32ToUtf8(diff.textToEmit);
    }
    return diff;
}

static bool isExplicitEffectUndo(
    const BambooEngine& eng, std::u32string_view raw, std::u32string_view vie) {
    // Immediate double-typing of an effect key (e.g. "tooss" -> "tôs", "toorr" -> "tôr", "dd" -> "đ", "ddd" -> "dd")
    if (raw.size() >= 2) {
        char32_t lastLower = toLowerChar(raw.back());
        char32_t prevLower = toLowerChar(raw[raw.size() - 2]);
        if (lastLower == prevLower && inKeyList(eng.getInputMethod().keys, lastLower)) {
            return true;
        }
    }
    // Also preserve in-word undo when an earlier effect key is typed again after intervening
    // consonants/vowels (e.g., "taasns" -> "tâns"), so hitting Backspace immediately returns "tấn".
    if (raw.size() >= 3) {
        char32_t lastLower = toLowerChar(raw.back());
        if (inKeyList(eng.getInputMethod().keys, lastLower)) {
            bool appearedEarlier = false;
            for (size_t i = 0; i + 2 < raw.size(); ++i) {
                if (toLowerChar(raw[i]) == lastLower) {
                    appearedEarlier = true;
                    break;
                }
            }
            if (appearedEarlier) {
                BambooEngine prefixEng(eng.getInputMethod(), eng.getFlag());
                for (size_t i = 0; i + 1 < raw.size(); ++i) {
                    prefixEng.processKey(raw[i], VietnameseMode);
                }
                std::u32string prefixVie = prefixEng.getProcessedU32(VietnameseMode);
                auto [prefixPrevSyl, prefixLastSyl] = extractLastSyllable(prefixEng.getComposition());
                if (prefixPrevSyl.empty() && prefixEng.isValid(false) &&
                    prefixVie != raw.substr(0, raw.size() - 1) &&
                    vie.size() == prefixVie.size() + 1 &&
                    toLowerChar(vie.back()) == lastLower) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool SessionProcessor::shouldAutoRestoreInProgress(
    const BambooEngine& eng, std::u32string_view raw, std::u32string_view vie) const {
    if (!options_.spellCheckEnabled || !options_.autoRestoreEnglish) {
        return false;
    }
    if (raw == vie) {
        return false;
    }
    if (isExplicitEffectUndo(eng, raw, vie)) {
        return false;
    }
    // If the engine says the current word is invalid in Vietnamese even as an incomplete syllable
    if (!eng.isValid(false)) {
        return true;
    }
    // Check if extractLastSyllable split the single word into multiple syllables because an earlier prefix was invalid
    // (e.g., "search" -> "se" + "ảch", where "seảch" is not a single valid syllable).
    auto [prevSyl, lastSyl] = extractLastSyllable(eng.getComposition());
    if (!prevSyl.empty()) {
        return true;
    }
    return false;
}

std::u32string SessionProcessor::evaluateBufferWithFeatures(
    bool isWordComplete,
    bool* outRestored,
    bool* outSuppressLastKey,
    bool* outValidSingleSyllable) const {
    if (outRestored) *outRestored = false;
    if (outSuppressLastKey) *outSuppressLastKey = false;
    if (outValidSingleSyllable) *outValidSingleSyllable = false;
    if (rawKeys_.empty()) return {};
    if (wordLockedRaw_) {
        if (outRestored) *outRestored = true;
        return rawKeys_;
    }

    // Prepare effective key sequence with optional initial/final consonant shorthand
    std::u32string effectiveKeys = rawKeys_;
    std::vector<uint8_t> effectiveForced = keyForcedEnglish_;
    if (effectiveForced.size() != effectiveKeys.size()) {
        effectiveForced.resize(effectiveKeys.size(), 0);
    }
    bool appliedInitialShorthand = false;
    if (options_.shorthandInitialConsonant && effectiveKeys.size() >= 2 &&
        !effectiveForced[0] && !effectiveForced[1]) {
        char32_t c0 = toLowerChar(effectiveKeys[0]);
        char32_t c1 = toLowerChar(effectiveKeys[1]);
        char32_t repl = 0;
        // Apply cc->ch, gg->gi, kk->kh, nn->ng, qq->qu, pp->ph, tt->th
        // unless user typed 3 identical consonants (e.g., ccc -> cc)
        if (c0 == c1 && getInitialShorthandSecondChar(c0, repl)) {
            bool tripleSame = (effectiveKeys.size() >= 3 && toLowerChar(effectiveKeys[2]) == c0);
            if (!tripleSame) {
                bool secondUpper = isUpperChar(effectiveKeys[1]);
                char32_t expandedSecond = secondUpper ? toUpperChar(repl) : repl;
                effectiveKeys[1] = expandedSecond;
                // If the user also typed the second letter of the digraph explicitly (e.g. "ggia", "qqua", "nngu"),
                // coalesce the redundant letter so both "ggaf"/"ggiaf", "qqas"/"qquas", "nnuwowif"/"nnguwowif" work!
                if (effectiveKeys.size() >= 3 && toLowerChar(effectiveKeys[2]) == repl) {
                    effectiveKeys.erase(effectiveKeys.begin() + 2);
                    effectiveForced.erase(effectiveForced.begin() + 2);
                }
                appliedInitialShorthand = true;
            }
        }
    }

    BambooEngine testEngine(engine_.getInputMethod(), engine_.getFlag());
    for (size_t i = 0; i < effectiveKeys.size(); ++i) {
        const Mode km = effectiveForced[i] ? EnglishMode : VietnameseMode;
        testEngine.processKey(effectiveKeys[i], km);
    }

    // Optional final consonant shorthand: g->ng, h->nh, k->ch after a vowel
    if (options_.shorthandFinalConsonant && effectiveKeys.size() >= 2 && !effectiveForced.back()) {
        char32_t lastKey = effectiveKeys.back();
        char32_t lastLower = toLowerChar(lastKey);
        std::u32string expLower;
        if (getFinalShorthandExpansion(lastLower, expLower)) {
            // Check if the character before the last key produced a vowel in the syllable
            BambooEngine prefixEngine(engine_.getInputMethod(), engine_.getFlag());
            for (size_t i = 0; i + 1 < effectiveKeys.size(); ++i) {
                const Mode km = effectiveForced[i] ? EnglishMode : VietnameseMode;
                prefixEngine.processKey(effectiveKeys[i], km);
            }
            std::u32string prefixVie = prefixEngine.getProcessedU32(VietnameseMode);
            if (!prefixVie.empty() && isVowel(toLowerChar(prefixVie.back()))) {
                bool isUpper = isUpperChar(lastKey);
                BambooEngine finalEngine = prefixEngine;
                for (char32_t ec : expLower) {
                    finalEngine.processKey(isUpper ? toUpperChar(ec) : ec, VietnameseMode);
                }
                if (finalEngine.isValid(true)) {
                    testEngine = std::move(finalEngine);
                }
            }
        }
    }

    std::u32string vieOut = testEngine.getProcessedU32(VietnameseMode);

    // Check spellcheck & auto-restore
    if (options_.spellCheckEnabled && options_.autoRestoreEnglish) {
        if (shouldAutoRestoreInProgress(testEngine, rawKeys_, vieOut)) {
            if (outRestored) *outRestored = true;
            return rawKeys_;
        }
        if (isWordComplete && vieOut != rawKeys_) {
            bool explicitDoubleUndo = false;
            if (rawKeys_.size() >= 2) {
                char32_t lastLower = toLowerChar(rawKeys_.back());
                char32_t prevLower = toLowerChar(rawKeys_[rawKeys_.size() - 2]);
                if (lastLower == prevLower && inKeyList(testEngine.getInputMethod().keys, lastLower)) {
                    explicitDoubleUndo = true;
                }
            }
            if (!explicitDoubleUndo && !testEngine.isValid(true)) {
                if (outRestored) *outRestored = true;
                return rawKeys_;
            }
            // If initial shorthand was applied on a 2-letter consonant pair with no vowel (e.g. just "cc" + Space),
            // restore it to "cc" because "ch" alone has no vowel.
            if (appliedInitialShorthand && !hasAnyVietnameseVowel(vieOut)) {
                if (outRestored) *outRestored = true;
                return rawKeys_;
            }
        }
    } else if (options_.spellCheckEnabled && !options_.autoRestoreEnglish && vieOut != rawKeys_) {
        // Even with Auto-Restore OFF, do not apply a diacritic to a non-Vietnamese /
        // multi-fragment prefix (e.g., "moder", "shift", "shader", "archlinux", "arrchlinux").
        auto [testPrevSyl, testLastSyl] = extractLastSyllable(testEngine.getComposition());
        if (!testPrevSyl.empty() || !testEngine.isValid(false)) {
            BambooEngine prefixEngine(engine_.getInputMethod(), engine_.getFlag());
            for (size_t i = 0; i + 1 < effectiveKeys.size(); ++i) {
                const Mode km = effectiveForced[i] ? EnglishMode : VietnameseMode;
                prefixEngine.processKey(effectiveKeys[i], km);
            }
            std::u32string prefixVie = prefixEngine.getProcessedU32(VietnameseMode);
            if (prefixVie == rawKeys_.substr(0, rawKeys_.size() - 1)) {
                if (outRestored) *outRestored = true;
                return rawKeys_;
            }
            std::u32string suppressedOut = prefixVie;
            suppressedOut.push_back(rawKeys_.back());
            if (vieOut != suppressedOut && !isExplicitEffectUndo(testEngine, rawKeys_, vieOut)) {
                if (outSuppressLastKey) *outSuppressLastKey = true;
                return suppressedOut;
            }
        }
    }

    if (outValidSingleSyllable) {
        auto [sylPrev, sylLast] = extractLastSyllable(testEngine.getComposition());
        (void)sylLast;
        *outValidSingleSyllable = (sylPrev.empty() && testEngine.isValid(isWordComplete));
    }
    return vieOut;
}

std::u32string SessionProcessor::postClickDisplayedTail() const {
    if (displayedWord_.size() >= preClickDisplayed_.size() &&
        displayedWord_.compare(0, preClickDisplayed_.size(), preClickDisplayed_) == 0) {
        return displayedWord_.substr(preClickDisplayed_.size());
    }
    return {};
}

KeyActionDiff SessionProcessor::replayPostClickPrintable(
    std::u32string_view newPostRaw, std::u32string_view oldPostDisp, char32_t lastCh) {
    const SessionStats savedStats = stats_;
    clearActiveWord();
    if (newPostRaw.size() >= 2) {
        for (size_t i = 0; i + 1 < newPostRaw.size(); ++i) {
            processPrintableKey(newPostRaw[i]);
        }
        stats_ = savedStats;
    }
    std::u32string preFinalDisp = displayedWord_;
    KeyActionDiff lastDiff = processPrintableKey(lastCh);
    stats_.totalKeysProcessed = savedStats.totalKeysProcessed;

    if (preFinalDisp == oldPostDisp) {
        return lastDiff;
    }
    if (displayedWord_.empty() && lastDiff.passThrough) {
        return lastDiff;
    }
    KeyActionDiff diff = computeDiff(oldPostDisp, displayedWord_, options_.outputCharset);
    if (diff.backspaces == 0 && diff.textToEmit.size() == 1 && diff.textToEmit[0] == lastCh &&
        displayedWord_.size() == oldPostDisp.size() + 1) {
        diff.passThrough = true;
    } else if (diff.backspaces > 0 || diff.textToEmit.size() > 1 ||
               (diff.textToEmit.size() == 1 && diff.textToEmit[0] != lastCh)) {
        diff.wasTransformed = true;
    }
    diff.wasRestored = lastDiff.wasRestored;
    diff.wasMacroExpanded = lastDiff.wasMacroExpanded;
    return diff;
}

bool SessionProcessor::dropPreClickIfActiveForCursorEdit() {
    if (!hasPostClickBoundary_) {
        return false;
    }
    if (postClickAddedRaw_.empty()) {
        clearActiveWord();
        return true;
    }
    std::u32string postRaw = postClickAddedRaw_;
    const SessionStats savedStats = stats_;
    clearActiveWord();
    for (char32_t k : postRaw) {
        processPrintableKey(k);
    }
    stats_ = savedStats;
    return false;
}

KeyActionDiff SessionProcessor::processPrintableKey(char32_t ch) {
    ++stats_.totalKeysProcessed;

    if (!options_.vietnameseEnabled) {
        reset();
        KeyActionDiff diff;
        diff.passThrough = true;
        return diff;
    }

    // If cursor is currently left of the end of the active word, split the right suffix off
    // into rightTokens_ before processing the new character at the cursor position.
    if (cursorInWord_ < displayedWord_.size()) {
        std::u32string rightFrag = displayedWord_.substr(cursorInWord_);
        std::u32string leftFrag = displayedWord_.substr(0, cursorInWord_);
        bool wasLocked = wordLockedRaw_;
        if (!rightFrag.empty()) {
            pushRightFront(makeTokenFromDisplayed(rightFrag, wasLocked));
        }
        loadDisplayedFragment(leftFrag, wasLocked);
    }

    char32_t lowerCh = toLowerChar(ch);
    bool isProcessable = engine_.canProcessKey(lowerCh);

    // If this key is a word-break symbol (Space, punctuation, or digit when not in VNI/Microsoft layout)
    if (!isProcessable) {
        if (hasPostClickBoundary_) {
            if (postClickAddedRaw_.empty()) {
                clearActiveWord();
                KeyActionDiff diff;
                diff.passThrough = true;
                return diff;
            }
            std::u32string oldPostDisp = postClickDisplayedTail();
            std::u32string newPostRaw = postClickAddedRaw_;
            newPostRaw.push_back(ch);
            return replayPostClickPrintable(newPostRaw, oldPostDisp, ch);
        }

        std::u32string finalWord = displayedWord_;
        bool restored = false;
        bool macroHit = false;

        if (!rawKeys_.empty()) {
            // 1. Check macro table first on the raw or displayed word
            if (options_.macroEnabled) {
                std::string candidateVie = u32ToUtf8(displayedWord_);
                std::string candidateRaw = u32ToUtf8(rawKeys_);
                auto exp = macroTable_.expand(candidateRaw);
                if (!exp.has_value()) {
                    exp = macroTable_.expand(candidateVie);
                }
                if (exp.has_value()) {
                    finalWord = utf8ToU32(*exp);
                    macroHit = true;
                    ++stats_.macrosExpandedCount;
                }
            }
            // 2. Otherwise check full-word completion validity (e.g., auto-restore "arch", "uow", etc.)
            if (!macroHit) {
                finalWord = evaluateBufferWithFeatures(true, &restored);
                if (restored && finalWord != displayedWord_) {
                    ++stats_.englishRestoresCount;
                }
            }
        }

        if (finalWord == displayedWord_) {
            // Preserve the committed word + delimiter in bounded history so backspacing or
            // moving back with Left Arrow lets the user keep editing tone marks!
            if (!rawKeys_.empty()) {
                CommittedToken wordTok;
                wordTok.isDelimiter = false;
                wordTok.rawKeys = rawKeys_;
                wordTok.displayedWord = displayedWord_;
                wordTok.keyForcedEnglish = keyForcedEnglish_;
                wordTok.wordLockedRaw = wordLockedRaw_;
                pushLeftToken(std::move(wordTok));
            }
            CommittedToken delimTok;
            delimTok.isDelimiter = true;
            delimTok.delimiter = ch;
            pushLeftToken(std::move(delimTok));

            clearActiveWord();
            KeyActionDiff diff;
            diff.passThrough = true;
            return diff;
        }

        // The preceding word changed on word-break (either Macro expanded or invalid Vietnamese auto-restored)
        CommittedToken wordTok;
        wordTok.isDelimiter = false;
        if (macroHit) {
            wordTok.rawKeys = finalWord;
            wordTok.displayedWord = finalWord;
            wordTok.keyForcedEnglish.assign(wordTok.rawKeys.size(), 0);
            wordTok.wordLockedRaw = true;
        } else {
            // Auto-restored to raw ASCII
            wordTok.rawKeys = rawKeys_;
            wordTok.displayedWord = finalWord;
            wordTok.keyForcedEnglish = keyForcedEnglish_;
            wordTok.wordLockedRaw = true;
            wordTok.restoredAtRawLen = rawKeys_.size();
            wordTok.peakRawLenSinceLock = rawKeys_.size();
        }
        pushLeftToken(std::move(wordTok));

        CommittedToken delimTok;
        delimTok.isDelimiter = true;
        delimTok.delimiter = ch;
        pushLeftToken(std::move(delimTok));

        std::u32string newScreen = finalWord;
        newScreen.push_back(ch);
        KeyActionDiff diff = computeDiff(displayedWord_, newScreen, options_.outputCharset);
        diff.wasTransformed = true;
        diff.wasMacroExpanded = macroHit;
        diff.wasRestored = restored;
        clearActiveWord();
        return diff;
    }

    // Processable key inside a word
    std::u32string oldDisplayed = displayedWord_;
    rawKeys_.push_back(ch);
    keyForcedEnglish_.push_back(0);

    bool restored = false;
    bool suppressLastKey = false;
    bool validSingleSyllable = false;
    std::u32string newDisplayed = evaluateBufferWithFeatures(false, &restored, &suppressLastKey, &validSingleSyllable);
    if (hasPostClickBoundary_ && (restored || suppressLastKey || !validSingleSyllable)) {
        std::u32string oldPostDisp =
            (oldDisplayed.size() >= preClickDisplayed_.size() &&
             oldDisplayed.compare(0, preClickDisplayed_.size(), preClickDisplayed_) == 0)
                ? oldDisplayed.substr(preClickDisplayed_.size())
                : std::u32string();
        std::u32string newPostRaw = postClickAddedRaw_;
        newPostRaw.push_back(ch);
        return replayPostClickPrintable(newPostRaw, oldPostDisp, ch);
    }
    if (suppressLastKey && !keyForcedEnglish_.empty()) {
        keyForcedEnglish_.back() = 1;
    }
    if (restored) {
        if (oldDisplayed != rawKeys_.substr(0, rawKeys_.size() - 1)) {
            ++stats_.englishRestoresCount;
        }
        // Track raw length at lock time so immediate Backspace recovery can re-enable Vietnamese
        // when user accidentally mistyped an extra key (e.g. "taasns" -> \b -> "tấn"), while long
        // English words ("windows", "search") stay locked 1-for-1 across Backspace.
        if (!wordLockedRaw_) {
            wordLockedRaw_ = true;
            restoredAtRawLen_ = rawKeys_.size();
            peakRawLenSinceLock_ = rawKeys_.size();
        } else if (rawKeys_.size() > peakRawLenSinceLock_) {
            peakRawLenSinceLock_ = rawKeys_.size();
        }
    }

    if (!wordLockedRaw_) {
        engine_.reset();
        for (size_t i = 0; i < rawKeys_.size(); ++i) {
            const Mode km = (i < keyForcedEnglish_.size() && keyForcedEnglish_[i])
                                ? EnglishMode
                                : VietnameseMode;
            engine_.processKey(rawKeys_[i], km);
        }
    }
    displayedWord_ = newDisplayed;
    cursorInWord_ = displayedWord_.size();

    KeyActionDiff diff = computeDiff(oldDisplayed, newDisplayed, options_.outputCharset);
    if (diff.backspaces > 0 || (diff.textToEmit.size() == 1 && diff.textToEmit[0] != ch) || diff.textToEmit.size() > 1) {
        diff.wasTransformed = true;
        ++stats_.vietnameseTransforms;
        if (diff.backspaces > 0) {
            ++stats_.backtracksCount;
        }
    }
    diff.wasRestored = restored;
    if (hasPostClickBoundary_) {
        const bool prefixPreserved =
            newDisplayed.size() >= preClickDisplayed_.size() &&
            newDisplayed.compare(0, preClickDisplayed_.size(), preClickDisplayed_) == 0;
        int postEncLen = 0;
        if (prefixPreserved && oldDisplayed.size() >= preClickDisplayed_.size()) {
            std::u32string oldPostDisp = oldDisplayed.substr(preClickDisplayed_.size());
            if (options_.outputCharset.empty() || options_.outputCharset == CHARSET_UNICODE) {
                postEncLen = static_cast<int>(oldPostDisp.size());
            } else {
                postEncLen = static_cast<int>(
                    utf8ToU32(encodeCharset(options_.outputCharset, u32ToUtf8(oldPostDisp))).size());
            }
        }
        if (prefixPreserved && diff.backspaces <= postEncLen) {
            postClickAddedRaw_.push_back(ch);
        } else {
            clearPostClickBoundary();
        }
    }
    return diff;
}

KeyActionDiff SessionProcessor::processBackspace() {
    if (!options_.vietnameseEnabled) {
        reset();
        KeyActionDiff diff;
        diff.passThrough = true;
        return diff;
    }

    if (hasPostClickBoundary_) {
        std::u32string oldPostDisp = postClickDisplayedTail();
        if (dropPreClickIfActiveForCursorEdit()) {
            KeyActionDiff diff;
            diff.passThrough = true;
            return diff;
        }
        std::u32string preBkspDisp = displayedWord_;
        KeyActionDiff diff = processBackspace();
        if (preBkspDisp != oldPostDisp && !diff.passThrough) {
            diff = computeDiff(oldPostDisp, displayedWord_, options_.outputCharset);
            diff.wasTransformed = true;
        }
        return diff;
    }

    // Mid-word Backspace when cursor is inside active word: delete the displayed codepoint
    // left of cursor, push suffix after cursor into rightTokens_, and keep editing left prefix!
    if (!displayedWord_.empty() && cursorInWord_ > 0 && cursorInWord_ < displayedWord_.size()) {
        std::u32string rightFrag = displayedWord_.substr(cursorInWord_);
        std::u32string newLeft = displayedWord_.substr(0, cursorInWord_ - 1);
        bool wasLocked = wordLockedRaw_;
        if (!rightFrag.empty()) {
            pushRightFront(makeTokenFromDisplayed(rightFrag, wasLocked));
        }
        loadDisplayedFragment(newLeft, wasLocked);
        KeyActionDiff diff;
        diff.passThrough = true;
        return diff;
    }

    // Cursor sits right at start of a word (e.g. "toán |học"): move active word to rightTokens_
    // so Backspace deletes the delimiter/prior text to the left.
    if (!displayedWord_.empty() && cursorInWord_ == 0) {
        CommittedToken rightTok;
        rightTok.isDelimiter = false;
        rightTok.rawKeys = rawKeys_;
        rightTok.displayedWord = displayedWord_;
        rightTok.keyForcedEnglish = keyForcedEnglish_;
        rightTok.wordLockedRaw = wordLockedRaw_;
        rightTok.restoredAtRawLen = restoredAtRawLen_;
        rightTok.peakRawLenSinceLock = peakRawLenSinceLock_;
        pushRightFront(std::move(rightTok));
        clearActiveWord();
    }

    if (rawKeys_.empty() || displayedWord_.empty()) {
        clearActiveWord();
        if (leftTokens_.empty()) {
            KeyActionDiff diff;
            diff.passThrough = true;
            return diff;
        }

        if (leftTokens_.back().isDelimiter) {
            leftTokens_.pop_back();
            // If immediately preceded by a committed word, bring that word back into the active
            // buffer right after Backspace deletes the delimiter on screen.
            if (!leftTokens_.empty() && !leftTokens_.back().isDelimiter) {
                CommittedToken prev = std::move(leftTokens_.back());
                leftTokens_.pop_back();
                size_t len = prev.displayedWord.size();
                restoreTokenToActive(std::move(prev), len);
            }
            KeyActionDiff diff;
            diff.passThrough = true;
            return diff;
        }

        // Fallback if top of leftTokens is directly a word token: restore it and remove its last char
        CommittedToken prev = std::move(leftTokens_.back());
        leftTokens_.pop_back();
        size_t len = prev.displayedWord.size();
        restoreTokenToActive(std::move(prev), len);
        if (rawKeys_.empty() || displayedWord_.empty()) {
            clearActiveWord();
            KeyActionDiff diff;
            diff.passThrough = true;
            return diff;
        }
    }

    std::u32string oldDisplayed = displayedWord_;
    std::u32string newDisplayed;

    if (wordLockedRaw_) {
        rawKeys_.pop_back();
        if (keyForcedEnglish_.size() > rawKeys_.size()) {
            keyForcedEnglish_.resize(rawKeys_.size(), 0);
        }
        newDisplayed = rawKeys_;
        if (!rawKeys_.empty() && options_.spellCheckEnabled && restoredAtRawLen_ > 0) {
            BambooEngine candEng(engine_.getInputMethod(), engine_.getFlag());
            for (char32_t k : rawKeys_) {
                candEng.processKey(k, VietnameseMode);
            }
            std::u32string candVie = candEng.getProcessedU32(VietnameseMode);
            auto [candPrevSyl, candLastSyl] = extractLastSyllable(candEng.getComposition());
            const bool canUnlockPlainPrefix =
                candPrevSyl.empty() && candEng.isValid(false) && candVie == rawKeys_;
            bool hasToneOrD = false;
            for (char32_t c : candVie) {
                char32_t lo = toLowerChar(c);
                if (lo == U'đ' || findToneFromChar(lo) != Tone::None) {
                    hasToneOrD = true;
                    break;
                }
            }
            const bool withinShortTypoWindow = (peakRawLenSinceLock_ <= restoredAtRawLen_ + 1);
            const bool canUnlockMistypedViWord =
                options_.autoRestoreEnglish &&
                candPrevSyl.empty() && candEng.isValid(true) &&
                candVie != rawKeys_ &&
                (withinShortTypoWindow || (rawKeys_.size() >= 4 && hasToneOrD));
            if (canUnlockPlainPrefix || canUnlockMistypedViWord) {
                wordLockedRaw_ = false;
                restoredAtRawLen_ = 0;
                peakRawLenSinceLock_ = 0;
                keyForcedEnglish_.assign(rawKeys_.size(), 0);
                engine_.reset();
                for (char32_t k : rawKeys_) {
                    engine_.processKey(k, VietnameseMode);
                }
                newDisplayed = evaluateBufferWithFeatures(false);
            }
        }
    } else {
        // Use BambooEngine's removeLastChar to remove the last base character and its attached marks/tones
        engine_.removeLastChar(true);
        std::u32string newEngineRaw = engine_.getProcessedU32(EnglishMode);
        rawKeys_ = newEngineRaw;
        if (keyForcedEnglish_.size() > rawKeys_.size()) {
            keyForcedEnglish_.resize(rawKeys_.size(), 0);
        } else if (keyForcedEnglish_.size() < rawKeys_.size()) {
            keyForcedEnglish_.resize(rawKeys_.size(), 0);
        }
        if (!rawKeys_.empty()) {
            newDisplayed = evaluateBufferWithFeatures(false);
        } else {
            newDisplayed.clear();
        }
    }

    if (rawKeys_.empty() || newDisplayed.empty()) {
        clearActiveWord();
    } else {
        displayedWord_ = newDisplayed;
        cursorInWord_ = displayedWord_.size();
    }

    // If deleting the last character simply shortens displayedWord_ by 1 codepoint in Unicode mode,
    // we can just let a single Backspace pass through!
    if ((options_.outputCharset.empty() || options_.outputCharset == CHARSET_UNICODE) &&
        oldDisplayed.size() == newDisplayed.size() + 1 &&
        oldDisplayed.substr(0, newDisplayed.size()) == newDisplayed) {
        KeyActionDiff diff;
        diff.passThrough = true;
        return diff;
    }

    KeyActionDiff diff = computeDiff(oldDisplayed, newDisplayed, options_.outputCharset);
    diff.wasTransformed = true;
    return diff;
}

void SessionProcessor::moveCursorLeft() {
    if (!options_.vietnameseEnabled) {
        reset();
        return;
    }

    if (dropPreClickIfActiveForCursorEdit()) {
        return;
    }

    if (cursorInWord_ > 0) {
        --cursorInWord_;
        return;
    }

    if (!displayedWord_.empty()) {
        CommittedToken rightTok;
        rightTok.isDelimiter = false;
        rightTok.rawKeys = rawKeys_;
        rightTok.displayedWord = displayedWord_;
        rightTok.keyForcedEnglish = keyForcedEnglish_;
        rightTok.wordLockedRaw = wordLockedRaw_;
        rightTok.restoredAtRawLen = restoredAtRawLen_;
        rightTok.peakRawLenSinceLock = peakRawLenSinceLock_;
        pushRightFront(std::move(rightTok));
        clearActiveWord();
    }

    if (leftTokens_.empty()) {
        // Cursor stepped before our tracked buffer into untracked text: reset cleanly
        reset();
        return;
    }

    CommittedToken top = std::move(leftTokens_.back());
    leftTokens_.pop_back();
    if (top.isDelimiter) {
        pushRightFront(std::move(top));
        if (!leftTokens_.empty() && !leftTokens_.back().isDelimiter) {
            CommittedToken prevWord = std::move(leftTokens_.back());
            leftTokens_.pop_back();
            size_t len = prevWord.displayedWord.size();
            restoreTokenToActive(std::move(prevWord), len);
        }
    } else {
        size_t len = top.displayedWord.size();
        size_t pos = (len > 0) ? (len - 1) : 0;
        restoreTokenToActive(std::move(top), pos);
    }
}

void SessionProcessor::moveCursorRight() {
    if (!options_.vietnameseEnabled) {
        reset();
        return;
    }

    if (dropPreClickIfActiveForCursorEdit()) {
        return;
    }

    if (cursorInWord_ < displayedWord_.size()) {
        ++cursorInWord_;
        return;
    }

    if (rightTokens_.empty()) {
        // Cursor stepped past tracked text to the right: reset cleanly
        reset();
        return;
    }

    if (!displayedWord_.empty()) {
        CommittedToken leftTok;
        leftTok.isDelimiter = false;
        leftTok.rawKeys = rawKeys_;
        leftTok.displayedWord = displayedWord_;
        leftTok.keyForcedEnglish = keyForcedEnglish_;
        leftTok.wordLockedRaw = wordLockedRaw_;
        leftTok.restoredAtRawLen = restoredAtRawLen_;
        leftTok.peakRawLenSinceLock = peakRawLenSinceLock_;
        pushLeftToken(std::move(leftTok));
        clearActiveWord();
    }

    CommittedToken front = std::move(rightTokens_.front());
    rightTokens_.pop_front();
    if (front.isDelimiter) {
        pushLeftToken(std::move(front));
        if (!rightTokens_.empty() && !rightTokens_.front().isDelimiter) {
            CommittedToken nextWord = std::move(rightTokens_.front());
            rightTokens_.pop_front();
            restoreTokenToActive(std::move(nextWord), 0);
        }
    } else {
        size_t pos = front.displayedWord.empty() ? 0 : 1;
        restoreTokenToActive(std::move(front), pos);
    }
}

void SessionProcessor::processDelete() {
    if (!options_.vietnameseEnabled) {
        reset();
        return;
    }

    if (dropPreClickIfActiveForCursorEdit()) {
        return;
    }

    if (cursorInWord_ < displayedWord_.size()) {
        std::u32string leftFrag = displayedWord_.substr(0, cursorInWord_);
        std::u32string rightFrag = displayedWord_.substr(cursorInWord_ + 1);
        bool wasLocked = wordLockedRaw_;
        if (!rightFrag.empty()) {
            pushRightFront(makeTokenFromDisplayed(rightFrag, wasLocked));
        }
        loadDisplayedFragment(leftFrag, wasLocked);
        return;
    }

    if (rightTokens_.empty()) {
        // Deleting untracked content to the right
        reset();
        return;
    }

    CommittedToken& front = rightTokens_.front();
    if (front.isDelimiter) {
        rightTokens_.pop_front();
    } else {
        if (!front.displayedWord.empty()) {
            std::u32string trimmed = front.displayedWord.substr(1);
            if (trimmed.empty()) {
                rightTokens_.pop_front();
            } else {
                front = makeTokenFromDisplayed(trimmed, front.wordLockedRaw);
            }
        } else {
            rightTokens_.pop_front();
        }
    }
}

KeyActionDiff SessionProcessor::restoreCurrentWord() {
    if (dropPreClickIfActiveForCursorEdit()) {
        return KeyActionDiff{};
    }
    if (cursorInWord_ < displayedWord_.size()) {
        std::u32string rightFrag = displayedWord_.substr(cursorInWord_);
        std::u32string leftFrag = displayedWord_.substr(0, cursorInWord_);
        bool wasLocked = wordLockedRaw_;
        if (!rightFrag.empty()) {
            pushRightFront(makeTokenFromDisplayed(rightFrag, wasLocked));
        }
        loadDisplayedFragment(leftFrag, wasLocked);
    }
    if (rawKeys_.empty() || displayedWord_ == rawKeys_) {
        return KeyActionDiff{};
    }
    std::u32string oldDisplayed = displayedWord_;
    wordLockedRaw_ = true;
    displayedWord_ = rawKeys_;
    cursorInWord_ = displayedWord_.size();
    KeyActionDiff diff = computeDiff(oldDisplayed, displayedWord_, options_.outputCharset);
    diff.wasTransformed = true;
    diff.wasRestored = true;
    ++stats_.englishRestoresCount;
    return diff;
}

} // namespace vnsnowkey::bamboo
