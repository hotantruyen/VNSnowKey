/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Engine derived from BambooEngine (bamboo-core)
 * Copyright (C) 2018 Luong Thanh Lam <ltlam93@gmail.com> (MIT License)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <map>
#include <vector>

namespace vnsnowkey::bamboo {

using Mode = uint32_t;

constexpr Mode VietnameseMode  = 1u << 0;
constexpr Mode EnglishMode     = 1u << 1;
constexpr Mode ToneLess        = 1u << 2;
constexpr Mode MarkLess        = 1u << 3;
constexpr Mode LowerCase       = 1u << 4;
constexpr Mode FullText        = 1u << 5;
constexpr Mode PunctuationMode = 1u << 6;
constexpr Mode InReverseOrder  = 1u << 7;

constexpr uint32_t EfreeToneMarking    = 1u << 0;
constexpr uint32_t EstdToneStyle       = 1u << 1;
constexpr uint32_t EautoCorrectEnabled = 1u << 2;
constexpr uint32_t EstdFlags           = EfreeToneMarking | EstdToneStyle | EautoCorrectEnabled;

enum class EffectType : int {
    Appending = 0,
    MarkTransformation = 1,
    ToneTransformation = 2,
    Replacing = 3
};

enum class Mark : uint8_t {
    None  = 0,
    Hat   = 1,
    Breve = 2,
    Horn  = 3,
    Dash  = 4,
    Raw   = 5
};

enum class Tone : uint8_t {
    None  = 0,
    Grave = 1,
    Acute = 2,
    Hook  = 3,
    Tilde = 4,
    Dot   = 5
};

struct Rule {
    char32_t key = 0;
    uint8_t effect = 0; // (Tone or Mark)
    EffectType effectType = EffectType::Appending;
    char32_t effectOn = 0;
    char32_t result = 0;
    std::vector<Rule> appendedRules;

    void setTone(Tone tone) { effect = static_cast<uint8_t>(tone); }
    void setMark(Mark mark) { effect = static_cast<uint8_t>(mark); }
    Tone getTone() const { return static_cast<Tone>(effect); }
    Mark getMark() const { return static_cast<Mark>(effect); }
};

struct Transformation {
    Rule rule;
    std::shared_ptr<Transformation> target;
    bool isUpperCase = false;
};

using TransPtr = std::shared_ptr<Transformation>;
using TransList = std::vector<TransPtr>;

struct InputMethod {
    std::string name;
    std::vector<Rule> rules;
    std::vector<char32_t> superKeys;
    std::vector<char32_t> toneKeys;
    std::vector<char32_t> appendingKeys;
    std::vector<char32_t> keys;
};

using InputMethodDefinition = std::map<std::string, std::string>;

} // namespace vnsnowkey::bamboo
