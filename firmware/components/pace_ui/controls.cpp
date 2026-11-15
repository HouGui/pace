// SPDX-License-Identifier: MIT
#include "esp_timer.h"
#include "pace/core.hpp"
#include "ui_state.hpp"
#include <ctime>

namespace pace::ui_detail {
const char *tr(const char *en, const char *de) {
    return state.runtime->settings.language == Language::german ? de : en;
}

void navigate() {
    state.last_touch = esp_timer_get_time() / 1000;
    state.redraw = state.calendar_redraw = true;
}

lv_obj_t *button(lv_obj_t *parent, const char *text, int width, int height, lv_event_cb_t callback,
                 std::intptr_t data) {
    auto *object = lv_button_create(parent);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_pad_all(object, 4, 0);
    lv_obj_set_style_radius(object, 8, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(0xe8eeec), 0);
    lv_obj_set_style_text_color(object, lv_color_hex(0x1d303b), 0);
    lv_obj_set_style_text_font(object, &pace_font_16, 0);
    auto *caption = label(object, text, width - 8);
    lv_obj_set_height(caption, 20);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(caption);
    if (callback)
        lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, reinterpret_cast<void *>(data));
    return object;
}

namespace {
void switch_view(lv_event_t *event) {
    state.view = static_cast<View>(reinterpret_cast<std::intptr_t>(lv_event_get_user_data(event)));
    state.selected_event = -1;
    state.selected_route = -1;
    state.detail_page = 0;
    lv_obj_scroll_to_y(state.agenda, 0, LV_ANIM_OFF);
    navigate();
}

void today(lv_event_t *) {
    if (state.view == View::traffic)
        state.view = View::agenda;
    const int day =
        state.health.time_trusted ? current_day(*state.snapshot, std::time(nullptr)) : -1;
    if (day >= 0)
        state.selected_day = state.month_day = day;
    state.selected_event = -1;
    state.detail_page = 0;
    navigate();
}

void sync(lv_event_t *) {
    services::request(*state.runtime, services::Command::sync);
    state.redraw = true;
}

void show_update(lv_event_t *) {
    state.update_open = true;
    state.redraw = true;
}

void install(lv_event_t *) {
    if (services::request(*state.runtime, services::Command::update)) {
        // Immediate feedback while the worker finishes its current operation.
        state.health.update_stage = services::UpdateStage::checking;
        lv_obj_add_state(state.update_confirm, LV_STATE_DISABLED);
    }
    state.redraw = true;
}

void close_update(lv_event_t *) {
    state.update_open = false;
    state.redraw = true;
}

void temperature_card(lv_obj_t *screen, int x, const char *title, std::uint32_t background,
                      lv_obj_t *&value, lv_obj_t *&note) {
    auto *card = lv_obj_create(screen);
    lv_obj_set_pos(card, x, 389);
    lv_obj_set_size(card, 194, 82);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(background), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_radius(card, 12, 0);
    auto *heading = label(card, title, 174);
    lv_obj_set_style_text_font(heading, &pace_font_16, 0);
    value = label(card, "--", 174);
    lv_obj_set_pos(value, 0, 18);
    lv_obj_set_style_text_font(value, &pace_font_32, 0);
    note = label(card, "", 174);
    lv_obj_set_pos(note, 0, 54);
    lv_obj_set_height(note, 20);
    lv_obj_set_style_text_font(note, &pace_font_16, 0);
}
} // namespace

void build_controls(lv_obj_t *screen) {
    state.heading = label(screen, "PACE", 324);
    lv_obj_set_pos(state.heading, 16, 10);
    state.network_icon = label(screen, "", 56);
    lv_obj_set_pos(state.network_icon, 342, 18);
    lv_obj_set_style_text_font(state.network_icon, &pace_font_16, 0);
    state.sync_button = button(screen, tr("Sync now", "Synchronisieren"), 150, 40, sync);
    lv_obj_set_pos(state.sync_button, 402, 6);
    state.sync_label = lv_obj_get_child(state.sync_button, 0);
    auto *update = button(screen, tr("Update", "Update"), 80, 40, show_update);
    lv_obj_set_pos(update, 560, 6);
    auto *settings = button(screen, tr("Settings", "Einstellungen"), 136, 40, nullptr);
    state.settings_button = settings;
    lv_obj_set_pos(settings, 648, 6);
    lv_obj_add_event_cb(
        settings, [](lv_event_t *) { services::request(*state.runtime, services::Command::setup); },
        LV_EVENT_LONG_PRESSED, nullptr);
    lv_obj_add_event_cb(settings, show_brightness, LV_EVENT_SHORT_CLICKED, nullptr);
    const char *names[] = {tr("Calendar", "Kalender"), tr("Day", "Tag"), tr("Month", "Monat"),
                           tr("Traffic", "Verkehr")};
    const int widths[] = {104, 60, 70, 86};
    const int positions[] = {16, 126, 192, 268};
    for (unsigned i = 0; i < 4; ++i) {
        state.tabs[i] = button(screen, names[i], widths[i], 38, switch_view, i);
        lv_obj_set_pos(state.tabs[i], positions[i], 51);
    }
    auto *now = button(screen, tr("Today", "Heute"), 68, 38, today);
    lv_obj_set_pos(now, 364, 51);
    state.next_label = label(screen, "", 344);
    lv_obj_set_pos(state.next_label, 442, 52);
    lv_obj_set_height(state.next_label, 38);
    lv_obj_set_style_text_font(state.next_label, &pace_font_16, 0);
    lv_obj_add_flag(state.next_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        state.next_label,
        [](lv_event_t *) {
            if (state.next_event >= 0) {
                if (state.view == View::traffic)
                    state.view = View::agenda;
                state.selected_event = state.next_event;
                navigate();
            }
        },
        LV_EVENT_CLICKED, nullptr);
    state.agenda = lv_obj_create(screen);
    lv_obj_set_pos(state.agenda, 12, 94);
    lv_obj_set_size(state.agenda, 776, 287);
    lv_obj_set_style_pad_all(state.agenda, 4, 0);
    lv_obj_set_style_pad_row(state.agenda, 3, 0);
    lv_obj_set_style_border_width(state.agenda, 0, 0);
    temperature_card(screen, 16, tr("INSIDE", "INNEN"), 0xf9efe2, state.inside_value,
                     state.inside_note);
    temperature_card(screen, 218, tr("OUTSIDE · MODEL", "AUSSEN · MODELL"), 0xe7f1f5,
                     state.outside_value, state.outside_note);
    state.footer = label(screen, "", 366);
    lv_obj_set_pos(state.footer, 426, 394);
    lv_obj_set_height(state.footer, 80);
    lv_obj_set_style_text_font(state.footer, &pace_font_16, 0);
    lv_label_set_long_mode(state.footer, LV_LABEL_LONG_WRAP);

    state.update_panel = lv_obj_create(screen);
    lv_obj_set_size(state.update_panel, 700, 290);
    lv_obj_center(state.update_panel);
    state.update_label = label(state.update_panel, "", 650);
    lv_label_set_long_mode(state.update_label, LV_LABEL_LONG_WRAP);
    state.update_confirm =
        button(state.update_panel, tr("Install update", "Update installieren"), 250, 48, install);
    lv_obj_set_pos(state.update_confirm, 0, 195);
    state.update_close =
        button(state.update_panel, tr("Close", "Schließen"), 180, 48, close_update);
    lv_obj_set_pos(state.update_close, 460, 195);
    lv_obj_add_flag(state.update_panel, LV_OBJ_FLAG_HIDDEN);

    state.setup_panel = lv_obj_create(screen);
    lv_obj_set_size(state.setup_panel, 760, 330);
    lv_obj_center(state.setup_panel);
    state.setup_label = label(state.setup_panel, "", 480);
    lv_obj_set_pos(state.setup_label, 8, 8);
    lv_label_set_long_mode(state.setup_label, LV_LABEL_LONG_WRAP);
    state.qr = lv_qrcode_create(state.setup_panel);
    lv_qrcode_set_size(state.qr, 190);
    lv_obj_set_pos(state.qr, 520, 12);
    auto *close = button(
        state.setup_panel, tr("Close setup", "Einrichtung schließen"), 250, 45,
        [](lv_event_t *) { services::request(*state.runtime, services::Command::close_setup); });
    lv_obj_set_pos(close, 8, 235);
    lv_obj_add_flag(state.setup_panel, LV_OBJ_FLAG_HIDDEN);
    build_brightness(screen);
}
} // namespace pace::ui_detail
