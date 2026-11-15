// SPDX-License-Identifier: MIT
#include "pace/calendar_view.hpp"
#include "pace/core.hpp"
#include "ui_state.hpp"
#include <algorithm>
#include <cstdio>

namespace pace::ui_detail {
namespace {
constexpr unsigned kDetailPageSize = 8;
}

void open_day(lv_event_t *event) {
    state.detail_page = 0;
    state.selected_event = -1;
    state.selected_day =
        static_cast<int>(reinterpret_cast<std::intptr_t>(lv_event_get_user_data(event)));
    state.view = View::day;
    lv_obj_scroll_to_y(state.agenda, 0, LV_ANIM_OFF);
    navigate();
}

void open_event(lv_event_t *event) {
    state.selected_event =
        static_cast<int>(reinterpret_cast<std::intptr_t>(lv_event_get_user_data(event)));
    lv_obj_scroll_to_y(state.agenda, 0, LV_ANIM_OFF);
    navigate();
}

std::uint32_t color(const CategoryId &id) {
    for (unsigned i = 0; i < state.runtime->settings.style_count; ++i)
        if (state.runtime->settings.styles[i].id == id)
            return state.runtime->settings.styles[i].rgb;
    constexpr std::uint32_t palette[] = {0x287d71, 0x7251a6, 0xa75a24, 0x2879a3,
                                         0xb64363, 0x636d24, 0x477657, 0x6c677e};
    for (unsigned i = 0; i < state.snapshot->category_count; ++i)
        if (state.snapshot->categories[i].id == id)
            return palette[i];
    return 0x65727b;
}

const char *secondary_name() {
    return state.snapshot->secondary_label[0] ? state.snapshot->secondary_label.data()
                                              : tr("Second calendar", "Zweiter Kalender");
}

std::uint32_t event_color(const Event &event) {
    return event.secondary        ? state.runtime->settings.secondary_rgb
           : event.category_count ? color(event.category_ids[0])
                                  : 0x65727b;
}

void event_text(const Event &event, char *text, std::size_t size, UtcSeconds now, int day,
                bool detail) {
    if (const auto *t = std::get_if<TimedInterval>(&event.interval)) {
        const auto mark = highlight(*state.snapshot, event, now, state.health.time_trusted);
        const char *prefix = mark == Highlight::now    ? tr("Now | ", "Jetzt | ")
                             : mark == Highlight::next ? tr("Next | ", "Danach | ")
                                                       : "";
        const char *continuing = day >= 0 && t->start_utc < state.snapshot->days[day].start_utc
                                     ? tr("Continues | ", "Fortsetzung | ")
                                     : "";
        if (detail)
            std::snprintf(text, size, "%s%s%s - %s  %s", prefix, continuing, t->start_label.data(),
                          t->end_label.data(),
                          event.title[0] ? event.title.data() : tr("(Untitled)", "(Ohne Titel)"));
        else {
            char time[33];
            compact_time_label(t->start_label.data(), time, sizeof(time));
            std::snprintf(text, size, "%s%s%s  %s", prefix, continuing, time,
                          event.title[0] ? event.title.data() : tr("(Untitled)", "(Ohne Titel)"));
        }
    } else
        std::snprintf(text, size, tr("All day | %s", "Ganztägig | %s"),
                      event.title[0] ? event.title.data() : tr("(Untitled)", "(Ohne Titel)"));
    check_font(event.title.data());
    if (const auto *timed = std::get_if<TimedInterval>(&event.interval)) {
        check_font(timed->start_label.data());
        if (detail)
            check_font(timed->end_label.data());
    }
}

namespace {
void categories(lv_obj_t *row, const Event &event) {
    if (event.secondary) {
        auto *source = label(row, secondary_name(), 700);
        lv_obj_set_style_border_color(source, lv_color_hex(event_color(event)), 0);
        lv_obj_set_style_border_width(source, 4, 0);
        lv_obj_set_style_border_side(source, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_pad_left(source, 8, 0);
        return;
    }
    if (!event.category_count)
        label(row, tr("Other", "Sonstiges"), 700);
    for (unsigned c = 0; c < event.category_count; ++c) {
        const char *name = tr("Other", "Sonstiges");
        for (unsigned k = 0; k < state.snapshot->category_count; ++k)
            if (event.category_ids[c] == state.snapshot->categories[k].id)
                name = state.snapshot->categories[k].label.data();
        check_font(name);
        auto *chip = label(row, name, 700);
        lv_obj_set_style_border_color(chip, lv_color_hex(color(event.category_ids[c])), 0);
        lv_obj_set_style_border_width(chip, 3, 0);
        lv_obj_set_style_border_side(chip, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_pad_left(chip, 8, 0);
    }
}

void event_details(UtcSeconds now) {
    button(state.agenda, tr("< Back", "< Zurück"), 750, 40, [](lv_event_t *) {
        state.selected_event = -1;
        navigate();
    });
    const auto &event = state.snapshot->events[state.selected_event];
    char text[512];
    event_text(event, text, sizeof(text), now, -1, true);
    auto *title = label(state.agenda, text, 740);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    if (const auto *timed = std::get_if<TimedInterval>(&event.interval)) {
        const int start = current_day(*state.snapshot, timed->start_utc);
        const int end = current_day(*state.snapshot, timed->end_utc - 1);
        if (start >= 0 && end >= 0) {
            std::snprintf(text, sizeof(text), "%s – %s", state.snapshot->days[start].date.data(),
                          state.snapshot->days[end].date.data());
            label(state.agenda, text, 740);
        }
    } else {
        const auto &days = std::get<AllDayInterval>(event.interval);
        std::snprintf(text, sizeof(text),
                      tr("%s to %s (end exclusive)", "%s bis %s (Enddatum exklusiv)"),
                      days.start_date.data(), days.end_date.data());
        label(state.agenda, text, 740);
    }
    label(state.agenda, tr("Location", "Ort"), 740);
    auto *location = label(state.agenda,
                           event.location[0] ? event.location.data()
                                             : tr("No location provided", "Kein Ort angegeben"),
                           740);
    lv_label_set_long_mode(location, LV_LABEL_LONG_WRAP);
    check_font(event.location.data());
    categories(state.agenda, event);
}

void day_list(UtcSeconds now) {
    if (state.selected_day >= state.snapshot->day_count)
        state.selected_day = 0;
    auto *header = lv_obj_create(state.agenda);
    lv_obj_set_size(header, 750, 44);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    auto *date = label(header, state.snapshot->days[state.selected_day].label.data(), 560);
    check_font(state.snapshot->days[state.selected_day].label.data());
    lv_obj_set_pos(date, 8, 6);
    for (int direction : {-1, 1}) {
        auto *arrow = button(
            header, direction < 0 ? "<" : ">", 66, 40,
            [](lv_event_t *e) {
                const int d = reinterpret_cast<std::intptr_t>(lv_event_get_user_data(e));
                state.selected_day += d;
                state.detail_page = 0;
                lv_obj_scroll_to_y(state.agenda, 0, LV_ANIM_OFF);
                navigate();
            },
            direction);
        lv_obj_set_pos(arrow, direction < 0 ? 602 : 678, 0);
        if (state.selected_day + direction < 0 ||
            state.selected_day + direction >= state.snapshot->day_count)
            lv_obj_add_state(arrow, LV_STATE_DISABLED);
    }
    const auto events = events_for_day(*state.snapshot, state.selected_day);
    if (!events.count)
        label(state.agenda, tr("No appointments", "Keine Termine"), 740);
    state.detail_page =
        events.count ? std::min<unsigned>(state.detail_page, (events.count - 1) / kDetailPageSize)
                     : 0;
    const unsigned begin = state.detail_page * kDetailPageSize;
    const unsigned end = std::min<unsigned>(events.count, begin + kDetailPageSize);
    for (unsigned i = begin; i < end; ++i) {
        const auto &event = state.snapshot->events[events.indices[i]];
        if (event.secondary &&
            (i == begin || !state.snapshot->events[events.indices[i - 1]].secondary)) {
            auto *source = label(state.agenda, secondary_name(), 740);
            lv_obj_set_style_text_font(source, &pace_font_16, 0);
            check_font(secondary_name());
        }
        char text[384];
        event_text(event, text, sizeof(text), now, state.selected_day, true);
        auto *row = button(state.agenda, text, 750, 64, open_event, events.indices[i]);
        auto *title = lv_obj_get_child(row, 0);
        lv_obj_set_style_text_font(title, &pace_font_24, 0);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
        lv_obj_set_height(title, 58);
        lv_obj_set_style_border_color(row, lv_color_hex(event_color(event)), 0);
        lv_obj_set_style_border_width(row, 4, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        if (highlight(*state.snapshot, event, now, state.health.time_trusted) == Highlight::past)
            lv_obj_set_style_text_opa(title, LV_OPA_60, 0);
    }
    for (int direction : {-1, 1}) {
        if ((direction < 0 && !state.detail_page) || (direction > 0 && end == events.count))
            continue;
        button(
            state.agenda,
            direction < 0 ? tr("Previous appointments", "Vorherige Termine")
                          : tr("More appointments", "Weitere Termine"),
            750, 44,
            [](lv_event_t *e) {
                const int d = reinterpret_cast<std::intptr_t>(lv_event_get_user_data(e));
                state.detail_page += d;
                lv_obj_scroll_to_y(state.agenda, 0, LV_ANIM_OFF);
                navigate();
            },
            direction);
    }
}

void week_list(UtcSeconds now, int today) {
    const int first = today < 0 ? 0 : today;
    for (int day = first; day < first + 7 && day < state.snapshot->day_count; ++day) {
        const auto events = events_for_day(*state.snapshot, day);
        auto *row = button(state.agenda, "", 750, 37, nullptr);
        lv_obj_clean(row);
        lv_obj_set_style_bg_color(row, lv_color_hex(day == today ? 0xe3efeb : 0xf5f7f6), 0);
        lv_obj_set_style_text_font(row, &pace_font_24, 0);
        char text[384];
        std::snprintf(text, sizeof(text), "%s", tr("No appointments", "Keine Termine"));
        if (events.count) {
            const auto &event = state.snapshot->events[events.indices[0]];
            event_text(event, text, sizeof(text), now, day);
            lv_obj_set_style_border_color(row, lv_color_hex(event_color(event)), 0);
            lv_obj_set_style_border_width(row, 3, 0);
            lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        }
        check_font(state.snapshot->days[day].label.data());
        auto *date = label(row, state.snapshot->days[day].label.data(), 165);
        lv_obj_add_flag(date, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(date, open_day, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<std::intptr_t>(day)));
        unsigned secondary = 0;
        for (unsigned i = 0; i < events.count; ++i)
            secondary += state.snapshot->events[events.indices[i]].secondary;
        const unsigned primary = events.count - secondary;
        const unsigned more_count = primary > 0 ? primary - 1 : 0;
        auto *title = label(row, text,
                            secondary    ? (more_count ? 340 : 430)
                            : more_count ? 470
                                         : 565);
        lv_obj_set_pos(title, 172, 0);
        lv_obj_add_flag(title, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(title, events.count ? open_event : open_day, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<std::intptr_t>(
                                events.count ? events.indices[0] : day)));
        if (secondary) {
            std::snprintf(text, sizeof(text), "*%u %s", secondary, secondary_name());
            check_font(secondary_name());
            auto *badge = label(row, text, 128);
            lv_obj_set_pos(badge, more_count ? 518 : 610, 5);
            lv_obj_set_style_text_font(badge, &pace_font_16, 0);
            lv_obj_set_style_border_color(badge,
                                          lv_color_hex(state.runtime->settings.secondary_rgb), 0);
            lv_obj_set_style_border_width(badge, 3, 0);
            lv_obj_set_style_border_side(badge, LV_BORDER_SIDE_LEFT, 0);
            lv_obj_set_style_pad_left(badge, 5, 0);
            lv_obj_add_flag(badge, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(badge, open_day, LV_EVENT_CLICKED,
                                reinterpret_cast<void *>(static_cast<std::intptr_t>(day)));
        }
        if (more_count) {
            std::snprintf(text, sizeof(text), tr("+%u more", "+%u mehr"), more_count);
            auto *more = label(row, text, 92);
            lv_obj_set_pos(more, 650, 5);
            lv_obj_set_style_text_font(more, &pace_font_16, 0);
            lv_obj_add_flag(more, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(more, open_day, LV_EVENT_CLICKED,
                                reinterpret_cast<void *>(static_cast<std::intptr_t>(day)));
        }
        lv_obj_add_event_cb(row, open_day, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<std::intptr_t>(day)));
    }
}
} // namespace

void make_agenda(UtcSeconds now) {
    const auto scroll = state.view == View::day || state.selected_event >= 0
                            ? lv_obj_get_scroll_y(state.agenda)
                            : 0;
    lv_obj_clean(state.agenda);
    lv_obj_set_flex_flow(state.agenda, LV_FLEX_FLOW_COLUMN);
    if (!state.snapshot->clock_count) {
        auto *hint = label(state.agenda,
                           tr("Calendar unavailable. Hold Settings to configure PACE.",
                              "Kalender nicht verfügbar. Einstellungen gedrückt halten."),
                           740);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        return;
    }
    const int today = state.health.time_trusted ? current_day(*state.snapshot, now) : -1;
    if (state.selected_event >= 0 && state.selected_event < state.snapshot->event_count)
        event_details(now);
    else if (state.view == View::month)
        make_month(today);
    else if (state.view == View::day)
        day_list(now);
    else
        week_list(now, today);
    lv_obj_scroll_to_y(state.agenda, scroll, LV_ANIM_OFF);
}
} // namespace pace::ui_detail
