/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "macro_table.hpp"
#include "bamboo_utils.hpp"
#include <fstream>
#include <sstream>

namespace vnsnowkey::bamboo {

namespace {

std::string trimStr(std::string_view sv) {
    size_t start = 0;
    while (start < sv.size() && (sv[start] == ' ' || sv[start] == '\t' || sv[start] == '\r' || sv[start] == '\n')) {
        ++start;
    }
    size_t end = sv.size();
    while (end > start && (sv[end - 1] == ' ' || sv[end - 1] == '\t' || sv[end - 1] == '\r' || sv[end - 1] == '\n')) {
        --end;
    }
    return std::string(sv.substr(start, end - start));
}

bool isAllUpperU32(std::u32string_view s) {
    bool hasLetter = false;
    for (char32_t c : s) {
        if (isUnicodeLetter(c)) {
            hasLetter = true;
            if (!isUpperChar(c)) return false;
        }
    }
    return hasLetter;
}

bool isTitleCaseU32(std::u32string_view s) {
    if (s.size() < 2) return false;
    if (!isUnicodeLetter(s[0]) || !isUpperChar(s[0])) return false;
    for (size_t i = 1; i < s.size(); ++i) {
        if (isUnicodeLetter(s[i]) && isUpperChar(s[i])) return false;
    }
    return true;
}

} // namespace

void MacroTable::setMacros(const std::map<std::string, std::string>& macros) {
    macros_ = macros;
}

void MacroTable::addMacro(const std::string& trigger, const std::string& replacement) {
    if (!trigger.empty()) {
        macros_[trigger] = replacement;
    }
}

void MacroTable::removeMacro(const std::string& trigger) {
    macros_.erase(trigger);
}

void MacroTable::clear() {
    macros_.clear();
}

std::optional<std::string> MacroTable::expand(std::string_view word) const {
    if (!enabled_ || word.empty() || macros_.empty()) {
        return std::nullopt;
    }
    std::string key(word);
    auto it = macros_.find(key);
    if (it != macros_.end()) {
        return it->second;
    }
    if (!autoCase_) {
        return std::nullopt;
    }

    std::u32string u32Word = utf8ToU32(word);
    std::string lowerKey = u32ToUtf8(toLowerStr(u32Word));
    auto lowerIt = macros_.find(lowerKey);
    if (lowerIt == macros_.end()) {
        return std::nullopt;
    }

    std::u32string u32Rep = utf8ToU32(lowerIt->second);
    if (isAllUpperU32(u32Word)) {
        return u32ToUtf8(toUpperStr(u32Rep));
    }
    if (isTitleCaseU32(u32Word) && !u32Rep.empty()) {
        u32Rep[0] = toUpperChar(u32Rep[0]);
        return u32ToUtf8(u32Rep);
    }
    return lowerIt->second;
}

std::string MacroTable::serializeToText() const {
    std::ostringstream oss;
    oss << "# VNSnowKey Macro Table (format: trigger = replacement)\n";
    for (const auto& [k, v] : macros_) {
        oss << k << " = " << v << "\n";
    }
    return oss.str();
}

void MacroTable::parseFromText(std::string_view content) {
    std::string str(content);
    std::istringstream iss(str);
    std::string line;
    while (std::getline(iss, line)) {
        std::string trimmed = trimStr(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        size_t sep = trimmed.find('=');
        if (sep == std::string::npos) {
            sep = trimmed.find(':');
        }
        if (sep != std::string::npos && sep > 0) {
            std::string k = trimStr(std::string_view(trimmed).substr(0, sep));
            std::string v = trimStr(std::string_view(trimmed).substr(sep + 1));
            if (k.size() >= 2 && k.front() == '"' && k.back() == '"') {
                k = k.substr(1, k.size() - 2);
            }
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                v = v.substr(1, v.size() - 2);
            }
            if (!k.empty() && !v.empty()) {
                macros_[k] = v;
            }
        }
    }
}

bool MacroTable::importFromFile(const std::string& filePath, std::string* errorMsg) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) {
        if (errorMsg) *errorMsg = "Cannot open file for reading: " + filePath;
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    parseFromText(oss.str());
    return true;
}

bool MacroTable::exportToFile(const std::string& filePath, std::string* errorMsg) const {
    std::ofstream ofs(filePath, std::ios::trunc);
    if (!ofs.is_open()) {
        if (errorMsg) *errorMsg = "Cannot open file for writing: " + filePath;
        return false;
    }
    ofs << serializeToText();
    return ofs.good();
}

} // namespace vnsnowkey::bamboo
