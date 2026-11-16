/*
 * VNSnowKey - Vietnamese Input Method for Arch Linux + KDE Plasma (Wayland)
 * Engine derived from BambooEngine (bamboo-core)
 * Copyright (C) 2018 Luong Thanh Lam <ltlam93@gmail.com> (MIT License)
 * Copyright (C) 2026 VNSnowKey Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 */

#pragma once

#include <string_view>

namespace vnsnowkey::bamboo {

bool isValidCVC(std::u32string_view fc, std::u32string_view vo, std::u32string_view lc, bool inputIsFullComplete);

} // namespace vnsnowkey::bamboo
