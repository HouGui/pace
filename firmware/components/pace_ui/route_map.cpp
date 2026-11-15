// SPDX-License-Identifier: MIT
#include "ui_state.hpp"

namespace pace::ui_detail {
void make_route_map(lv_obj_t *parent) {
    const auto *frame = state.map_frame;
    const bool open = state.runtime->settings.traffic.provider == RoutingProvider::open;
    const char *message =
        !open ? tr("Street maps require Open routing", "Straßenkarten benötigen offenes Routing")
        : !state.runtime->map_completed || state.map_failed
            ? tr("Street map unavailable", "Straßenkarte nicht verfügbar")
            : tr("Loading street map...", "Straßenkarte wird geladen...");
    if (!open || !frame || state.map_failed || !frame->ready ||
        state.map_decoded < static_cast<unsigned>(frame->view.columns * frame->view.rows)) {
        auto *note = label(parent, message, kMapWidth);
        lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(note, &pace_font_16, 0);
        return;
    }
    auto *map = lv_obj_create(parent);
    lv_obj_set_size(map, kMapWidth, kMapHeight);
    lv_obj_set_style_pad_all(map, 0, 0);
    lv_obj_set_style_border_width(map, 0, 0);
    lv_obj_set_style_radius(map, 0, 0);
    lv_obj_remove_flag(map, LV_OBJ_FLAG_SCROLLABLE);
    auto *background = lv_image_create(map);
    lv_image_set_src(background, state.map_pixels);
    for (const unsigned i : {0u, frame->geometry.count - 1}) {
        auto *marker = lv_obj_create(map);
        lv_obj_set_size(marker, 20, 20);
        const auto point = map_point(frame->view, frame->geometry.points[i]);
        lv_obj_set_pos(marker, point.x - 10, point.y - 10);
        lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(marker, 0, 0);
        lv_obj_set_style_border_width(marker, 0, 0);
        lv_obj_set_style_bg_color(marker, lv_color_hex(i ? 0x7251a6 : 0x145b52), 0);
        lv_obj_set_style_text_color(marker, lv_color_hex(0xffffff), 0);
        lv_obj_remove_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
        auto *letter = label(marker, i ? "B" : "A", 18);
        lv_obj_set_style_text_font(letter, &pace_font_16, 0);
        lv_obj_set_style_text_align(letter, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(letter);
    }
    // Attribution stays visible with the map, including on the initial unscrolled view.
    auto *credit = label(map, "© OpenStreetMap contributors", kMapWidth);
    lv_obj_set_style_text_font(credit, &pace_font_16, 0);
    lv_obj_set_style_bg_color(credit, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(credit, LV_OPA_90, 0);
    lv_obj_align(credit, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    auto *caption = label(parent,
                          frame->geometry.schematic
                              ? tr("Transit · approximate line", "ÖPNV · schematische Linie")
                              : tr("A: start   B: destination", "A: Start   B: Ziel"),
                          kMapWidth);
    lv_obj_set_style_text_font(caption, &pace_font_16, 0);
}
} // namespace pace::ui_detail
