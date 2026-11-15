// SPDX-License-Identifier: MIT
#include "ui_state.hpp"
#include <cstdio>

namespace pace::ui_detail {
void show_brightness(lv_event_t *) {
    const unsigned pending = state.runtime->brightness_pending;
    state.brightness = pending ? pending : state.runtime->brightness.load();
    lv_slider_set_value(state.brightness_slider, state.brightness, LV_ANIM_OFF);
    state.brightness_open = true;
    navigate();
}

void render_brightness() {
    if (state.brightness_open &&
        (state.health.setup || state.health.updating || state.update_open)) {
        state.brightness_open = false;
        state.brightness = state.runtime->brightness;
    }
    if (state.brightness_open)
        lv_obj_remove_flag(state.brightness_panel, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(state.brightness_panel, LV_OBJ_FLAG_HIDDEN);
    const unsigned percent =
        state.health.setup || state.health.updating || state.update_open ? 100 : state.brightness;
    lv_obj_set_style_bg_opa(state.dimmer, (100 - percent) * LV_OPA_COVER / 100, 0);
    if (percent == 100)
        lv_obj_add_flag(state.dimmer, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(state.dimmer, LV_OBJ_FLAG_HIDDEN);
    char value[40];
    std::snprintf(value, sizeof(value), "%u %%", state.brightness);
    lv_label_set_text(state.brightness_value, value);
}

void build_brightness(lv_obj_t *screen) {
    state.brightness = state.runtime->brightness;
    state.brightness_panel = lv_obj_create(screen);
    lv_obj_set_size(state.brightness_panel, 800, 480);
    lv_obj_set_style_pad_all(state.brightness_panel, 0, 0);
    lv_obj_set_style_border_width(state.brightness_panel, 0, 0);
    lv_obj_set_style_radius(state.brightness_panel, 0, 0);
    lv_obj_set_style_bg_opa(state.brightness_panel, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(state.brightness_panel, LV_OBJ_FLAG_SCROLLABLE);
    auto *card = lv_obj_create(state.brightness_panel);
    lv_obj_set_size(card, 720, 310);
    lv_obj_center(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    label(card, tr("Screen brightness", "Bildschirmhelligkeit"), 650);
    state.brightness_value = label(card, "", 100);
    lv_obj_set_pos(state.brightness_value, 565, 0);
    auto *note = label(card,
                       tr("Software dimming · does not reduce backlight power. Hold Settings for "
                          "Wi-Fi and calendar setup.",
                          "Software-Abdunklung · senkt nicht die Leistung der "
                          "Hintergrundbeleuchtung. Einstellungen "
                          "für WLAN und Kalender gedrückt halten."),
                       650);
    lv_obj_set_style_text_font(note, &pace_font_16, 0);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(note, 0, 45);
    state.brightness_slider = lv_slider_create(card);
    lv_obj_set_size(state.brightness_slider, 600, 24);
    lv_obj_set_pos(state.brightness_slider, 30, 125);
    lv_slider_set_range(state.brightness_slider, 20, 100);
    lv_slider_set_value(state.brightness_slider, state.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(
        state.brightness_slider,
        [](lv_event_t *) {
            state.brightness = lv_slider_get_value(state.brightness_slider);
            state.redraw = true;
        },
        LV_EVENT_VALUE_CHANGED, nullptr);
    auto *save = button(card, tr("Save", "Speichern"), 190, 48, [](lv_event_t *) {
        state.runtime->brightness_pending = state.brightness;
        state.brightness_open = false;
        navigate();
    });
    lv_obj_set_pos(save, 0, 215);
    auto *cancel = button(card, tr("Cancel", "Abbrechen"), 190, 48, [](lv_event_t *) {
        state.brightness = state.runtime->brightness;
        state.brightness_open = false;
        navigate();
    });
    lv_obj_set_pos(cancel, 465, 215);
    lv_obj_add_flag(state.brightness_panel, LV_OBJ_FLAG_HIDDEN);
    state.dimmer = lv_obj_create(screen);
    lv_obj_set_pos(state.dimmer, 0, 0);
    lv_obj_set_size(state.dimmer, 800, 480);
    lv_obj_set_style_radius(state.dimmer, 0, 0);
    lv_obj_set_style_border_width(state.dimmer, 0, 0);
    lv_obj_set_style_bg_color(state.dimmer, lv_color_black(), 0);
    lv_obj_remove_flag(state.dimmer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(state.dimmer, LV_OBJ_FLAG_SCROLLABLE);
    render_brightness();
}
} // namespace pace::ui_detail
