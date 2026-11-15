// SPDX-License-Identifier: MIT
#include "network_state.hpp"
#include "pace/calendar_view.hpp"
#include "pace/core.hpp"
#include <cstring>
#include <ctime>

namespace pace::services::network_detail {
void make_demo(Snapshot &s, const Preferences &p, UtcSeconds now) {
    // Demo deliberately uses UTC and labels it. Real feeds carry IANA transitions.
    s.secondary_configured = s.secondary_available = false;
    s.secondary_label = {};
    s.preferences = p;
    s.generated_at_utc = now;
    s.clock_count = 1;
    s.category_count = 1;
    s.event_count = 2;
    const time_t current = now;
    tm civil{};
    gmtime_r(&current, &civil);
    const auto start = now - now % 86400 - (civil.tm_mday - 1) * 86400;
    s.day_count = kDays;
    const auto language =
        std::strncmp(p.locale.data(), "de", 2) == 0 ? Language::german : Language::english;
    for (unsigned i = 0; i < s.day_count; ++i) {
        auto &d = s.days[i];
        d.start_utc = start + i * 86400;
        d.end_utc = d.start_utc + 86400;
        time_t value = d.start_utc;
        tm date{};
        gmtime_r(&value, &date);
        strftime(d.date.data(), d.date.size(), "%Y-%m-%d", &date);
        date_label(d.date, language, d.label.data(), d.label.size());
    }
    s.clock[0] = {start, start + s.day_count * 86400, 0};
    std::strcpy(s.categories[0].id.data(), "demo");
    std::strcpy(s.categories[0].label.data(), "Demo");
    s.events[0] = Event{};
    s.events[1] = Event{};
    std::strcpy(s.events[0].id.data(), "demo-1");
    std::strcpy(s.events[0].title.data(), language == Language::german ? "Entdecke deinen Kalender"
                                                                       : "Explore your calendar");
    std::strcpy(s.events[0].location.data(),
                language == Language::german ? "Besprechungsraum" : "Meeting room");
    TimedInterval event;
    event.start_utc = now + 1800;
    event.end_utc = now + 3600;
    for (auto end : {false, true}) {
        time_t value = end ? event.end_utc : event.start_utc;
        tm time{};
        gmtime_r(&value, &time);
        auto &label = end ? event.end_label : event.start_label;
        strftime(label.data(), label.size(), "%H:%M UTC", &time);
    }
    s.events[0].interval = event;
    s.events[0].category_count = 1;
    s.events[0].category_ids[0] = s.categories[0].id;
    std::strcpy(s.events[1].id.data(), "demo-2");
    std::strcpy(s.events[1].title.data(),
                language == Language::german ? "Tippe auf diesen Tag" : "Try tapping this day");
    s.events[1].interval =
        AllDayInterval{s.days[civil.tm_mday - 1].date, s.days[civil.tm_mday].date};
}
} // namespace pace::services::network_detail
