// SPDX-License-Identifier: MIT
#include "pace/calendar_view.hpp"
#include "pace/core.hpp"
#include "pace/sync.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
unsigned checks{};
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                                        \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
pace::Snapshot snapshot;

void day(unsigned i, const char *date) {
    std::strcpy(snapshot.days[i].date.data(), date);
    snapshot.days[i].start_utc = pace::date_number(date) * 86400;
    snapshot.days[i].end_utc = snapshot.days[i].start_utc + 86400;
    snapshot.day_count = i + 1;
}
} // namespace

int main() {
    using namespace pace;
    CHECK(month_view(snapshot, 0).month == 0);
    CHECK(adjacent_month(snapshot, 0, 1) == -1);
    day(0, "2024-02-28");
    day(1, "2024-02-29");
    day(2, "2024-03-01");
    auto month = month_view(snapshot, 0);
    CHECK(month.month == 2 && month.year == 2024);
    CHECK(month.cells[3].day == 1 && month.cells[3].snapshot_day == -1);
    CHECK(month.cells[31].day == 29 && month.cells[31].snapshot_day == 1);
    CHECK(month.cells[32].day == 0);
    CHECK(adjacent_month(snapshot, 0, 1) == 2);
    CHECK(adjacent_month(snapshot, 2, -1) == 1);
    CHECK(adjacent_month(snapshot, 0, -1) == -1);
    CHECK(adjacent_month(snapshot, 0, 0) == -1);
    snapshot.event_count = 2;
    snapshot.events[0].interval = AllDayInterval{snapshot.days[0].date, snapshot.days[2].date};
    snapshot.events[1].interval =
        TimedInterval{snapshot.days[0].end_utc - 60, snapshot.days[1].start_utc + 60, {}, {}};
    month = month_view(snapshot, 0);
    CHECK(month.cells[30].events == 2 && month.cells[31].events == 2);
    CHECK(month_view(snapshot, 2).cells[4].events == 0);
    day(0, "2030-09-01");
    month = month_view(snapshot, 0);
    CHECK(month.cells[6].day == 1 && month.cells[35].day == 30 && month.cells[36].day == 0);
    day(0, "2030-02-01");
    month = month_view(snapshot, 0);
    CHECK(month.cells[31].day == 28 && month.cells[32].day == 0);
    day(0, "2030-12-31");
    day(1, "2031-01-01");
    CHECK(adjacent_month(snapshot, 0, 1) == 1);
    CHECK(month_view(snapshot, 1).year == 2031);
    CHECK(!std::strcmp(month_name(3, Language::german), "März"));
    CHECK(!std::strcmp(weekday_name(0, Language::german), "Mo"));
    CHECK(!std::strcmp(weekday_name(0, Language::english), "Mon"));
    char text[64];
    temperature_text(21.5f, false, Language::german, text, sizeof(text));
    CHECK(!std::strcmp(text, "21,5 °C"));
    temperature_text(25.0f, true, Language::english, text, sizeof(text));
    CHECK(!std::strcmp(text, "77.0 °F"));
    date_label(snapshot.days[0].date, Language::german, text, sizeof(text));
    CHECK(!std::strcmp(text, "Di 31.12."));
    snapshot.clock_count = 2;
    const auto midnight = snapshot.days[0].start_utc;
    snapshot.clock[0] = {midnight, midnight + 3600, 7200};
    snapshot.clock[1] = {midnight + 3600, midnight + 86400, 3600};
    CHECK(clock_text(snapshot, midnight + 1800, false, true, text, sizeof(text)));
    CHECK(!std::strcmp(text, "02:30 GMT+02:00"));
    CHECK(clock_text(snapshot, midnight + 5400, false, true, text, sizeof(text)));
    CHECK(!std::strcmp(text, "02:30 GMT+01:00"));
    snapshot.clock[0].offset_seconds = -12600;
    CHECK(clock_text(snapshot, midnight, true, true, text, sizeof(text)));
    CHECK(!std::strcmp(text, "8:30 PM GMT-03:30"));
    snapshot.clock[0].offset_seconds = 20700;
    CHECK(clock_text(snapshot, midnight, true, true, text, sizeof(text)));
    CHECK(!std::strcmp(text, "5:45 AM GMT+05:45"));
    CHECK(!clock_text(snapshot, midnight - 1, false, true, text, sizeof(text)) && !text[0]);
    compact_time_label("09:30 AM GMT+5:45", text, sizeof(text));
    CHECK(!std::strcmp(text, "09:30 AM"));
    compact_time_label("09:30 UTC−3:30", text, sizeof(text));
    CHECK(!std::strcmp(text, "09:30"));
    compact_time_label("09:30", text, sizeof(text));
    CHECK(!std::strcmp(text, "09:30"));
    SyncSchedule schedule;
    CHECK(schedule.due(0, false));
    schedule.complete(1000, true, 0);
    CHECK(!schedule.due(600999, false));
    CHECK(schedule.due(601000, false));
    CHECK(schedule.due(1001, true));
    schedule.complete(2000, false, 0);
    CHECK(!schedule.due(16999, false) && schedule.due(17000, false));
    CHECK(schedule.due(2001, true));
    for (int i = 0; i < 100; ++i)
        schedule.complete(0, false, 0);
    CHECK(schedule.failures == 6 && schedule.next == 900000);
    schedule.complete(3000, true, 0);
    CHECK(schedule.failures == 0 && schedule.next == 603000);
    Snapshot priority{};
    priority.day_count = 14;
    priority.days[0] = snapshot.days[0];
    priority.event_count = 2;
    priority.events[0].secondary = true;
    priority.events[0].interval =
        TimedInterval{priority.days[0].start_utc + 600, priority.days[0].start_utc + 1200, {}, {}};
    priority.events[1].interval =
        TimedInterval{priority.days[0].start_utc + 1800, priority.days[0].start_utc + 2400, {}, {}};
    const auto ordered = events_for_day(priority, 0);
    CHECK(ordered.count == 2 && ordered.indices[0] == 1);
    CHECK(highlight(priority, priority.events[0], priority.days[0].start_utc, true) ==
          Highlight::none);
    CHECK(highlight(priority, priority.events[1], priority.days[0].start_utc, true) ==
          Highlight::next);
    const auto marked = month_view(priority, 0);
    unsigned primary_count = 0, secondary_count = 0;
    for (const auto &cell : marked.cells) {
        primary_count += cell.events;
        secondary_count += cell.secondary;
    }
    CHECK(primary_count == 1 && secondary_count == 1);
    std::printf("PASS: month/day presentation, locale and sync timing (%u assertions)\n", checks);
}
