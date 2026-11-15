// SPDX-License-Identifier: MIT
#include "json_fields.hpp"
#include <algorithm>

namespace pace {
using namespace json_detail;

const char *feed_result_text(FeedResult r) {
    switch (r) {
    case FeedResult::ok:
        return "Calendar synchronized";
    case FeedResult::access_denied:
        return "Calendar: check pairing secret";
    case FeedResult::reauthorization_required:
        return "Calendar: authorize adapter again";
    case FeedResult::capacity_exceeded:
        return "Calendar exceeds capacity";
    case FeedResult::unsupported_preferences:
        return "Calendar: unsupported preferences";
    case FeedResult::temporarily_unavailable:
        return "Calendar temporarily unavailable";
    case FeedResult::invalid_request:
        return "Calendar request rejected";
    default:
        return "Calendar response invalid";
    }
}

FeedResult parse_feed(const char *bytes, std::size_t size, const Preferences &requested,
                      Snapshot &s) {
    Json json(bytes, size, 65536);
    const auto *root = json.get();
    std::int64_t version;
    if (!cJSON_IsObject(root) || !integer(field(root, "schema_version"), 2, 2, version) ||
        !cJSON_IsBool(field(root, "ok")))
        return FeedResult::invalid;
    if (!cJSON_IsTrue(field(root, "ok"))) {
        const auto *code = field(field(root, "error"), "code");
        if (!cJSON_IsString(code))
            return FeedResult::invalid;
        const char *names[] = {"access_denied",           "reauthorization_required",
                               "capacity_exceeded",       "unsupported_preferences",
                               "temporarily_unavailable", "invalid_request"};
        for (unsigned i = 0; i < 6; ++i)
            if (!std::strcmp(code->valuestring, names[i]))
                return static_cast<FeedResult>(i + 2);
        return FeedResult::invalid;
    }
    s.day_count = 0;
    s.event_count = 0;
    s.category_count = 0;
    s.clock_count = 0;
    s.secondary_label = {};
    s.secondary_configured = s.secondary_available = false;
    if (const auto *secondary = field(root, "secondary_calendar")) {
        if (!cJSON_IsObject(secondary) ||
            !string(field(secondary, "label"), s.secondary_label, true) ||
            !boolean(field(secondary, "available"), s.secondary_available))
            return FeedResult::invalid;
        s.secondary_configured = true;
    }
    if (!timestamp(field(root, "generated_at_utc"), s.generated_at_utc) ||
        !preferences(root, s.preferences) || !same_preferences(s.preferences, requested))
        return FeedResult::invalid;
    int count;
    auto *days = field(root, "days");
    if (!array(days, kDays, count) || count < 14)
        return FeedResult::invalid;
    s.day_count = count;
    for (int i = 0; i < count; ++i) {
        const auto *j = cJSON_GetArrayItem(days, i);
        auto &d = s.days[i];
        if (!date(field(j, "date"), d.date) || !string(field(j, "label"), d.label) ||
            !timestamp(field(j, "start_utc"), d.start_utc) ||
            !timestamp(field(j, "end_utc"), d.end_utc) || d.end_utc - d.start_utc < 20 * 3600 ||
            d.end_utc - d.start_utc > 28 * 3600)
            return FeedResult::invalid;
        if (i && (d.start_utc != s.days[i - 1].end_utc ||
                  date_number(d.date.data()) != date_number(s.days[i - 1].date.data()) + 1))
            return FeedResult::invalid;
    }
    if (current_day(s, s.generated_at_utc) < 0 ||
        current_day(s, s.generated_at_utc) + 7 > s.day_count)
        return FeedResult::invalid;
    auto *clock = field(root, "clock");
    if (!array(clock, 32, count) || count == 0)
        return FeedResult::invalid;
    s.clock_count = count;
    for (int i = 0; i < count; ++i) {
        const auto *j = cJSON_GetArrayItem(clock, i);
        auto &c = s.clock[i];
        std::int64_t offset;
        if (!timestamp(field(j, "start_utc"), c.start_utc) ||
            !timestamp(field(j, "end_utc"), c.end_utc) ||
            !integer(field(j, "offset_seconds"), -50400, 50400, offset) ||
            c.start_utc >= c.end_utc || (i && c.start_utc != s.clock[i - 1].end_utc))
            return FeedResult::invalid;
        c.offset_seconds = offset;
    }
    if (s.clock[0].start_utc != s.days[0].start_utc ||
        s.clock[count - 1].end_utc != s.days[s.day_count - 1].end_utc)
        return FeedResult::invalid;
    for (unsigned day = 0; day < s.day_count; ++day) {
        const auto &d = s.days[day];
        const auto date = date_number(d.date.data());
        for (unsigned i = 0; i < s.clock_count; ++i) {
            const auto &c = s.clock[i];
            const auto start = std::max(d.start_utc, c.start_utc);
            const auto end = std::min(d.end_utc, c.end_utc);
            // Check both ends of every overlap, including transitions inside a
            // day. Within a segment the local clock increases monotonically.
            if (start < end && ((start + c.offset_seconds) / 86400 != date ||
                                (end - 1 + c.offset_seconds) / 86400 != date))
                return FeedResult::invalid;
        }
    }
    auto *categories = field(root, "categories");
    if (!array(categories, 8, count))
        return FeedResult::invalid;
    s.category_count = count;
    for (int i = 0; i < count; ++i) {
        const auto *j = cJSON_GetArrayItem(categories, i);
        auto &c = s.categories[i];
        if (!string(field(j, "id"), c.id) || !string(field(j, "label"), c.label))
            return FeedResult::invalid;
        for (int k = 0; k < i; ++k)
            if (c.id == s.categories[k].id)
                return FeedResult::invalid;
    }
    auto *events = field(root, "events");
    if (!array(events, 128, count))
        return FeedResult::invalid;
    s.event_count = count;
    for (int i = 0; i < count; ++i) {
        const auto *j = cJSON_GetArrayItem(events, i);
        auto &e = s.events[i];
        e = Event{};
        bool all_day;
        if (!string(field(j, "id"), e.id) || !string(field(j, "title"), e.title, true) ||
            !boolean(field(j, "all_day"), all_day))
            return FeedResult::invalid;
        if (const auto *secondary = field(j, "secondary")) {
            if (!boolean(secondary, e.secondary) ||
                (e.secondary && (!s.secondary_configured || !s.secondary_available)))
                return FeedResult::invalid;
        }
        if (field(j, "location") && !string(field(j, "location"), e.location, true))
            return FeedResult::invalid;
        for (int k = 0; k < i; ++k)
            if (e.id == s.events[k].id)
                return FeedResult::invalid;
        const auto *ids = field(j, "category_ids");
        int n = 0;
        if (ids && !array(ids, 4, n))
            return FeedResult::invalid;
        e.category_count = n;
        for (int k = 0; k < n; ++k) {
            if (!string(cJSON_GetArrayItem(ids, k), e.category_ids[k]))
                return FeedResult::invalid;
            bool found = false;
            for (unsigned c = 0; c < s.category_count; ++c)
                if (e.category_ids[k] == s.categories[c].id)
                    found = true;
            for (int c = 0; c < k; ++c)
                if (e.category_ids[k] == e.category_ids[c])
                    return FeedResult::invalid;
            if (!found)
                return FeedResult::invalid;
        }
        if (all_day) {
            AllDayInterval a;
            if (forbidden(j, {"start_utc", "end_utc", "start_label", "end_label"}) ||
                !date(field(j, "start_date"), a.start_date) ||
                !date(field(j, "end_date"), a.end_date) ||
                date_number(a.start_date.data()) >= date_number(a.end_date.data()))
                return FeedResult::invalid;
            e.interval = a;
        } else {
            TimedInterval t;
            if (forbidden(j, {"start_date", "end_date"}) ||
                !timestamp(field(j, "start_utc"), t.start_utc) ||
                !timestamp(field(j, "end_utc"), t.end_utc) || t.start_utc >= t.end_utc ||
                !string(field(j, "start_label"), t.start_label) ||
                !string(field(j, "end_label"), t.end_label))
                return FeedResult::invalid;
            e.interval = t;
        }
        bool relevant = false;
        for (unsigned day = 0; day < s.day_count; ++day)
            if (overlaps(e, s.days[day]))
                relevant = true;
        if (!relevant)
            return FeedResult::invalid;
    }
    return FeedResult::ok;
}

} // namespace pace
