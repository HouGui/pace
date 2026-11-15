// SPDX-License-Identifier: MIT
#pragma once
#include "pace/core.hpp"
#include "pace/protocol.hpp"
#include <cmath>
#include <cstring>
#include <string_view>

namespace pace::json_detail {
inline const cJSON *field(const cJSON *j, const char *name) {
    return cJSON_GetObjectItemCaseSensitive(j, name);
}

inline bool integer(const cJSON *j, std::int64_t low, std::int64_t high, std::int64_t &value) {
    if (!cJSON_IsNumber(j) || !std::isfinite(j->valuedouble) ||
        j->valuedouble < static_cast<double>(low) || j->valuedouble > static_cast<double>(high) ||
        std::floor(j->valuedouble) != j->valuedouble)
        return false;
    value = static_cast<std::int64_t>(j->valuedouble);
    return true;
}

inline bool timestamp(const cJSON *j, UtcSeconds &value) {
    return integer(j, 1577836800, 4102444799, value);
}

template <std::size_t N> bool string(const cJSON *j, std::array<char, N> &out, bool empty = false) {
    if (!cJSON_IsString(j))
        return false;
    const std::string_view s(j->valuestring);
    if ((!empty && s.empty()) || s.size() >= N || !valid_utf8(s))
        return false;
    out.fill(0);
    std::memcpy(out.data(), s.data(), s.size());
    return true;
}

inline bool boolean(const cJSON *j, bool &value) {
    if (!cJSON_IsBool(j))
        return false;
    value = cJSON_IsTrue(j);
    return true;
}

inline bool preferences(const cJSON *j, Preferences &p) {
    std::array<char, 4> cycle{};
    if (!string(field(j, "time_zone"), p.time_zone) || !string(field(j, "locale"), p.locale) ||
        !string(field(j, "hour_cycle"), cycle))
        return false;
    if (std::strcmp(cycle.data(), "h12") && std::strcmp(cycle.data(), "h23"))
        return false;
    p.twelve_hour = !std::strcmp(cycle.data(), "h12");
    return true;
}

inline bool array(const cJSON *j, int max, int &size) {
    if (!cJSON_IsArray(j))
        return false;
    size = cJSON_GetArraySize(j);
    return size <= max;
}

inline bool date(const cJSON *j, IsoDate &value) {
    return string(j, value) && date_number(value.data()) >= 0;
}

inline bool forbidden(const cJSON *j, std::initializer_list<const char *> names) {
    for (auto name : names)
        if (field(j, name))
            return true;
    return false;
}

} // namespace pace::json_detail
