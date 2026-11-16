/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Engine derived from BambooEngine (bamboo-core)
 * Copyright (C) 2018 Luong Thanh Lam <ltlam93@gmail.com> (MIT License)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 */

#include "bamboo_utils.hpp"
#include <unordered_map>

namespace vnsnowkey::bamboo {

namespace {

const std::u32string kVowelsLower =
    U"aàáảãạăằắẳẵặâầấẩẫậeèéẻẽẹêềếểễệiìíỉĩịoòóỏõọôồốổỗộơờớởỡợuùúủũụưừứửữựyỳýỷỹỵ";

const std::u32string kVowelsUpper =
    U"AÀÁẢÃẠĂẰẮẲẴẶÂẦẤẨẪẬEÈÉẺẼẸÊỀẾỂỄỆIÌÍỈĨỊOÒÓỎÕỌÔỒỐỔỖỘƠỜỚỞỠỢUÙÚỦŨỤƯỪỨỬỮỰYỲÝỶỸỴ";

const std::vector<char32_t> kPunctuationMarks = {
    U',', U';', U':', U'.', U'"', U'\'', U'!', U'?', U' ',
    U'<', U'>', U'=', U'+', U'-', U'*', U'/', U'\\',
    U'_', U'~', U'`', U'@', U'#', U'$', U'%', U'^', U'&', U'(', U')', U'{', U'}', U'[', U']',
    U'|'
};

const std::unordered_map<char32_t, std::u32string> kMarksMaps = {
    {U'a', U"aâă__"},
    {U'â', U"aâă__"},
    {U'ă', U"aâă__"},
    {U'e', U"eê___"},
    {U'ê', U"eê___"},
    {U'o', U"oô_ơ_"},
    {U'ô', U"oô_ơ_"},
    {U'ơ', U"oô_ơ_"},
    {U'u', U"u__ư_"},
    {U'ư', U"u__ư_"},
    {U'd', U"d___đ"},
    {U'đ', U"d___đ"},
};

struct CaseTables {
    std::unordered_map<char32_t, char32_t> upperToLower;
    std::unordered_map<char32_t, char32_t> lowerToUpper;
    std::vector<char32_t> allVietnameseChars;

    CaseTables() {
        for (size_t i = 0; i < kVowelsLower.size(); ++i) {
            char32_t lo = kVowelsLower[i];
            char32_t up = kVowelsUpper[i];
            upperToLower[up] = lo;
            lowerToUpper[lo] = up;
            if (lo > 127) {
                allVietnameseChars.push_back(lo);
                allVietnameseChars.push_back(up);
            }
        }
        upperToLower[U'Đ'] = U'đ';
        lowerToUpper[U'đ'] = U'Đ';
        allVietnameseChars.push_back(U'đ');
        allVietnameseChars.push_back(U'Đ');

        // Also Latin-1 supplement letters (e.g., French VNI keys é, è, ç, à)
        for (char32_t c = 0x00C0; c <= 0x00DE; ++c) {
            if (c == 0x00D7) continue;
            upperToLower[c] = c + 0x20;
            lowerToUpper[c + 0x20] = c;
        }
    }
};

const CaseTables& getCaseTables() {
    static const CaseTables tables;
    return tables;
}

} // namespace

std::u32string utf8ToU32(std::string_view utf8) {
    std::u32string out;
    out.reserve(utf8.size());
    size_t i = 0;
    const size_t n = utf8.size();
    while (i < n) {
        uint8_t c = static_cast<uint8_t>(utf8[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char32_t>(c));
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < n) {
            char32_t cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(utf8[i + 1]) & 0x3F);
            out.push_back(cp);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < n) {
            char32_t cp = ((c & 0x0F) << 12) |
                          ((static_cast<uint8_t>(utf8[i + 1]) & 0x3F) << 6) |
                          (static_cast<uint8_t>(utf8[i + 2]) & 0x3F);
            out.push_back(cp);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < n) {
            char32_t cp = ((c & 0x07) << 18) |
                          ((static_cast<uint8_t>(utf8[i + 1]) & 0x3F) << 12) |
                          ((static_cast<uint8_t>(utf8[i + 2]) & 0x3F) << 6) |
                          (static_cast<uint8_t>(utf8[i + 3]) & 0x3F);
            out.push_back(cp);
            i += 4;
        } else {
            out.push_back(static_cast<char32_t>(c));
            i += 1;
        }
    }
    return out;
}

std::string u32CharToUtf8(char32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::string u32ToUtf8(std::u32string_view u32) {
    std::string out;
    out.reserve(u32.size() * 2);
    for (char32_t cp : u32) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

char32_t toLowerChar(char32_t c) {
    if (c >= U'A' && c <= U'Z') {
        return c + (U'a' - U'A');
    }
    const auto& tbl = getCaseTables().upperToLower;
    auto it = tbl.find(c);
    if (it != tbl.end()) {
        return it->second;
    }
    return c;
}

char32_t toUpperChar(char32_t c) {
    if (c >= U'a' && c <= U'z') {
        return c - (U'a' - U'A');
    }
    const auto& tbl = getCaseTables().lowerToUpper;
    auto it = tbl.find(c);
    if (it != tbl.end()) {
        return it->second;
    }
    return c;
}

bool isUpperChar(char32_t c) {
    if (c >= U'A' && c <= U'Z') {
        return true;
    }
    const auto& tbl = getCaseTables().upperToLower;
    return tbl.find(c) != tbl.end();
}

bool isAlphaChar(char32_t c) {
    return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
}

bool isUnicodeLetter(char32_t c) {
    if (isAlphaChar(c)) return true;
    const auto& tables = getCaseTables();
    if (tables.lowerToUpper.find(c) != tables.lowerToUpper.end()) return true;
    if (tables.upperToLower.find(c) != tables.upperToLower.end()) return true;
    return false;
}

std::u32string toLowerStr(std::u32string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (char32_t c : s) out.push_back(toLowerChar(c));
    return out;
}

std::u32string toUpperStr(std::u32string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (char32_t c : s) out.push_back(toUpperChar(c));
    return out;
}

const std::u32string& getVowels() {
    return kVowelsLower;
}

const std::u32string& getUppercaseVowels() {
    return kVowelsUpper;
}

const std::vector<char32_t>& getAllVietnameseChars() {
    return getCaseTables().allVietnameseChars;
}

bool isSpace(char32_t key) {
    return key == U' ';
}

bool isPunctuationMark(char32_t key) {
    for (char32_t c : kPunctuationMarks) {
        if (c == key) return true;
    }
    return false;
}

bool isWordBreakSymbol(char32_t key) {
    return isPunctuationMark(key) || (U'0' <= key && key <= U'9');
}

bool isVowel(char32_t chr) {
    for (char32_t v : kVowelsLower) {
        if (v == chr) return true;
    }
    return false;
}

int findVowelPosition(char32_t chr) {
    for (size_t pos = 0; pos < kVowelsLower.size(); ++pos) {
        if (kVowelsLower[pos] == chr) {
            return static_cast<int>(pos);
        }
    }
    return -1;
}

std::vector<char32_t> getMarkFamily(char32_t chr) {
    std::vector<char32_t> result;
    auto it = kMarksMaps.find(chr);
    if (it != kMarksMaps.end()) {
        for (char32_t c : it->second) {
            if (c != U'_') {
                result.push_back(c);
            }
        }
    }
    return result;
}

int findMarkPosition(char32_t chr) {
    auto it = kMarksMaps.find(chr);
    if (it != kMarksMaps.end()) {
        const auto& str = it->second;
        for (size_t pos = 0; pos < str.size(); ++pos) {
            if (str[pos] == chr) {
                return static_cast<int>(pos);
            }
        }
    }
    return -1;
}

bool findMarkFromChar(char32_t chr, Mark& outMark) {
    int pos = findMarkPosition(chr);
    if (pos >= 0) {
        outMark = static_cast<Mark>(pos);
        return true;
    }
    outMark = Mark::None;
    return false;
}

char32_t addMarkToTonelessChar(char32_t chr, uint8_t mark) {
    auto it = kMarksMaps.find(chr);
    if (it != kMarksMaps.end()) {
        const auto& marks = it->second;
        if (mark < marks.size() && marks[mark] != U'_') {
            return marks[mark];
        }
    }
    return chr;
}

char32_t addMarkToChar(char32_t chr, uint8_t mark) {
    Tone tone = findToneFromChar(chr);
    chr = addToneToChar(chr, 0);
    chr = addMarkToTonelessChar(chr, mark);
    return addToneToChar(chr, static_cast<uint8_t>(tone));
}

bool inKeyList(const std::vector<char32_t>& keys, char32_t key) {
    for (char32_t k : keys) {
        if (k == key) return true;
    }
    return false;
}

Tone findToneFromChar(char32_t chr) {
    int pos = findVowelPosition(chr);
    if (pos == -1) {
        return Tone::None;
    }
    return static_cast<Tone>(pos % 6);
}

char32_t addToneToChar(char32_t chr, uint8_t tone) {
    int pos = findVowelPosition(chr);
    if (pos > -1) {
        int currentTone = pos % 6;
        int offset = static_cast<int>(tone) - currentTone;
        return kVowelsLower[static_cast<size_t>(pos + offset)];
    }
    return chr;
}

bool canProcessKey(char32_t lowerKey, const std::vector<char32_t>& effectKeys) {
    if (isAlphaChar(lowerKey) || inKeyList(effectKeys, lowerKey)) {
        return true;
    }
    if (isWordBreakSymbol(lowerKey)) {
        return false;
    }
    return isVietnameseRune(lowerKey);
}

bool isVietnameseRune(char32_t lowerKey) {
    if (findToneFromChar(lowerKey) != Tone::None) {
        return true;
    }
    return lowerKey != addMarkToTonelessChar(lowerKey, 0);
}

bool hasAnyVietnameseRune(std::u32string_view word) {
    for (char32_t chr : word) {
        if (isVietnameseRune(toLowerChar(chr))) {
            return true;
        }
    }
    return false;
}

bool hasAnyVietnameseVowel(std::u32string_view word) {
    for (char32_t chr : word) {
        if (isVowel(toLowerChar(chr))) {
            return true;
        }
    }
    return false;
}

void decomposeVietnameseChar(char32_t chr, char32_t& outBaseAscii, Mark& outMark, Tone& outTone) {
    const bool upper = isUpperChar(chr);
    char32_t lo = toLowerChar(chr);
    outTone = findToneFromChar(lo);
    char32_t toneless = addToneToChar(lo, 0);
    if (!findMarkFromChar(toneless, outMark)) {
        outMark = Mark::None;
    }
    char32_t base = addMarkToTonelessChar(toneless, 0);
    outBaseAscii = upper ? toUpperChar(base) : base;
}

} // namespace vnsnowkey::bamboo
