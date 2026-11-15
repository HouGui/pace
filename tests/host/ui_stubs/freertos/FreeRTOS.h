// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
using TickType_t = std::uint32_t;
inline constexpr int pdTRUE = 1, pdFALSE = 0;
#define pdMS_TO_TICKS(value) (value)
