// SPDX-License-Identifier: MIT
#pragma once
#include "esp_lcd_types.h"
#include "lvgl.h"
#include "pace_ui.hpp"

LV_FONT_DECLARE(pace_font_16);
LV_FONT_DECLARE(pace_font_24);
LV_FONT_DECLARE(pace_font_32);

namespace pace::ui_detail {
enum class View { agenda, day, month, traffic };

// One instance, owned exclusively by run_ui and its LVGL callbacks.
struct State {
    services::Runtime *runtime{};
    services::Health health{};
    Snapshot *snapshot{};
    esp_lcd_panel_handle_t panel{};
    lv_obj_t *heading{}, *next_label{}, *agenda{}, *footer{}, *setup_panel{}, *setup_label{}, *qr{};
    lv_obj_t *inside_value{}, *inside_note{}, *outside_value{}, *outside_note{};
    lv_obj_t *sync_button{}, *sync_label{}, *update_panel{}, *update_label{}, *update_confirm{},
        *update_close{};
    lv_obj_t *network_icon{};
    lv_obj_t *settings_button{}, *brightness_panel{}, *brightness_slider{}, *brightness_value{},
        *dimmer{};
    unsigned brightness{100};
    bool brightness_open{};
    std::array<lv_obj_t *, 4> tabs{};
    TemperatureSample inside{};
    std::int64_t last_touch{};
    View view{View::agenda};
    int selected_day{}, month_day{}, selected_event{-1}, next_event{-1};
    int selected_route{-1};
    unsigned detail_page{};
    services::MapFrame *map_frame{};
    lv_draw_buf_t *map_pixels{};
    unsigned map_decoded{};
    bool map_failed{};
    int map_cached_route{-1};
    std::int64_t map_cached_fetched_ms{};
    bool redraw{true}, calendar_redraw{true}, backlight{true}, suppress_touch{}, touch_fault{},
        display_fault{};
    bool unsupported_font{}, sensor_fault{}, update_open{};
};

extern State state;
const char *tr(const char *english, const char *german);
lv_obj_t *label(lv_obj_t *parent, const char *text, int width);
lv_obj_t *button(lv_obj_t *parent, const char *text, int width, int height, lv_event_cb_t callback,
                 std::intptr_t data = 0);
void check_font(const char *text);
void navigate();
void build_controls(lv_obj_t *screen);
void build_brightness(lv_obj_t *screen);
void show_brightness(lv_event_t *event);
void render_brightness();
void open_day(lv_event_t *event);
void open_event(lv_event_t *event);
void event_text(const Event &event, char *text, std::size_t size, UtcSeconds now, int day = -1,
                bool detail = false);
std::uint32_t color(const CategoryId &id);
std::uint32_t event_color(const Event &event);
const char *secondary_name();
void make_agenda(UtcSeconds now);
void make_month(int today);
void make_traffic(std::int64_t mono);
bool update_route_map(std::int64_t mono);
void make_route_map(lv_obj_t *parent);
void render(UtcSeconds now, std::int64_t mono);
} // namespace pace::ui_detail
