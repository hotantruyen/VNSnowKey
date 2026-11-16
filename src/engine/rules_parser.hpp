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
#include <map>
#include <string>
#include <vector>

namespace vnsnowkey::bamboo {

const std::map<std::string, InputMethodDefinition>& getInputMethodDefinitions();
InputMethod parseInputMethod(const std::map<std::string, InputMethodDefinition>& imDef, const std::string& imName);
std::map<std::string, InputMethod> parseInputMethods(const std::map<std::string, InputMethodDefinition>& imDef);

std::vector<Rule> parseRules(char32_t key, const std::string& line);
std::vector<Rule> parseTonelessRules(char32_t key, const std::string& line);
std::vector<Rule> parseToneLessRule(char32_t key, char32_t effectiveOn, char32_t result, Mark effect);
bool getAppendingRule(char32_t key, const std::u32string& value, Rule& outRule);

} // namespace vnsnowkey::bamboo
