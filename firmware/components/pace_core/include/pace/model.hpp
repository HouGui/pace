// SPDX-License-Identifier: MIT
#pragma once

#include "pace/traffic.hpp"
#include <array>
#include <cstdint>
#include <variant>

namespace pace {

inline constexpr std::uint16_t kFeedSchemaVersion = 2;
inline constexpr std::size_t kMaxEvents = 128;
inline constexpr std::size_t kDays = 62;
using UtcSeconds = std::int64_t;
using IsoDate = std::array<char, 11>; // YYYY-MM-DD plus terminator, validated at the boundary.
using CategoryId = std::array<char, 33>;

struct Category {
    CategoryId id{};
    std::array<char, 33> label{};
};

struct TimedInterval {
    UtcSeconds start_utc{};
    UtcSeconds end_utc{}; // Exclusive.
    std::array<char, 33> start_label{};
    std::array<char, 33> end_label{};
};

struct AllDayInterval {
    IsoDate start_date{};
    IsoDate end_date{}; // Exclusive local date; never convert through UTC midnight.
};

struct Event {
    std::array<char, 97> id{};
    std::array<char, 129> title{};
    std::array<char, 193> location{};
    std::array<CategoryId, 4> category_ids{};
    bool secondary{};
    std::uint8_t category_count{}; // Validated against category_ids.size().
    std::variant<TimedInterval, AllDayInterval> interval{};
};

struct CalendarDay {
    IsoDate date{};
    std::array<char, 65> label{};
    UtcSeconds start_utc{};
    UtcSeconds end_utc{};
};

struct ClockSegment {
    UtcSeconds start_utc{};
    UtcSeconds end_utc{};
    std::int32_t offset_seconds{};
};

struct Preferences {
    std::array<char, 65> time_zone{};
    std::array<char, 33> locale{};
    bool twelve_hour{false};
};

// Fixed-capacity values. No allocator, provider types, or SDK dependencies.
struct Snapshot {
    std::array<char, 33> secondary_label{};
    bool secondary_configured{}, secondary_available{};
    UtcSeconds generated_at_utc{};
    Preferences preferences{};
    std::array<CalendarDay, kDays> days{};
    std::array<ClockSegment, 32> clock{};
    std::array<Category, 8> categories{};
    std::array<Event, kMaxEvents> events{};
    std::uint16_t event_count{};
    std::uint8_t day_count{};
    std::uint8_t category_count{};
    std::uint8_t clock_count{};
};

struct CategoryStyle {
    CategoryId id{};
    std::uint32_t rgb{};
};

enum class Language { english, german };

struct Settings {
    TrafficSettings traffic{};
    Language language{Language::english};
    std::array<char, 513> ota_manifest_url{};
    std::array<char, 33> ssid{};
    std::array<char, 65> wifi_password{};
    std::array<char, 513> endpoint{};
    std::array<char, 129> device_token{};
    Preferences preferences{};
    std::array<CategoryStyle, 8> styles{};
    std::uint8_t style_count{};
    std::uint32_t secondary_rgb{0x7251a6};
    bool demo{true};
    bool google_redirect{false};
    bool fahrenheit{false};
    bool weather_enabled{false};
    double latitude{};
    double longitude{};
    std::array<char, 65> location{};
    bool night_enabled{false};
    std::uint16_t night_start{22 * 60};
    std::uint16_t night_end{7 * 60};
};

struct TemperatureSample {
    float celsius{}; // Canonical unit; presentation may convert to Fahrenheit.
    std::int64_t sampled_at_monotonic_ms{}; // Freshness survives a wall-clock correction.
    bool valid{false};
};

struct OutdoorTemperatureSample {
    float celsius{};
    UtcSeconds source_time_utc{};
    std::int64_t fetched_at_monotonic_ms{};
    bool valid{false};
};

} // namespace pace
