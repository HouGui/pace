// SPDX-License-Identifier: MIT
#include "pace/core.hpp"
#include <algorithm>
#include <cstring>

namespace pace {
bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x20 || first == 0x7f)
            return false;
        if (first < 0x80)
            continue;
        unsigned remaining;
        std::uint32_t point, minimum;
        if (first >= 0xc2 && first <= 0xdf) {
            remaining = 1;
            point = first & 31;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            remaining = 2;
            point = first & 15;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            remaining = 3;
            point = first & 7;
            minimum = 0x10000;
        } else
            return false;
        if (remaining > text.size() - i)
            return false;
        while (remaining--) {
            auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xc0) != 0x80)
                return false;
            point = (point << 6) | (byte & 63);
        }
        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff))
            return false;
        // Reject invisible direction overrides; labels are plain display text.
        if ((point >= 0x202a && point <= 0x202e) || (point >= 0x2066 && point <= 0x2069))
            return false;
    }
    return true;
}

std::int64_t date_number(std::string_view date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-')
        return -1;
    for (unsigned i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9'))
            return -1;
    const int year =
        (date[0] - '0') * 1000 + (date[1] - '0') * 100 + (date[2] - '0') * 10 + date[3] - '0';
    const int month = (date[5] - '0') * 10 + date[6] - '0';
    const int day = (date[8] - '0') * 10 + date[9] - '0';
    if (year < 2020 || year > 2099 || month < 1 || month > 12)
        return -1;
    constexpr int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (day < 1 || day > lengths[month - 1] + (month == 2 && leap))
        return -1;
    std::int64_t days = 0;
    for (int y = 1970; y < year; ++y)
        days += 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    for (int m = 1; m < month; ++m)
        days += lengths[m - 1] + (m == 2 && leap);
    return days + day - 1;
}

bool same_preferences(const Preferences &a, const Preferences &b) {
    return a.time_zone == b.time_zone && a.locale == b.locale && a.twelve_hour == b.twelve_hour;
}

int current_day(const Snapshot &s, UtcSeconds now) {
    for (std::size_t i = 0; i < s.day_count; ++i)
        if (now >= s.days[i].start_utc && now < s.days[i].end_utc)
            return static_cast<int>(i);
    return -1;
}

int local_minute(const Snapshot &s, UtcSeconds now) {
    for (unsigned i = 0; i < s.clock_count; ++i) {
        const auto &c = s.clock[i];
        if (now >= c.start_utc && now < c.end_utc)
            return static_cast<int>(((now + c.offset_seconds) % 86400) / 60);
    }
    return -1;
}

bool overlaps(const Event &event, const CalendarDay &day) {
    if (const auto *t = std::get_if<TimedInterval>(&event.interval))
        return t->start_utc < day.end_utc && t->end_utc > day.start_utc;
    const auto &a = std::get<AllDayInterval>(event.interval);
    return std::strcmp(a.start_date.data(), day.date.data()) <= 0 &&
           std::strcmp(a.end_date.data(), day.date.data()) > 0;
}

DayEvents events_for_day(const Snapshot &s, std::size_t day) {
    DayEvents result;
    if (day >= s.day_count)
        return result;
    for (unsigned i = 0; i < s.event_count; ++i)
        if (overlaps(s.events[i], s.days[day]))
            result.indices[result.count++] = i;
    std::sort(result.indices.begin(), result.indices.begin() + result.count, [&s](auto a, auto b) {
        if (s.events[a].secondary != s.events[b].secondary)
            return !s.events[a].secondary;
        const auto *ta = std::get_if<TimedInterval>(&s.events[a].interval);
        const auto *tb = std::get_if<TimedInterval>(&s.events[b].interval);
        if (!ta != !tb)
            return !ta; // All-day first.
        if (ta && ta->start_utc != tb->start_utc)
            return ta->start_utc < tb->start_utc;
        return std::strcmp(s.events[a].id.data(), s.events[b].id.data()) < 0;
    });
    return result;
}

Highlight highlight(const Snapshot &s, const Event &event, UtcSeconds now, bool trusted) {
    const auto *t = std::get_if<TimedInterval>(&event.interval);
    if (!trusted || !t || event.secondary)
        return Highlight::none;
    if (t->end_utc <= now)
        return Highlight::past;
    if (t->start_utc <= now)
        return Highlight::now;
    for (unsigned i = 0; i < s.event_count; ++i)
        if (const auto *other = std::get_if<TimedInterval>(&s.events[i].interval))
            if (!s.events[i].secondary && other->start_utc > now && other->start_utc < t->start_utc)
                return Highlight::none;
    return Highlight::next;
}

Freshness freshness(const Snapshot *s, UtcSeconds now, bool trusted) {
    if (!s)
        return Freshness::unavailable;
    if (!trusted || now < s->generated_at_utc - 300)
        return Freshness::time_unknown;
    const int day = current_day(*s, now);
    if (day < 0 || day + 7 > s->day_count)
        return Freshness::expired;
    return now - s->generated_at_utc >= 1800 ? Freshness::stale : Freshness::current;
}

bool night_active(int minute, std::uint16_t start, std::uint16_t end) {
    if (minute < 0 || minute >= 1440 || start >= 1440 || end >= 1440 || start == end)
        return false;
    return start < end ? minute >= start && minute < end : minute >= start || minute < end;
}

float display_temperature(float c, bool f) { return f ? c * 1.8f + 32.0f : c; }

bool indoor_available(const TemperatureSample &s, std::int64_t mono) {
    return s.valid && mono >= s.sampled_at_monotonic_ms &&
           mono - s.sampled_at_monotonic_ms <= 300000;
}

Freshness outdoor_freshness(const OutdoorTemperatureSample &s, UtcSeconds now, std::int64_t mono,
                            bool trusted) {
    if (!s.valid)
        return Freshness::unavailable;
    if (!trusted || now < s.source_time_utc - 300 || mono < s.fetched_at_monotonic_ms)
        return Freshness::time_unknown;
    const auto fetched_age = mono - s.fetched_at_monotonic_ms;
    const auto source_age = now - s.source_time_utc;
    if (fetched_age > 21600000 || source_age > 21600)
        return Freshness::expired;
    return fetched_age >= 3600000 || source_age >= 3600 ? Freshness::stale : Freshness::current;
}

std::uint8_t sht_crc(const std::uint8_t *bytes, std::size_t size) {
    std::uint8_t crc = 0xff;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
    }
    return crc;
}

bool decode_sht40(const std::array<std::uint8_t, 6> &b, float &celsius) {
    if (sht_crc(b.data(), 2) != b[2] || sht_crc(b.data() + 3, 2) != b[5])
        return false;
    celsius = -45.0f + 175.0f * static_cast<float>((b[0] << 8) | b[1]) / 65535.0f;
    return true;
}

std::uint32_t retry_seconds(unsigned failures, std::uint32_t random) {
    const auto base = std::min(15u << std::min(failures, 6u), 900u);
    return base + random % (base / 4 + 1);
}
} // namespace pace
