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

namespace vnsnowkey::bamboo {

std::u32string getCanvas(const TransList& composition, Mode mode);
std::u32string flattenU32(const TransList& composition, Mode mode);
std::string flatten(const TransList& composition, Mode mode);

} // namespace vnsnowkey::bamboo
