/*
 * VNSnowKey - Vietnamese Input Method for Arch Linux + KDE Plasma (Wayland)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vnsnowkey::bamboo {

struct MacroEntry {
    std::string trigger;
    std::string replacement;
};

class MacroTable {
public:
    MacroTable() = default;

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    void setAutoCase(bool autoCase) { autoCase_ = autoCase; }
    bool isAutoCase() const { return autoCase_; }

    void setMacros(const std::map<std::string, std::string>& macros);
    const std::map<std::string, std::string>& getMacros() const { return macros_; }

    void addMacro(const std::string& trigger, const std::string& replacement);
    void removeMacro(const std::string& trigger);
    void clear();

    // Lookup macro expansion for a typed word (supports case adaptation when autoCase_ is true)
    std::optional<std::string> expand(std::string_view word) const;

    // Import / Export macros in text format ("trigger=replacement" or "trigger:replacement" or JSON/TOML lines)
    bool importFromFile(const std::string& filePath, std::string* errorMsg = nullptr);
    bool exportToFile(const std::string& filePath, std::string* errorMsg = nullptr) const;
    std::string serializeToText() const;
    void parseFromText(std::string_view content);

private:
    bool enabled_ = true;
    bool autoCase_ = true;
    std::map<std::string, std::string> macros_;
};

} // namespace vnsnowkey::bamboo
