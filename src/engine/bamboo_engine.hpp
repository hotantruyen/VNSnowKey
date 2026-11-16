/*
 * VNSnowKey - Vietnamese Input Method for Arch Linux + KDE Plasma (Wayland)
 * Engine derived from BambooEngine (bamboo-core)
 * Copyright (C) 2018 Luong Thanh Lam <ltlam93@gmail.com> (MIT License)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 */

#pragma once

#include "bamboo_types.hpp"
#include <string>
#include <string_view>
#include <utility>

namespace vnsnowkey::bamboo {

// Core transformation helper functions exposed for unit testing parity with bamboo-core
TransPtr findLastAppendingTrans(const TransList& composition);
TransPtr newAppendingTrans(char32_t key, bool isUpperCase);
bool isValidComposition(const TransList& composition, bool inputIsFullComplete);
std::pair<TransList, TransList> extractLastWord(const TransList& composition, const std::vector<char32_t>& effectKeys);
std::pair<TransList, TransList> extractLastSyllable(const TransList& composition);

class BambooEngine {
public:
    BambooEngine(InputMethod inputMethod, uint32_t flags = EstdFlags);

    void setInputMethod(InputMethod inputMethod);
    const InputMethod& getInputMethod() const { return inputMethod_; }

    void setFlag(uint32_t flag) { flags_ = flag; }
    uint32_t getFlag() const { return flags_; }

    bool isValid(bool inputIsFullComplete) const;
    std::string getProcessedString(Mode mode) const;
    std::u32string getProcessedU32(Mode mode) const;
    bool canProcessKey(char32_t key) const;

    void processKey(char32_t key, Mode mode);
    void processString(std::string_view utf8Str, Mode mode);
    void processU32String(std::u32string_view u32Str, Mode mode);

    void restoreLastWord(bool toVietnamese);
    void removeLastChar(bool refreshLastTone);
    void reset();

    const TransList& getComposition() const { return composition_; }

private:
    std::vector<Rule> getApplicableRules(char32_t key) const;
    std::pair<TransPtr, Rule> findTargetByKey(const TransList& composition, char32_t key) const;
    TransList generateTransformations(const TransList& composition, char32_t lowerKey, bool isUpperCase) const;
    TransList newComposition(const TransList& composition, char32_t key, bool isUpperCase) const;
    TransPtr applyUowShortcut(const TransList& syllable) const;
    TransList refreshLastToneTarget(const TransList& syllable) const;

    TransList composition_;
    InputMethod inputMethod_;
    uint32_t flags_ = EstdFlags;
};

} // namespace vnsnowkey::bamboo
