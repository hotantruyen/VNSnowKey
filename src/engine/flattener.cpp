/*
 * VNSnowKey - Vietnamese Input Method for Arch Linux + KDE Plasma (Wayland)
 * Engine derived from BambooEngine (bamboo-core)
 * Copyright (C) 2018 Luong Thanh Lam <ltlam93@gmail.com> (MIT License)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 */

#include "flattener.hpp"
#include "bamboo_utils.hpp"
#include <unordered_map>

namespace vnsnowkey::bamboo {

std::u32string getCanvas(const TransList& composition, Mode mode) {
    std::u32string canvas;
    std::unordered_map<TransPtr, TransList> appendingMap;
    TransList appendingList;

    for (const auto& trans : composition) {
        if (!trans) continue;
        if ((mode & EnglishMode) != 0) {
            if (trans->rule.key == 0) {
                continue; // ignore virtual key
            }
            appendingList.push_back(trans);
        } else if (trans->rule.effectType == EffectType::Appending) {
            if (trans->rule.key == 0) {
                continue; // ignore virtual key
            }
            appendingList.push_back(trans);
        } else if (trans->target != nullptr) {
            appendingMap[trans->target].push_back(trans);
        }
    }

    canvas.reserve(appendingList.size());
    for (const auto& appendingTrans : appendingList) {
        char32_t chr = 0;
        const auto& transList = appendingMap[appendingTrans];
        if ((mode & EnglishMode) != 0) {
            chr = appendingTrans->rule.key;
        } else {
            chr = appendingTrans->rule.effectOn;
            for (const auto& trans : transList) {
                switch (trans->rule.effectType) {
                    case EffectType::MarkTransformation:
                        if (trans->rule.effect == static_cast<uint8_t>(Mark::Raw)) {
                            chr = appendingTrans->rule.key;
                        } else {
                            chr = addMarkToChar(chr, trans->rule.effect);
                        }
                        break;
                    case EffectType::ToneTransformation:
                        chr = addToneToChar(chr, trans->rule.effect);
                        break;
                    default:
                        break;
                }
            }
        }
        if ((mode & ToneLess) != 0) {
            chr = addToneToChar(chr, 0);
        }
        if ((mode & MarkLess) != 0) {
            chr = addMarkToChar(chr, 0);
        }
        if ((mode & LowerCase) != 0) {
            chr = toLowerChar(chr);
        } else if (appendingTrans->isUpperCase) {
            chr = toUpperChar(chr);
        }
        canvas.push_back(chr);
    }
    return canvas;
}

std::u32string flattenU32(const TransList& composition, Mode mode) {
    return getCanvas(composition, mode);
}

std::string flatten(const TransList& composition, Mode mode) {
    return u32ToUtf8(getCanvas(composition, mode));
}

} // namespace vnsnowkey::bamboo
