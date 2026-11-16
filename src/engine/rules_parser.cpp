/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Engine derived from BambooEngine (bamboo-core)
 * Copyright (C) 2018 Luong Thanh Lam <ltlam93@gmail.com> (MIT License)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 */

#include "rules_parser.hpp"
#include "bamboo_utils.hpp"
#include <algorithm>
#include <unordered_map>

namespace vnsnowkey::bamboo {

namespace {

const std::unordered_map<std::string, Tone> kTones = {
    {"XoaDauThanh", Tone::None},
    {"DauSac",      Tone::Acute},
    {"DauHuyen",    Tone::Grave},
    {"DauNga",      Tone::Tilde},
    {"DauNang",     Tone::Dot},
    {"DauHoi",      Tone::Hook},
};

const std::map<std::string, InputMethodDefinition> kInputMethodDefinitions = {
    // Standard Telex (standalone w -> ư, matching Unikey/EVKey & VNSnowKey Requirement 2)
    {"Telex", {
        {"z", "XoaDauThanh"},
        {"s", "DauSac"},
        {"f", "DauHuyen"},
        {"r", "DauHoi"},
        {"x", "DauNga"},
        {"j", "DauNang"},
        {"a", "A_Â"},
        {"e", "E_Ê"},
        {"o", "O_Ô"},
        {"w", "UOA_ƯƠĂ__Ư"},
        {"d", "D_Đ"},
    }},
    // Simple Telex (standalone w stays w, matching bamboo-core "Telex")
    {"Simple Telex", {
        {"z", "XoaDauThanh"},
        {"s", "DauSac"},
        {"f", "DauHuyen"},
        {"r", "DauHoi"},
        {"x", "DauNga"},
        {"j", "DauNang"},
        {"a", "A_Â"},
        {"e", "E_Ê"},
        {"o", "O_Ô"},
        {"w", "UOA_ƯƠĂ"},
        {"d", "D_Đ"},
    }},
    {"VNI", {
        {"0", "XoaDauThanh"},
        {"1", "DauSac"},
        {"2", "DauHuyen"},
        {"3", "DauHoi"},
        {"4", "DauNga"},
        {"5", "DauNang"},
        {"6", "AEO_ÂÊÔ"},
        {"7", "UO_ƯƠ"},
        {"8", "A_Ă"},
        {"9", "D_Đ"},
    }},
    {"VIQR", {
        {"0", "XoaDauThanh"},
        {"'", "DauSac"},
        {"`", "DauHuyen"},
        {"?", "DauHoi"},
        {"~", "DauNga"},
        {".", "DauNang"},
        {"^", "AEO_ÂÊÔ"},
        {"+", "UO_ƯƠ"},
        {"*", "UO_ƯƠ"},
        {"(", "A_Ă"},
        {"d", "D_Đ"},
    }},
    {"Microsoft layout", {
        {"8", "DauSac"},
        {"5", "DauHuyen"},
        {"6", "DauHoi"},
        {"7", "DauNga"},
        {"9", "DauNang"},
        {"1", "__ă"},
        {"!", "_Ă"},
        {"2", "__â"},
        {"@", "_Â"},
        {"3", "__ê"},
        {"#", "_Ê"},
        {"4", "__ô"},
        {"$", "_Ô"},
        {"0", "__đ"},
        {")", "_Đ"},
        {"[", "__ư"},
        {"{", "_Ư"},
        {"]", "__ơ"},
        {"}", "_Ơ"},
    }},
    {"Telex 2", {
        {"z", "XoaDauThanh"},
        {"s", "DauSac"},
        {"f", "DauHuyen"},
        {"r", "DauHoi"},
        {"x", "DauNga"},
        {"j", "DauNang"},
        {"a", "A_Â"},
        {"e", "E_Ê"},
        {"o", "O_Ô"},
        {"w", "UOA_ƯƠĂ__Ư"},
        {"d", "D_Đ"},
        {"]", "__ư"},
        {"[", "__ơ"},
        {"}", "_Ư"},
        {"{", "_Ơ"},
    }},
    {"Telex + VNI", {
        {"z", "XoaDauThanh"},
        {"s", "DauSac"},
        {"f", "DauHuyen"},
        {"r", "DauHoi"},
        {"x", "DauNga"},
        {"j", "DauNang"},
        {"a", "A_Â"},
        {"e", "E_Ê"},
        {"o", "O_Ô"},
        {"w", "UOA_ƯƠĂ"},
        {"d", "D_Đ"},
        {"0", "XoaDauThanh"},
        {"1", "DauSac"},
        {"2", "DauHuyen"},
        {"3", "DauHoi"},
        {"4", "DauNga"},
        {"5", "DauNang"},
        {"6", "AEO_ÂÊÔ"},
        {"7", "UO_ƯƠ"},
        {"8", "A_Ă"},
        {"9", "D_Đ"},
    }},
    {"Telex + VNI + VIQR", {
        {"z",  "XoaDauThanh"},
        {"s",  "DauSac"},
        {"f",  "DauHuyen"},
        {"r",  "DauHoi"},
        {"x",  "DauNga"},
        {"j",  "DauNang"},
        {"a",  "A_Â"},
        {"e",  "E_Ê"},
        {"o",  "O_Ô"},
        {"w",  "UOA_ƯƠĂ"},
        {"d",  "D_Đ"},
        {"0",  "XoaDauThanh"},
        {"1",  "DauSac"},
        {"2",  "DauHuyen"},
        {"3",  "DauHoi"},
        {"4",  "DauNga"},
        {"5",  "DauNang"},
        {"6",  "AEO_ÂÊÔ"},
        {"7",  "UO_ƯƠ"},
        {"8",  "A_Ă"},
        {"9",  "D_Đ"},
        {"'",  "DauSac"},
        {"`",  "DauHuyen"},
        {"?",  "DauHoi"},
        {"~",  "DauNga"},
        {".",  "DauNang"},
        {"^",  "AEO_ÂÊÔ"},
        {"+",  "UO_ƯƠ"},
        {"*",  "UO_ƯƠ"},
        {"(",  "A_Ă"},
        {"\\", "D_Đ"},
    }},
    {"VNI Bàn phím tiếng Pháp", {
        {"&",  "XoaDauThanh"},
        {"é",  "DauSac"},
        {"\"", "DauHuyen"},
        {"'",  "DauHoi"},
        {"(",  "DauNga"},
        {"-",  "DauNang"},
        {"è",  "AEO_ÂÊÔ"},
        {"_",  "UO_ƯƠ"},
        {"ç",  "A_Ă"},
        {"à",  "D_Đ"},
    }},
    {"Telex W", {
        {"z", "XoaDauThanh"},
        {"s", "DauSac"},
        {"f", "DauHuyen"},
        {"r", "DauHoi"},
        {"x", "DauNga"},
        {"j", "DauNang"},
        {"a", "A_Â"},
        {"e", "E_Ê"},
        {"o", "O_Ô"},
        {"w", "UOA_ƯƠĂ__Ư"},
        {"d", "D_Đ"},
    }},
};

// Matches `([a-zA-Z]+)_(\p{L}+)([_\p{L}]*)`
bool matchDsl(const std::u32string& u32, std::u32string& part1, std::u32string& part2, std::u32string& part3) {
    size_t i = 0;
    const size_t n = u32.size();
    while (i < n && isAlphaChar(u32[i])) {
        ++i;
    }
    if (i == 0 || i >= n || u32[i] != U'_') {
        return false;
    }
    size_t p1End = i;
    ++i; // skip '_'
    size_t p2Start = i;
    while (i < n && isUnicodeLetter(u32[i])) {
        ++i;
    }
    if (i == p2Start) {
        return false;
    }
    size_t p2End = i;
    size_t p3Start = i;
    while (i < n && (u32[i] == U'_' || isUnicodeLetter(u32[i]))) {
        ++i;
    }
    part1 = u32.substr(0, p1End);
    part2 = u32.substr(p2Start, p2End - p2Start);
    part3 = u32.substr(p3Start, i - p3Start);
    return true;
}

// Matches `(_?)_(\p{L}+)`
bool matchDslAppending(const std::u32string& u32, std::u32string& outChars) {
    size_t n = u32.size();
    for (size_t start = 0; start < n; ++start) {
        size_t i = start;
        if (u32[i] != U'_') continue;
        ++i;
        if (i < n && u32[i] == U'_') {
            ++i;
        }
        size_t charsStart = i;
        while (i < n && isUnicodeLetter(u32[i])) {
            ++i;
        }
        if (i > charsStart) {
            outChars = u32.substr(charsStart, i - charsStart);
            return true;
        }
    }
    return false;
}

} // namespace

const std::map<std::string, InputMethodDefinition>& getInputMethodDefinitions() {
    return kInputMethodDefinitions;
}

namespace {

std::string normalizeImLookupKey(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (unsigned char c : sv) {
        if (c == ' ' || c == '_' || c == '-') continue;
        if (c >= 'A' && c <= 'Z') {
            out.push_back(static_cast<char>(c + ('a' - 'A')));
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

} // namespace

InputMethod parseInputMethod(const std::map<std::string, InputMethodDefinition>& imDef, const std::string& imName) {
    auto inputMethods = parseInputMethods(imDef);
    auto it = inputMethods.find(imName);
    if (it != inputMethods.end()) {
        return it->second;
    }
    std::string targetNorm = normalizeImLookupKey(imName);
    if (!targetNorm.empty()) {
        for (const auto& [name, im] : inputMethods) {
            if (normalizeImLookupKey(name) == targetNorm) {
                return im;
            }
        }
    }
    return InputMethod{};
}

std::map<std::string, InputMethod> parseInputMethods(const std::map<std::string, InputMethodDefinition>& imDef) {
    std::map<std::string, InputMethod> inputMethods;
    for (const auto& [name, imDefinition] : imDef) {
        InputMethod im;
        im.name = name;
        for (const auto& [keyStr, line] : imDefinition) {
            std::u32string keys = utf8ToU32(keyStr);
            if (keys.empty()) continue;
            char32_t key = keys[0];
            auto parsed = parseRules(key, line);
            im.rules.insert(im.rules.end(), parsed.begin(), parsed.end());

            std::u32string lowerLine = toLowerStr(utf8ToU32(line));
            if (lowerLine.find(U"uo") != std::u32string::npos) {
                im.superKeys.push_back(key);
            }
            im.keys.push_back(key);
        }
        for (const auto& rule : im.rules) {
            if (rule.effectType == EffectType::Appending) {
                im.appendingKeys.push_back(rule.key);
            }
            if (rule.effectType == EffectType::ToneTransformation) {
                im.toneKeys.push_back(rule.key);
            }
        }
        inputMethods[name] = std::move(im);
    }
    return inputMethods;
}

std::vector<Rule> parseRules(char32_t key, const std::string& line) {
    std::vector<Rule> rules;
    auto it = kTones.find(line);
    if (it != kTones.end()) {
        Rule rule;
        rule.key = key;
        rule.effectType = EffectType::ToneTransformation;
        rule.effect = static_cast<uint8_t>(it->second);
        rules.push_back(rule);
    } else {
        rules = parseTonelessRules(key, line);
    }
    return rules;
}

std::vector<Rule> parseTonelessRules(char32_t key, const std::string& line) {
    std::vector<Rule> rules;
    std::u32string u32Line = utf8ToU32(line);
    std::u32string p1, p2, p3;
    if (matchDsl(u32Line, p1, p2, p3)) {
        std::u32string lowerLine = toLowerStr(u32Line);
        matchDsl(lowerLine, p1, p2, p3);
        for (size_t i = 0; i < p1.size() && i < p2.size(); ++i) {
            char32_t effectiveOn = p1[i];
            Mark effect = Mark::None;
            if (!findMarkFromChar(p2[i], effect)) {
                continue;
            }
            auto subRules = parseToneLessRule(key, effectiveOn, p2[i], effect);
            rules.insert(rules.end(), subRules.begin(), subRules.end());
        }
        Rule appRule;
        if (getAppendingRule(key, p3, appRule)) {
            rules.push_back(std::move(appRule));
        }
    } else {
        Rule appRule;
        if (getAppendingRule(key, u32Line, appRule)) {
            rules.push_back(std::move(appRule));
        }
    }
    return rules;
}

std::vector<Rule> parseToneLessRule(char32_t key, char32_t effectiveOn, char32_t result, Mark effect) {
    std::vector<Rule> rules;
    for (char32_t chr : getMarkFamily(effectiveOn)) {
        if (chr == result) {
            Rule rule;
            rule.key = key;
            rule.effectType = EffectType::MarkTransformation;
            rule.effect = 0;
            rule.effectOn = result;
            rule.result = effectiveOn;
            rules.push_back(rule);
        } else if (isVowel(chr)) {
            for (uint8_t tone = 0; tone < 6; ++tone) {
                Rule rule;
                rule.key = key;
                rule.effectType = EffectType::MarkTransformation;
                rule.effectOn = addToneToChar(chr, tone);
                rule.effect = static_cast<uint8_t>(effect);
                rule.result = addToneToChar(result, tone);
                rules.push_back(rule);
            }
        } else {
            Rule rule;
            rule.key = key;
            rule.effectType = EffectType::MarkTransformation;
            rule.effectOn = chr;
            rule.effect = static_cast<uint8_t>(effect);
            rule.result = result;
            rules.push_back(rule);
        }
    }
    return rules;
}

bool getAppendingRule(char32_t key, const std::u32string& value, Rule& outRule) {
    std::u32string chars;
    if (matchDslAppending(value, chars) && !chars.empty()) {
        outRule = Rule{};
        outRule.key = key;
        outRule.effectType = EffectType::Appending;
        outRule.effectOn = chars[0];
        outRule.result = chars[0];
        for (size_t i = 1; i < chars.size(); ++i) {
            Rule app;
            app.key = key;
            app.effectType = EffectType::Appending;
            app.effectOn = chars[i];
            app.result = chars[i];
            outRule.appendedRules.push_back(app);
        }
        return true;
    }
    return false;
}

} // namespace vnsnowkey::bamboo
