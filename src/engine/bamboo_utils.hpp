/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
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
#include <vector>

namespace vnsnowkey::bamboo {

// UTF-8 <-> UTF-32 conversion utilities
std::u32string utf8ToU32(std::string_view utf8);
std::string u32ToUtf8(std::u32string_view u32);
std::string u32CharToUtf8(char32_t c);

// Locale-independent Unicode case and character classification for ASCII + Vietnamese
char32_t toLowerChar(char32_t c);
char32_t toUpperChar(char32_t c);
bool isUpperChar(char32_t c);
bool isAlphaChar(char32_t c);
bool isUnicodeLetter(char32_t c);
std::u32string toLowerStr(std::u32string_view s);
std::u32string toUpperStr(std::u32string_view s);

// Bamboo character utilities
const std::u32string& getVowels();
const std::u32string& getUppercaseVowels();
const std::vector<char32_t>& getAllVietnameseChars(); // All 134 Vietnamese chars (67 lower + 67 upper)

bool isSpace(char32_t key);
bool isPunctuationMark(char32_t key);
bool isWordBreakSymbol(char32_t key);
bool isVowel(char32_t chr);
int findVowelPosition(char32_t chr);

std::vector<char32_t> getMarkFamily(char32_t chr);
int findMarkPosition(char32_t chr);
bool findMarkFromChar(char32_t chr, Mark& outMark);

char32_t addMarkToTonelessChar(char32_t chr, uint8_t mark);
char32_t addMarkToChar(char32_t chr, uint8_t mark);

bool inKeyList(const std::vector<char32_t>& keys, char32_t key);
Tone findToneFromChar(char32_t chr);
char32_t addToneToChar(char32_t chr, uint8_t tone);

bool canProcessKey(char32_t lowerKey, const std::vector<char32_t>& effectKeys);
bool isVietnameseRune(char32_t lowerKey);
bool hasAnyVietnameseRune(std::u32string_view word);
bool hasAnyVietnameseVowel(std::u32string_view word);

// Decompose a Vietnamese letter (preserving original case in outBaseAscii) into base ASCII + Mark + Tone.
void decomposeVietnameseChar(char32_t chr, char32_t& outBaseAscii, Mark& outMark, Tone& outTone);

} // namespace vnsnowkey::bamboo
