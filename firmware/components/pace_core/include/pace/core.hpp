// SPDX-License-Identifier: MIT
#pragma once
#include "pace/model.hpp"
#include <cstddef>
#include <string_view>

namespace pace {
bool valid_utf8(std::string_view text);
// Invalid dates return -1. Valid result is days since 1970-01-01.
std::int64_t date_number(std::string_view date);
bool same_preferences(const Preferences &a, const Preferences &b);
int current_day(const Snapshot &snapshot, UtcSeconds now);
int local_minute(const Snapshot &snapshot, UtcSeconds now);
bool overlaps(const Event &event, const CalendarDay &day);

struct DayEvents {
    std::array<std::uint16_t, kMaxEvents> indices{};
    std::uint16_t count{};
};

DayEvents events_for_day(const Snapshot &snapshot, std::size_t day);
enum class Highlight { none, past, now, next };
Highlight highlight(const Snapshot &snapshot, const Event &event, UtcSeconds now, bool trusted);
enum class Freshness { unavailable, time_unknown, expired, stale, current };
Freshness freshness(const Snapshot *snapshot, UtcSeconds now, bool trusted);
bool night_active(int minute, std::uint16_t start, std::uint16_t end);
float display_temperature(float celsius, bool fahrenheit);
bool indoor_available(const TemperatureSample &sample, std::int64_t monotonic_ms);
Freshness outdoor_freshness(const OutdoorTemperatureSample &sample, UtcSeconds now,
                            std::int64_t monotonic_ms, bool time_trusted);
std::uint8_t sht_crc(const std::uint8_t *bytes, std::size_t size);
bool decode_sht40(const std::array<std::uint8_t, 6> &bytes, float &celsius);
// Saturating exponential retry with caller-provided entropy; no global RNG.
std::uint32_t retry_seconds(unsigned failures, std::uint32_t random);
} // namespace pace
