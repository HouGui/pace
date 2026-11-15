// SPDX-License-Identifier: MIT
#pragma once
#include "pace/model.hpp"

namespace pace {
struct MonthCell {
    int day{};            // Zero denotes a cell outside this month.
    int snapshot_day{-1}; // Uncovered dates must never look like empty dates.
    unsigned events{}, secondary{};
};

struct MonthView {
    int year{}, month{};
    std::array<MonthCell, 42> cells{}; // Monday first, at most six weeks.
};

MonthView month_view(const Snapshot &snapshot, unsigned anchor);
int adjacent_month(const Snapshot &snapshot, unsigned anchor, int direction);
const char *month_name(int month, Language language);
const char *weekday_name(int monday_index, Language language);
void date_label(const IsoDate &date, Language language, char *out, std::size_t size);
void temperature_text(float celsius, bool fahrenheit, Language language, char *out,
                      std::size_t size);
bool clock_text(const Snapshot &snapshot, UtcSeconds utc, bool twelve_hour, bool offset, char *out,
                std::size_t size);
void compact_time_label(const char *label, char *out, std::size_t size);
} // namespace pace
