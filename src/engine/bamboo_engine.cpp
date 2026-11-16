/*
 * VNSnowKey - Vietnamese Input Method for Arch Linux + KDE Plasma (Wayland)
 * Engine derived from BambooEngine (bamboo-core)
 * Copyright (C) 2018 Luong Thanh Lam <ltlam93@gmail.com> (MIT License)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 */

#include "bamboo_engine.hpp"
#include "bamboo_utils.hpp"
#include "flattener.hpp"
#include "spelling.hpp"
#include <tuple>
#include <unordered_map>

namespace vnsnowkey::bamboo {

namespace {

TransPtr generateAppendingTrans(const std::vector<Rule>& rules, char32_t lowerKey, bool isUpperCase) {
    for (Rule rule : rules) {
        if (rule.key == lowerKey && rule.effectType == EffectType::Appending) {
            bool isUpper = isUpperCase || isUpperChar(rule.effectOn);
            rule.effectOn = toLowerChar(rule.effectOn);
            rule.result = rule.effectOn;
            auto t = std::make_shared<Transformation>();
            t->isUpperCase = isUpper;
            t->rule = std::move(rule);
            return t;
        }
    }
    return newAppendingTrans(lowerKey, isUpperCase);
}

TransList filterAppendingComposition(const TransList& composition) {
    TransList appendingTransformations;
    for (const auto& trans : composition) {
        if (trans->rule.effectType == EffectType::Appending) {
            appendingTransformations.push_back(trans);
        }
    }
    return appendingTransformations;
}

TransPtr findRootTarget(const TransPtr& target) {
    if (!target || target->target == nullptr) {
        return target;
    }
    return findRootTarget(target->target);
}

std::pair<TransList, TransList> extractAtomicTrans(const TransList& composition, const TransList& last, bool lastIsVowel) {
    if (composition.empty()) {
        return {composition, last};
    }
    const auto& tmp = composition.back();
    if (tmp != nullptr && tmp->target == nullptr && lastIsVowel != isVowel(tmp->rule.result)) {
        return {composition, last};
    }
    TransList prefix(composition.begin(), composition.end() - 1);
    TransList newLast;
    newLast.reserve(last.size() + 1);
    newLast.push_back(tmp);
    newLast.insert(newLast.end(), last.begin(), last.end());
    return extractAtomicTrans(prefix, newLast, lastIsVowel);
}

std::tuple<TransList, TransList, TransList> extractCvcAppendingTrans(const TransList& composition) {
    auto [head, lastConsonant] = extractAtomicTrans(composition, {}, false);
    auto [firstConsonant, vowel] = extractAtomicTrans(head, {}, true);
    if (!lastConsonant.empty() && vowel.empty() && firstConsonant.empty()) {
        firstConsonant = std::move(lastConsonant);
        vowel.clear();
        lastConsonant.clear();
    }

    // 'gi' and 'qu' are considered qualified consonants:
    // ['g', 'ia', ''] -> ['gi', 'a', '']
    // ['q', 'ua', ''] -> ['qu', 'a', '']
    // except: ['g', 'ie', 'ng'] -> ['g', 'ie', 'ng']
    if (firstConsonant.size() == 1 && !vowel.empty() &&
        ((firstConsonant[0]->rule.result == U'g' && vowel[0]->rule.result == U'i' && vowel.size() > 1 &&
          !(vowel[1]->rule.result == U'e' && !lastConsonant.empty())) ||
         (firstConsonant[0]->rule.result == U'q' && vowel[0]->rule.result == U'u'))) {
        firstConsonant.push_back(vowel[0]);
        vowel.erase(vowel.begin());
    }
    return {firstConsonant, vowel, lastConsonant};
}

std::tuple<TransList, TransList, TransList> extractCvcTrans(const TransList& composition) {
    std::unordered_map<TransPtr, TransList> transMap;
    TransList appendingList;
    for (const auto& trans : composition) {
        if (trans->target == nullptr) {
            appendingList.push_back(trans);
        } else {
            transMap[trans->target].push_back(trans);
        }
    }
    auto [fc, vo, lc] = extractCvcAppendingTrans(appendingList);
    TransList fcBase = fc;
    for (const auto& t : fcBase) {
        auto it = transMap.find(t);
        if (it != transMap.end()) {
            fc.insert(fc.end(), it->second.begin(), it->second.end());
        }
    }
    TransList voBase = vo;
    for (const auto& t : voBase) {
        auto it = transMap.find(t);
        if (it != transMap.end()) {
            vo.insert(vo.end(), it->second.begin(), it->second.end());
        }
    }
    TransList lcBase = lc;
    for (const auto& t : lcBase) {
        auto it = transMap.find(t);
        if (it != transMap.end()) {
            lc.insert(lc.end(), it->second.begin(), it->second.end());
        }
    }
    return {fc, vo, lc};
}

bool hasValidTone(const TransList& composition, Tone tone) {
    if (tone == Tone::None || tone == Tone::Acute || tone == Tone::Dot) {
        return true;
    }
    auto [fc, vo, lc] = extractCvcTrans(composition);
    if (lc.empty()) {
        return true;
    }
    std::u32string lastConsonants = flattenU32(lc, EnglishMode | LowerCase);
    static const std::vector<std::u32string> kDotWithConsonants = {
        U"c", U"k", U"p", U"t", U"ch"
    };
    for (const auto& s : kDotWithConsonants) {
        if (s == lastConsonants) {
            return false;
        }
    }
    return true;
}

TransList getRightMostVowels(const TransList& composition) {
    auto [fc, vo, lc] = extractCvcTrans(composition);
    return vo;
}

TransPtr findToneTarget(const TransList& composition, bool stdStyle) {
    if (composition.empty()) {
        return nullptr;
    }
    TransPtr target = nullptr;
    auto [fc, vo, lc] = extractCvcTrans(composition);
    TransList vowels = filterAppendingComposition(vo);
    if (vowels.size() == 1) {
        target = vowels[0];
    } else if (vowels.size() == 2) {
        for (const auto& trans : vo) {
            if (trans->rule.result == U'ơ' || trans->rule.result == U'ê') {
                if (trans->target != nullptr) {
                    target = trans->target;
                } else {
                    target = trans;
                }
            }
        }
        if (target == nullptr) {
            if (!lc.empty()) {
                target = vowels[1];
            } else if (!stdStyle) {
                std::u32string str = flattenU32(vowels, EnglishMode | LowerCase | ToneLess | MarkLess);
                if (str == U"oa" || str == U"oe" || str == U"uy" || str == U"ue" || str == U"uo") {
                    target = vowels[1];
                } else {
                    target = vowels[0];
                }
            } else {
                target = vowels[0];
            }
        }
    } else if (vowels.size() == 3) {
        if (flattenU32(vowels, EnglishMode | LowerCase | ToneLess | MarkLess) == U"uye") {
            target = vowels[2];
        } else {
            target = vowels[1];
        }
    }
    return target;
}

TransPtr getLastToneTransformation(const TransList& composition) {
    for (int i = static_cast<int>(composition.size()) - 1; i >= 0; --i) {
        const auto& t = composition[static_cast<size_t>(i)];
        if (t->rule.effectType == EffectType::ToneTransformation && t->target != nullptr) {
            return t;
        }
    }
    return nullptr;
}

bool isFree(const TransList& composition, const TransPtr& trans, EffectType effectType) {
    for (const auto& t : composition) {
        if (t->target == trans && t->rule.effectType == effectType) {
            return false;
        }
    }
    return true;
}

std::pair<TransList, TransList> extractLastWordWithPunctuationMarks(
    const TransList& composition, const std::vector<char32_t>& /*effectKeys*/) {
    for (int i = static_cast<int>(composition.size()) - 1; i >= 0; --i) {
        TransList tail(composition.begin() + i, composition.end());
        std::u32string canvas = getCanvas(tail, EnglishMode);
        if (canvas.empty()) {
            continue;
        }
        char32_t c = canvas[0];
        if (isSpace(c)) {
            if (static_cast<size_t>(i) == composition.size() - 1) {
                return {composition, {}};
            }
            return {
                TransList(composition.begin(), composition.begin() + i + 1),
                TransList(composition.begin() + i + 1, composition.end())
            };
        }
    }
    return {{}, composition};
}

std::pair<TransPtr, Rule> findMarkTarget(const TransList& composition, const std::vector<Rule>& rules) {
    std::u32string str = flattenU32(composition, VietnameseMode);
    for (int i = static_cast<int>(composition.size()) - 1; i >= 0; --i) {
        const auto& trans = composition[static_cast<size_t>(i)];
        for (const auto& rule : rules) {
            if (rule.effectType != EffectType::MarkTransformation) {
                continue;
            }
            if (trans->rule.result == rule.effectOn && rule.effect > 0) {
                TransPtr target = findRootTarget(trans);
                auto testTrans = std::make_shared<Transformation>();
                testTrans->target = target;
                testTrans->rule = rule;
                TransList tmp = composition;
                tmp.push_back(testTrans);
                if (str == flattenU32(tmp, VietnameseMode)) {
                    continue;
                }
                if (isValidComposition(tmp, false)) {
                    return {target, rule};
                }
            }
        }
    }
    return {nullptr, Rule{}};
}

std::pair<TransPtr, Rule> findTarget(const TransList& composition, const std::vector<Rule>& applicableRules, uint32_t flags) {
    std::u32string str = flattenU32(composition, VietnameseMode);
    for (const auto& applicableRule : applicableRules) {
        if (applicableRule.effectType != EffectType::ToneTransformation) {
            continue;
        }
        TransPtr target = nullptr;
        if ((flags & EfreeToneMarking) != 0) {
            if (hasValidTone(composition, static_cast<Tone>(applicableRule.effect))) {
                target = findToneTarget(composition, (flags & EstdToneStyle) != 0);
            }
        } else {
            TransPtr lastAppending = findLastAppendingTrans(composition);
            if (lastAppending != nullptr && isVowel(lastAppending->rule.effectOn)) {
                target = lastAppending;
            }
        }
        auto testTrans = std::make_shared<Transformation>();
        testTrans->target = target;
        testTrans->rule = applicableRule;
        TransList tmp = composition;
        tmp.push_back(testTrans);
        if (str == flattenU32(tmp, VietnameseMode)) {
            continue;
        }
        if (target != nullptr &&
            static_cast<Tone>(applicableRule.effect) == Tone::None &&
            isFree(composition, target, EffectType::ToneTransformation) &&
            findToneFromChar(target->rule.result) == Tone::None) {
            target = nullptr;
        }
        return {target, applicableRule};
    }
    return findMarkTarget(composition, applicableRules);
}

TransList generateUndoTransformations(const TransList& composition, const std::vector<Rule>& rules, uint32_t flags) {
    TransList transformations;
    std::u32string str = flattenU32(composition, VietnameseMode | ToneLess | LowerCase);
    for (const auto& rule : rules) {
        if (rule.effectType == EffectType::ToneTransformation) {
            TransPtr target = nullptr;
            if ((flags & EfreeToneMarking) != 0) {
                if (hasValidTone(composition, static_cast<Tone>(rule.effect))) {
                    target = findToneTarget(composition, (flags & EstdToneStyle) != 0);
                }
            } else {
                TransPtr lastAppending = findLastAppendingTrans(composition);
                if (lastAppending != nullptr && isVowel(lastAppending->rule.effectOn)) {
                    target = lastAppending;
                }
            }
            if (target == nullptr) {
                continue;
            }
            auto trans = std::make_shared<Transformation>();
            trans->target = target;
            trans->rule.effectType = EffectType::ToneTransformation;
            trans->rule.effect = 0;
            trans->rule.key = 0;
            transformations.push_back(trans);
        } else if (rule.effectType == EffectType::MarkTransformation) {
            for (int i = static_cast<int>(composition.size()) - 1; i >= 0; --i) {
                const auto& existing = composition[static_cast<size_t>(i)];
                if (existing->rule.result == rule.effectOn) {
                    TransPtr target = findRootTarget(existing);
                    auto trans = std::make_shared<Transformation>();
                    trans->target = target;
                    trans->rule.key = 0;
                    trans->rule.effectType = EffectType::MarkTransformation;
                    trans->rule.effect = 0;
                    TransList tmp = composition;
                    tmp.push_back(trans);
                    if (str == flattenU32(tmp, VietnameseMode | ToneLess | LowerCase)) {
                        continue;
                    }
                    transformations.push_back(trans);
                }
            }
        }
    }
    return transformations;
}

// Matches `(uơ|ưo)\p{L}+`
bool matchesRegUOhTail(std::u32string_view str) {
    if (str.size() < 3) return false;
    for (size_t i = 0; i + 2 < str.size(); ++i) {
        bool pairMatch = (str[i] == U'u' && str[i + 1] == U'ơ') ||
                         (str[i] == U'ư' && str[i + 1] == U'o');
        if (pairMatch && isUnicodeLetter(str[i + 2])) {
            return true;
        }
    }
    return false;
}

// Matches `(ưo|ươ)`
bool matchesRegUhO(std::u32string_view str) {
    if (str.size() < 2) return false;
    for (size_t i = 0; i + 1 < str.size(); ++i) {
        if (str[i] == U'ư' && (str[i + 1] == U'o' || str[i + 1] == U'ơ')) {
            return true;
        }
    }
    return false;
}

TransList generateTransformationsImpl(
    const TransList& composition,
    const std::vector<Rule>& applicableRules,
    uint32_t flags,
    char32_t lowerKey,
    bool isUpperCase) {
    TransList transformations;
    // Double typing an effect key undoes it and its effects, e.g. w + w -> w (Telex 2)
    if (!composition.empty()) {
        const Rule& rule = composition.back()->rule;
        if (rule.effectType == EffectType::Appending && rule.key == lowerKey && rule.key != rule.result) {
            auto t = std::make_shared<Transformation>();
            t->rule.effectType = EffectType::MarkTransformation;
            t->rule.effect = static_cast<uint8_t>(Mark::Raw);
            t->rule.key = 0;
            t->target = composition.back();
            transformations.push_back(t);
            return transformations;
        }
    }

    auto [target, applicableRule] = findTarget(composition, applicableRules, flags);
    if (target != nullptr) {
        auto t = std::make_shared<Transformation>();
        t->rule = applicableRule;
        t->target = target;
        t->isUpperCase = isUpperCase;
        transformations.push_back(t);
        if (applicableRule.effectType != EffectType::MarkTransformation) {
            return transformations;
        }
        TransList newComp = composition;
        newComp.insert(newComp.end(), transformations.begin(), transformations.end());
        if (isValidComposition(newComp, true)) {
            return transformations;
        }
        // Implement the uow typing shortcut by creating a virtual Mark_HORN rule that targets 'u' or 'o'.
        auto [vTarget, virtualRule] = findTarget(newComp, applicableRules, flags);
        if (vTarget != nullptr) {
            virtualRule.key = 0;
            auto vt = std::make_shared<Transformation>();
            vt->rule = virtualRule;
            vt->target = vTarget;
            vt->isUpperCase = false;
            transformations.push_back(vt);
            return transformations;
        }
    } else {
        // Implement ươ/ưo(i/c/ng) + o -> uô
        if (matchesRegUhO(flattenU32(composition, VietnameseMode | ToneLess | LowerCase))) {
            TransList vowels = filterAppendingComposition(getRightMostVowels(composition));
            if (!vowels.empty()) {
                auto trans = std::make_shared<Transformation>();
                trans->target = vowels[0];
                trans->rule.effectType = EffectType::MarkTransformation;
                trans->rule.key = 0;
                trans->rule.effect = static_cast<uint8_t>(Mark::None);

                TransList testComp = composition;
                testComp.push_back(trans);
                auto [fTarget, fRule] = findTarget(testComp, applicableRules, flags);
                if (fTarget != nullptr && fTarget != vowels[0]) {
                    transformations.push_back(trans);
                    auto t2 = std::make_shared<Transformation>();
                    t2->rule = fRule;
                    t2->target = fTarget;
                    t2->isUpperCase = isUpperCase;
                    transformations.push_back(t2);
                    return transformations;
                }
            }
        }
        TransList undoTrans = generateUndoTransformations(composition, applicableRules, flags);
        if (!undoTrans.empty()) {
            transformations.insert(transformations.end(), undoTrans.begin(), undoTrans.end());
            transformations.push_back(newAppendingTrans(lowerKey, isUpperCase));
        }
    }
    return transformations;
}

TransList generateFallbackTransformations(
    const TransList& /*composition*/,
    const std::vector<Rule>& applicableRules,
    char32_t lowerKey,
    bool isUpperCase) {
    TransList transformations;
    TransPtr trans = generateAppendingTrans(applicableRules, lowerKey, isUpperCase);
    transformations.push_back(trans);
    for (Rule appendedRule : trans->rule.appendedRules) {
        bool isUpper = isUpperCase || isUpperChar(appendedRule.effectOn);
        appendedRule.key = 0; // virtual key
        appendedRule.effectOn = toLowerChar(appendedRule.effectOn);
        appendedRule.result = appendedRule.effectOn;
        auto at = std::make_shared<Transformation>();
        at->rule = std::move(appendedRule);
        at->isUpperCase = isUpper;
        transformations.push_back(at);
    }
    return transformations;
}

TransList breakComposition(const TransList& composition) {
    TransList result;
    for (const auto& trans : composition) {
        if (trans->rule.key == 0) {
            continue;
        }
        result.push_back(newAppendingTrans(trans->rule.key, trans->isUpperCase));
    }
    return result;
}

TransList refreshLastToneTargetImpl(const TransList& composition, bool stdStyle) {
    TransList transformations;
    TransList rightmostVowels = getRightMostVowels(composition);
    TransPtr lastToneTrans = getLastToneTransformation(composition);
    if (rightmostVowels.empty() || lastToneTrans == nullptr) {
        return {};
    }
    TransPtr newToneTarget = findToneTarget(composition, stdStyle);
    if (lastToneTrans->target != newToneTarget) {
        lastToneTrans->target = newToneTarget;
        auto clearTone = std::make_shared<Transformation>();
        clearTone->target = lastToneTrans->target;
        clearTone->rule.key = 0;
        clearTone->rule.effectType = EffectType::ToneTransformation;
        clearTone->rule.effect = static_cast<uint8_t>(Tone::None);
        transformations.push_back(clearTone);

        Rule overrideRule = lastToneTrans->rule;
        overrideRule.key = 0;
        auto setTone = std::make_shared<Transformation>();
        setTone->target = newToneTarget;
        setTone->rule = overrideRule;
        transformations.push_back(setTone);
    }
    return transformations;
}

} // namespace

TransPtr findLastAppendingTrans(const TransList& composition) {
    for (int i = static_cast<int>(composition.size()) - 1; i >= 0; --i) {
        const auto& trans = composition[static_cast<size_t>(i)];
        if (trans->rule.effectType == EffectType::Appending) {
            return trans;
        }
    }
    return nullptr;
}

TransPtr newAppendingTrans(char32_t key, bool isUpperCase) {
    auto t = std::make_shared<Transformation>();
    t->isUpperCase = isUpperCase;
    t->rule.key = key;
    t->rule.effectOn = key;
    t->rule.effectType = EffectType::Appending;
    t->rule.result = key;
    return t;
}

bool isValidComposition(const TransList& composition, bool inputIsFullComplete) {
    if (composition.size() <= 1) {
        return true;
    }
    for (int i = static_cast<int>(composition.size()) - 1; i >= 0; --i) {
        if (composition[static_cast<size_t>(i)]->rule.effectType == EffectType::ToneTransformation) {
            Tone lastTone = static_cast<Tone>(composition[static_cast<size_t>(i)]->rule.effect);
            if (!hasValidTone(composition, lastTone)) {
                return false;
            }
            break;
        }
    }
    auto [fc, vo, lc] = extractCvcTrans(composition);
    Mode flattenMode = VietnameseMode | LowerCase | ToneLess;
    return isValidCVC(
        flattenU32(fc, flattenMode),
        flattenU32(vo, flattenMode),
        flattenU32(lc, flattenMode),
        inputIsFullComplete);
}

std::pair<TransList, TransList> extractLastWord(
    const TransList& composition, const std::vector<char32_t>& effectKeys) {
    for (int i = static_cast<int>(composition.size()) - 1; i >= 0; --i) {
        TransList tail(composition.begin() + i, composition.end());
        std::u32string canvas = getCanvas(tail, VietnameseMode | LowerCase | ToneLess | MarkLess);
        if (canvas.empty()) {
            continue;
        }
        char32_t c = canvas[0];
        if (!isAlphaChar(c) && !inKeyList(effectKeys, c)) {
            if (static_cast<size_t>(i) == composition.size() - 1) {
                return {composition, {}};
            }
            return {
                TransList(composition.begin(), composition.begin() + i + 1),
                TransList(composition.begin() + i + 1, composition.end())
            };
        }
    }
    return {{}, composition};
}

std::pair<TransList, TransList> extractLastSyllable(const TransList& composition) {
    auto [previous, last] = extractLastWord(composition, {});
    size_t anchor = 0;
    for (size_t i = 0; i < last.size(); ++i) {
        TransList slice(last.begin() + static_cast<std::ptrdiff_t>(anchor),
                        last.begin() + static_cast<std::ptrdiff_t>(i + 1));
        if (!isValidComposition(slice, false)) {
            anchor = i;
        }
    }
    if (anchor > 0) {
        previous.insert(previous.end(), last.begin(), last.begin() + static_cast<std::ptrdiff_t>(anchor));
    }
    return {previous, TransList(last.begin() + static_cast<std::ptrdiff_t>(anchor), last.end())};
}

BambooEngine::BambooEngine(InputMethod inputMethod, uint32_t flags)
    : inputMethod_(std::move(inputMethod)), flags_(flags) {}

void BambooEngine::setInputMethod(InputMethod inputMethod) {
    inputMethod_ = std::move(inputMethod);
    reset();
}

bool BambooEngine::isValid(bool inputIsFullComplete) const {
    auto [prev, last] = extractLastWord(composition_, inputMethod_.keys);
    return isValidComposition(last, inputIsFullComplete);
}

std::u32string BambooEngine::getProcessedU32(Mode mode) const {
    TransList tmp;
    if ((mode & FullText) != 0) {
        tmp = composition_;
    } else if ((mode & PunctuationMode) != 0) {
        auto [prev, last] = extractLastWordWithPunctuationMarks(composition_, inputMethod_.keys);
        return flattenU32(last, VietnameseMode);
    } else {
        auto [prev, last] = extractLastWord(composition_, inputMethod_.keys);
        tmp = std::move(last);
    }
    return flattenU32(tmp, mode);
}

std::string BambooEngine::getProcessedString(Mode mode) const {
    return u32ToUtf8(getProcessedU32(mode));
}

std::vector<Rule> BambooEngine::getApplicableRules(char32_t key) const {
    std::vector<Rule> applicableRules;
    char32_t lk = toLowerChar(key);
    for (const auto& inputRule : inputMethod_.rules) {
        if (inputRule.key == lk) {
            applicableRules.push_back(inputRule);
        }
    }
    return applicableRules;
}

std::pair<TransPtr, Rule> BambooEngine::findTargetByKey(const TransList& composition, char32_t key) const {
    return findTarget(composition, getApplicableRules(key), flags_);
}

bool BambooEngine::canProcessKey(char32_t key) const {
    return vnsnowkey::bamboo::canProcessKey(key, inputMethod_.keys);
}

TransList BambooEngine::generateTransformations(
    const TransList& composition, char32_t lowerKey, bool isUpperCase) const {
    auto rules = getApplicableRules(lowerKey);
    TransList transformations = generateTransformationsImpl(composition, rules, flags_, lowerKey, isUpperCase);
    if (transformations.empty()) {
        transformations = generateFallbackTransformations(composition, rules, lowerKey, isUpperCase);
        TransList newComp = composition;
        newComp.insert(newComp.end(), transformations.begin(), transformations.end());
        if (TransPtr virtualTrans = applyUowShortcut(newComp)) {
            transformations.push_back(virtualTrans);
        }
    }
    TransList combined = composition;
    combined.insert(combined.end(), transformations.begin(), transformations.end());
    TransList refreshed = refreshLastToneTarget(combined);
    transformations.insert(transformations.end(), refreshed.begin(), refreshed.end());
    return transformations;
}

TransList BambooEngine::newComposition(
    const TransList& composition, char32_t key, bool isUpperCase) const {
    auto [previousTransformations, lastSyllable] = extractLastSyllable(composition);
    TransList gen = generateTransformations(lastSyllable, key, isUpperCase);
    lastSyllable.insert(lastSyllable.end(), gen.begin(), gen.end());
    previousTransformations.insert(previousTransformations.end(), lastSyllable.begin(), lastSyllable.end());
    return previousTransformations;
}

TransPtr BambooEngine::applyUowShortcut(const TransList& syllable) const {
    std::u32string str = flattenU32(syllable, ToneLess | LowerCase);
    if (!inputMethod_.superKeys.empty() && matchesRegUOhTail(str)) {
        auto [target, missingRule] = findTargetByKey(syllable, inputMethod_.superKeys[0]);
        if (target != nullptr) {
            missingRule.key = 0; // virtual rule should not appear in the raw string
            auto virtualTrans = std::make_shared<Transformation>();
            virtualTrans->rule = missingRule;
            virtualTrans->target = target;
            return virtualTrans;
        }
    }
    return nullptr;
}

TransList BambooEngine::refreshLastToneTarget(const TransList& syllable) const {
    if ((flags_ & EfreeToneMarking) != 0 && isValidComposition(syllable, false)) {
        return refreshLastToneTargetImpl(syllable, (flags_ & EstdToneStyle) != 0);
    }
    return {};
}

void BambooEngine::processString(std::string_view utf8Str, Mode mode) {
    processU32String(utf8ToU32(utf8Str), mode);
}

void BambooEngine::processU32String(std::u32string_view u32Str, Mode mode) {
    for (char32_t key : u32Str) {
        processKey(key, mode);
    }
}

void BambooEngine::processKey(char32_t key, Mode mode) {
    char32_t lowerKey = toLowerChar(key);
    bool isUpper = isUpperChar(key);
    if ((mode & EnglishMode) != 0 || !canProcessKey(lowerKey)) {
        if ((mode & InReverseOrder) != 0) {
            composition_.insert(composition_.begin(), newAppendingTrans(lowerKey, isUpper));
            return;
        }
        composition_.push_back(newAppendingTrans(lowerKey, isUpper));
        return;
    }
    composition_ = newComposition(composition_, lowerKey, isUpper);
}

void BambooEngine::restoreLastWord(bool toVietnamese) {
    auto [previous, lastComb] = extractLastWord(composition_, inputMethod_.keys);
    if (lastComb.empty()) {
        return;
    }
    if (!toVietnamese) {
        TransList broken = breakComposition(lastComb);
        previous.insert(previous.end(), broken.begin(), broken.end());
        composition_ = std::move(previous);
    } else {
        TransList newComp;
        for (const auto& tnx : lastComb) {
            newComp = newComposition(newComp, tnx->rule.key, tnx->isUpperCase);
        }
        previous.insert(previous.end(), newComp.begin(), newComp.end());
        composition_ = std::move(previous);
    }
}

void BambooEngine::reset() {
    composition_.clear();
}

void BambooEngine::removeLastChar(bool refreshLastTone) {
    TransPtr lastAppending = findLastAppendingTrans(composition_);
    if (lastAppending == nullptr) {
        return;
    }
    if (!canProcessKey(lastAppending->rule.key)) {
        composition_.pop_back();
        return;
    }
    auto [previous, lastComb] = extractLastWord(composition_, inputMethod_.keys);
    TransList newComb;
    for (const auto& t : lastComb) {
        if (t->target == lastAppending || t == lastAppending) {
            continue;
        }
        newComb.push_back(t);
    }
    if (refreshLastTone) {
        TransList refreshed = refreshLastToneTarget(newComb);
        newComb.insert(newComb.end(), refreshed.begin(), refreshed.end());
    }
    previous.insert(previous.end(), newComb.begin(), newComb.end());
    composition_ = std::move(previous);
}

} // namespace vnsnowkey::bamboo
