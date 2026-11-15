// SPDX-License-Identifier: MIT
#include "pace/calendar_view.hpp"
#include "pace/core.hpp"
#include <cstdio>
#include <cstring>
#include <string_view>

namespace pace {
namespace {
int month_number(const IsoDate &date) { return (date[5] - '0') * 10 + date[6] - '0'; }
} // namespace

MonthView month_view(const Snapshot &s, unsigned anchor) {
    MonthView result;
    if (anchor >= s.day_count)
        return result;
    auto first = s.days[anchor].date;
    first[8] = '0';
    first[9] = '1';
    const auto start = date_number(first.data());
    if (start < 0)
        return result;
    result.month = month_number(first);
    result.year =
        (first[0] - '0') * 1000 + (first[1] - '0') * 100 + (first[2] - '0') * 10 + first[3] - '0';
    const auto offset = (start + 3) % 7;
    for (int day = 1; day <= 31; ++day) {
        auto date = first;
        date[8] = '0' + day / 10;
        date[9] = '0' + day % 10;
        if (date_number(date.data()) < 0)
            break;
        auto &cell = result.cells[offset + day - 1];
        cell.day = day;
        for (unsigned i = 0; i < s.day_count; ++i) {
            if (s.days[i].date != date)
                continue;
            cell.snapshot_day = i;
            for (unsigned e = 0; e < s.event_count; ++e)
                if (overlaps(s.events[e], s.days[i])) {
                    if (s.events[e].secondary)
                        ++cell.secondary;
                    else
                        ++cell.events;
                }
            break;
        }
    }
    return result;
}

int adjacent_month(const Snapshot &s, unsigned anchor, int direction) {
    if (anchor >= s.day_count || (direction != -1 && direction != 1))
        return -1;
    const auto &date = s.days[anchor].date;
    for (int i = static_cast<int>(anchor) + direction; i >= 0 && i < s.day_count; i += direction)
        if (std::memcmp(date.data(), s.days[i].date.data(), 7) != 0)
            return i;
    return -1;
}

const char *month_name(int month, Language language) {
    static constexpr const char *en[] = {"January",   "February", "March",    "April",
                                         "May",       "June",     "July",     "August",
                                         "September", "October",  "November", "December"};
    static constexpr const char *de[] = {"Januar",    "Februar", "März",     "April",
                                         "Mai",       "Juni",    "Juli",     "August",
                                         "September", "Oktober", "November", "Dezember"};
    return month >= 1 && month <= 12 ? (language == Language::german ? de : en)[month - 1] : "";
}

const char *weekday_name(int index, Language language) {
    static constexpr const char *en[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static constexpr const char *de[] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
    return index >= 0 && index < 7 ? (language == Language::german ? de : en)[index] : "";
}

void date_label(const IsoDate &date, Language language, char *out, std::size_t size) {
    const auto number = date_number(date.data());
    if (number < 0) {
        if (size)
            out[0] = '\0';
        return;
    }
    std::snprintf(out, size, "%s %c%c.%c%c.", weekday_name((number + 3) % 7, language), date[8],
                  date[9], date[5], date[6]);
}

void temperature_text(float celsius, bool fahrenheit, Language language, char *out,
                      std::size_t size) {
    std::snprintf(out, size, "%.1f °%c", display_temperature(celsius, fahrenheit),
                  fahrenheit ? 'F' : 'C');
    if (language == Language::german)
        if (auto *decimal = std::strchr(out, '.'))
            *decimal = ',';
}

bool clock_text(const Snapshot &snapshot, UtcSeconds utc, bool twelve_hour, bool offset, char *out,
                std::size_t size) {
    for (unsigned i = 0; i < snapshot.clock_count; ++i) {
        const auto &segment = snapshot.clock[i];
        if (utc < segment.start_utc || utc >= segment.end_utc)
            continue;
        const auto minute = static_cast<int>(((utc + segment.offset_seconds) % 86400) / 60);
        const int hour = minute / 60;
        char zone[24]{};
        const int magnitude =
            segment.offset_seconds < 0 ? -segment.offset_seconds : segment.offset_seconds;
        if (offset)
            std::snprintf(zone, sizeof(zone), " GMT%c%02d:%02d",
                          segment.offset_seconds < 0 ? '-' : '+', magnitude / 3600,
                          magnitude % 3600 / 60);
        if (twelve_hour)
            std::snprintf(out, size, "%d:%02d %s%s", hour % 12 ? hour % 12 : 12, minute % 60,
                          hour >= 12 ? "PM" : "AM", zone);
        else
            std::snprintf(out, size, "%02d:%02d%s", hour, minute % 60, zone);
        return true;
    }
    if (size)
        out[0] = '\0';
    return false;
}

void compact_time_label(const char *label, char *out, std::size_t size) {
    // Keep adapter-localized AM/PM and out-of-window event times. Details retain the
    // original offset labels, including the two occurrences of a repeated DST hour.
    auto text = std::string_view(label);
    for (const auto zone : {" GMT", " UTC"}) {
        const auto at = text.find(zone);
        if (at != text.npos)
            text = text.substr(0, at);
    }
    std::snprintf(out, size, "%.*s", static_cast<int>(text.size()), text.data());
}
} // namespace pace
