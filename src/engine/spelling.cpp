/*
 * VNSnowKey - Vietnamese Input Method for Arch Linux + KDE Plasma (Wayland)
 * Engine derived from BambooEngine (bamboo-core)
 * Copyright (C) 2018 Luong Thanh Lam <ltlam93@gmail.com> (MIT License)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 */

#include "spelling.hpp"
#include "bamboo_utils.hpp"
#include <vector>

namespace vnsnowkey::bamboo {

namespace {

const std::vector<std::u32string> kFirstConsonantSeqs = {
    U"b d đ g gh m n nh p ph r s t tr v z",
    U"c h k kh qu th",
    U"ch gi l ng ngh x",
    U"đ l",
    U"h",
};

const std::vector<std::u32string> kVowelSeqs = {
    U"ê i ua uê uy y",
    U"a iê oa uyê yê",
    U"â ă e o oo ô ơ oe u ư uâ uô ươ",
    U"oă",
    U"uơ",
    U"ai ao au âu ay ây eo êu ia iêu iu oai oao oay oeo oi ôi ơi ưa uây ui ưi uôi ươi ươu ưu uya uyu yêu",
    U"ă",
    U"i",
};

const std::vector<std::u32string> kLastConsonantSeqs = {
    U"ch nh",
    U"c ng",
    U"m n p t",
    U"k",
    U"c",
};

const std::vector<std::vector<int>> kCvMatrix = {
    {0, 1, 2, 5},
    {0, 1, 2, 3, 4, 5},
    {0, 1, 2, 3, 5},
    {6},
    {7},
};

const std::vector<std::vector<int>> kVcMatrix = {
    {0, 2},
    {0, 1, 2},
    {1, 2},
    {1, 2},
    {},
    {},
    {3},
    {4},
};

std::vector<int> lookup(const std::vector<std::u32string>& seq, std::u32string_view input, bool inputIsFull, bool inputIsComplete) {
    std::vector<int> ret;
    const size_t inputLen = input.size();
    for (size_t index = 0; index < seq.size(); ++index) {
        const auto& row = seq[index];
        size_t i = 0;
        const size_t rowLen = row.size();
        for (size_t j = 0; j <= rowLen; ++j) {
            char32_t chr = (j < rowLen) ? row[j] : U' ';
            if (chr != U' ') {
                continue;
            }
            std::u32string_view canvas(row.data() + i, j - i);
            i = j + 1;
            if (canvas.size() < inputLen || (inputIsFull && canvas.size() > inputLen)) {
                continue;
            }
            bool isMatch = true;
            for (size_t k = 0; k < inputLen; ++k) {
                char32_t ic = input[k];
                if (ic != canvas[k] && !(!inputIsComplete && addMarkToTonelessChar(canvas[k], 0) == ic)) {
                    isMatch = false;
                    break;
                }
            }
            if (isMatch) {
                ret.push_back(static_cast<int>(index));
                break;
            }
        }
    }
    return ret;
}

bool isValidCV(const std::vector<int>& fcIndexes, const std::vector<int>& voIndexes) {
    for (int fc : fcIndexes) {
        for (int c : kCvMatrix[static_cast<size_t>(fc)]) {
            for (int vo : voIndexes) {
                if (c == vo) return true;
            }
        }
    }
    return false;
}

bool isValidVC(const std::vector<int>& voIndexes, const std::vector<int>& lcIndexes) {
    for (int vo : voIndexes) {
        for (int c : kVcMatrix[static_cast<size_t>(vo)]) {
            for (int lc : lcIndexes) {
                if (c == lc) return true;
            }
        }
    }
    return false;
}

} // namespace

bool isValidCVC(std::u32string_view fc, std::u32string_view vo, std::u32string_view lc, bool inputIsFullComplete) {
    std::vector<int> fcIndexes, voIndexes, lcIndexes;
    if (!fc.empty()) {
        fcIndexes = lookup(kFirstConsonantSeqs, fc, inputIsFullComplete || !vo.empty(), true);
        if (fcIndexes.empty()) return false;
    }
    if (!vo.empty()) {
        voIndexes = lookup(kVowelSeqs, vo, inputIsFullComplete || !lc.empty(), inputIsFullComplete);
        if (voIndexes.empty()) return false;
    }
    if (!lc.empty()) {
        lcIndexes = lookup(kLastConsonantSeqs, lc, inputIsFullComplete, true);
        if (lcIndexes.empty()) return false;
    }
    if (voIndexes.empty()) {
        return !fcIndexes.empty();
    }
    bool ret = false;
    if (!fcIndexes.empty()) {
        ret = isValidCV(fcIndexes, voIndexes);
        if (!ret || lcIndexes.empty()) {
            return ret;
        }
    }
    if (!lcIndexes.empty()) {
        ret = isValidVC(voIndexes, lcIndexes);
    } else {
        // Vowel-only syllable: "oă" (index 3) and "uơ" (index 4) are only valid with an initial consonant
        if (fcIndexes.empty() && voIndexes.size() == 1 && (voIndexes[0] == 3 || voIndexes[0] == 4)) {
            ret = false;
        } else {
            ret = true;
        }
    }
    return ret;
}

} // namespace vnsnowkey::bamboo
