/*
 * VNSnowKey - Vietnamese Input Method for Linux (Wayland & X11)
 * Engine derived from BambooEngine (bamboo-core)
 * Copyright (C) 2018 Luong Thanh Lam <ltlam93@gmail.com> (MIT License)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace vnsnowkey::bamboo {

inline constexpr const char* CHARSET_UNICODE = "Unicode";
inline constexpr const char* CHARSET_TCVN3 = "TCVN3 (ABC)";
inline constexpr const char* CHARSET_VNI_WINDOWS = "VNI Windows";
inline constexpr const char* CHARSET_UNICODE_COMPOSITE = "Unicode tổ hợp";
inline constexpr const char* CHARSET_VIQR = "VIQR";
inline constexpr const char* CHARSET_CP1258 = "Windows 1258 codepage";

// Encode from standard precomposed Unicode (NFC) to target charset
std::string encodeCharset(const std::string& charsetName, std::string_view unicodeInput);

// Decode from source charset back to standard precomposed Unicode (NFC)
std::string decodeCharset(const std::string& charsetName, std::string_view encodedInput);

// Convert text between any two supported charsets
std::string convertCharset(
    const std::string& fromCharset,
    const std::string& toCharset,
    std::string_view input,
    bool stripDiacritics = false);

// Remove all Vietnamese diacritics/tones (e.g., "Tiếng Việt" -> "Tieng Viet")
std::string removeVietnameseDiacritics(std::string_view unicodeInput);

// List all supported charset names
std::vector<std::string> getCharsetNames();

} // namespace vnsnowkey::bamboo
