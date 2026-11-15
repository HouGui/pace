// SPDX-License-Identifier: MIT
#include "pace/calendar_view.hpp"
#include "ui_state.hpp"
#include <cstdio>

namespace pace::ui_detail {
void make_month(int today) {
    if (state.month_day >= state.snapshot->day_count)
        state.month_day = 0;
    const auto month = month_view(*state.snapshot, state.month_day);
    lv_obj_set_layout(state.agenda, LV_LAYOUT_NONE);
    char text[96];
    std::snprintf(text, sizeof(text), "%s %d",
                  month_name(month.month, state.runtime->settings.language), month.year);
    auto *title = label(state.agenda, text, 565);
    lv_obj_set_pos(title, 0, 3);
    for (int direction : {-1, 1}) {
        const int next = adjacent_month(*state.snapshot, state.month_day, direction);
        auto *arrow = button(
            state.agenda, direction < 0 ? "<" : ">", 66, 36,
            [](lv_event_t *e) {
                state.month_day = reinterpret_cast<std::intptr_t>(lv_event_get_user_data(e));
                navigate();
            },
            next);
        lv_obj_set_pos(arrow, direction < 0 ? 610 : 684, 0);
        if (next < 0)
            lv_obj_add_state(arrow, LV_STATE_DISABLED);
    }
    for (int col = 0; col < 7; ++col) {
        auto *day = label(state.agenda, weekday_name(col, state.runtime->settings.language), 104);
        lv_obj_set_pos(day, col * 108, 39);
        lv_obj_set_style_text_font(day, &pace_font_16, 0);
        lv_obj_set_style_text_align(day, LV_TEXT_ALIGN_CENTER, 0);
    }
    for (unsigned i = 0; i < month.cells.size(); ++i) {
        const auto &cell = month.cells[i];
        if (!cell.day)
            continue;
        if (cell.snapshot_day < 0)
            std::snprintf(text, sizeof(text), "%d  --", cell.day);
        else if (cell.events)
            std::snprintf(text, sizeof(text), "%d  (%u)", cell.day, cell.events);
        else
            std::snprintf(text, sizeof(text), "%d", cell.day);
        auto *day = button(state.agenda, text, 104, 32, open_day, cell.snapshot_day);
        lv_obj_set_pos(day, (i % 7) * 108, 59 + (i / 7) * 33);
        if (cell.secondary) {
            auto *mark = label(day, "*", 12);
            lv_obj_set_style_text_font(mark, &pace_font_16, 0);
            lv_obj_align(mark, LV_ALIGN_TOP_RIGHT, 4, -4);
            lv_obj_set_style_border_color(day, lv_color_hex(state.runtime->settings.secondary_rgb),
                                          0);
            lv_obj_set_style_border_width(day, 3, 0);
            lv_obj_set_style_border_side(day, LV_BORDER_SIDE_BOTTOM, 0);
        }
        if (cell.snapshot_day < 0)
            lv_obj_add_state(day, LV_STATE_DISABLED);
        else if (cell.snapshot_day == today) {
            lv_obj_set_style_bg_color(day, lv_color_hex(0x145b52), 0);
            lv_obj_set_style_text_color(day, lv_color_hex(0xffffff), 0);
        } else if (cell.events)
            lv_obj_set_style_bg_color(day, lv_color_hex(0xdcece5), 0);
    }
    std::snprintf(
        text, sizeof(text), "%s%s%s",
        tr("(N) main appointments · -- not loaded", "(N) Haupttermine · -- nicht geladen"),
        state.snapshot->secondary_configured ? " · * " : "",
        state.snapshot->secondary_configured ? secondary_name() : "");
    auto *hint = label(state.agenda, text, 750);
    lv_obj_set_pos(hint, 0, 259);
    lv_obj_set_style_text_font(hint, &pace_font_16, 0);
}
} // namespace pace::ui_detail
