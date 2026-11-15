// SPDX-License-Identifier: MIT
#pragma once
#include "json_fields.hpp"

namespace pace::json_detail {
template <std::size_t N>
bool append(std::array<char, N> &out, const cJSON *value, const char *separator) {
    std::array<char, N> part{};
    if (!string(value, part))
        return false;
    const auto used = std::strlen(out.data());
    const auto extra = used ? std::strlen(separator) : 0;
    if (used + extra + std::strlen(part.data()) >= N)
        return false;
    if (used)
        std::strcat(out.data(), separator);
    std::strcat(out.data(), part.data());
    return true;
}

bool decode_polyline(const cJSON *value, unsigned precision, RouteGeometry &geometry);
} // namespace pace::json_detail
