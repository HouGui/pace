// SPDX-License-Identifier: MIT
#include "display_transform.hpp"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "pace/core.hpp"
#include "pace_board.h"
#include "pace_board_driver.h"
#include "sdkconfig.h"
#include "ui_state.hpp"
#include <ctime>

namespace pace::ui_detail {
State state;

std::uint32_t tick() { return static_cast<std::uint32_t>(esp_timer_get_time() / 1000); }

void flush(lv_display_t *display, const lv_area_t *area, std::uint8_t *pixels) {
    // The RGB driver copies a partial draw buffer synchronously to its framebuffer.
    auto physical = *area;
    if (lv_display_get_rotation(display) == LV_DISPLAY_ROTATION_180) {
        rotate_rgb565_180(pixels, lv_area_get_width(area) * lv_area_get_height(area));
        lv_display_rotate_area(display, &physical);
    }
    if (esp_lcd_panel_draw_bitmap(state.panel, physical.x1, physical.y1, physical.x2 + 1,
                                  physical.y2 + 1, pixels) != ESP_OK)
        state.display_fault = true;
    lv_display_flush_ready(display);
}

void touch(lv_indev_t *, lv_indev_data_t *data) {
    std::uint16_t x = 0, y = 0;
    bool down = false;
    state.touch_fault = pace_board_touch(&x, &y, &down) != ESP_OK;
    if (down) {
        state.last_touch = esp_timer_get_time() / 1000;
        if (!state.backlight) {
            if (pace_board_backlight(true) == ESP_OK)
                state.backlight = true;
            state.suppress_touch = true;
        }
    } else
        state.suppress_touch = false;
    data->point.x = x;
    data->point.y = y;
    data->state = down && !state.suppress_touch ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

} // namespace pace::ui_detail

namespace pace {
using namespace ui_detail;

void run_ui(services::Runtime &r) {
    esp_task_wdt_add(nullptr);
    state.runtime = &r;
    state.snapshot = r.initial;
    auto error = pace_board_init(&state.panel);
    if (error != ESP_OK) {
        ESP_LOGE("ui", "Board initialization failed: %s", esp_err_to_name(error));
        services::display_ready(false);
        for (;;) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    lv_init();
    lv_tick_set_cb(tick);
    auto *display = lv_display_create(PACE_DISPLAY_WIDTH, PACE_DISPLAY_HEIGHT);
    auto *buffer =
        heap_caps_malloc(PACE_DISPLAY_WIDTH * 40 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!display || !buffer) {
        ESP_LOGE("ui", "Display allocation failed");
        return;
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
#if CONFIG_PACE_GUI_ROTATE_180
    // LVGL also transforms raw touch coordinates using this same rotation.
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_180);
#endif
    lv_display_set_buffers(display, buffer, nullptr, PACE_DISPLAY_WIDTH * 40 * 2,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    auto *input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input, touch);
    lv_indev_set_long_press_time(input, 2000);
    auto *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0x1d303b), 0);
    lv_obj_set_style_text_font(screen, &pace_font_24, 0);
    build_controls(screen);
    std::int64_t next_sensor = 0, last_minute = -1;
    static services::Health previous; // UI-owned; route previews do not belong on the task stack.
    for (;;) {
        esp_task_wdt_reset();
        const auto mono = esp_timer_get_time() / 1000;
        const UtcSeconds now = std::time(nullptr);
        if (xQueuePeek(r.health, &state.health, 0) == pdTRUE) {
            if (state.view == View::traffic &&
                (state.health.traffic_revision != previous.traffic_revision ||
                 state.health.network != previous.network))
                state.redraw = state.calendar_redraw = true;
            if (state.health.network != previous.network || state.health.setup != previous.setup ||
                state.health.time_trusted != previous.time_trusted ||
                state.health.calendar != previous.calendar ||
                state.health.updating != previous.updating ||
                state.health.outside.source_time_utc != previous.outside.source_time_utc ||
                state.health.notice != previous.notice ||
                state.health.syncing != previous.syncing ||
                state.health.sync_attempt != previous.sync_attempt ||
                state.health.sync_pending != previous.sync_pending ||
                state.health.update_stage != previous.update_stage ||
                state.health.update_percent != previous.update_percent ||
                state.health.cache_error != previous.cache_error ||
                state.health.brightness_error != previous.brightness_error)
                state.redraw = true;
            if (state.health.time_trusted != previous.time_trusted)
                state.calendar_redraw = true;
            previous = state.health;
        }
        Snapshot *fresh = nullptr;
        if (xQueueReceive(r.completed, &fresh, 0) == pdTRUE) {
            auto *old = state.snapshot;
            const auto day = old->days[state.selected_day].date;
            const auto month = old->days[state.month_day].date;
            const auto event =
                state.selected_event >= 0 ? old->events[state.selected_event].id : Event{}.id;
            state.snapshot = fresh;
            const int today = state.health.time_trusted ? current_day(*fresh, now) : -1;
            state.selected_day = state.month_day = today < 0 ? 0 : today;
            state.selected_event = -1;
            for (unsigned i = 0; i < fresh->day_count; ++i) {
                if (fresh->days[i].date == day)
                    state.selected_day = i;
                if (fresh->days[i].date == month)
                    state.month_day = i;
            }
            for (unsigned i = 0; i < fresh->event_count; ++i)
                if (fresh->events[i].id == event)
                    state.selected_event = i;
            xQueueSend(r.available, &old, 0);
            state.redraw = state.calendar_redraw = true;
        }
        if (mono >= next_sensor) {
            std::array<std::uint8_t, 6> bytes{};
            float value;
            state.sensor_fault =
                pace_board_sensor(bytes.data()) != ESP_OK || !decode_sht40(bytes, value);
            if (!state.sensor_fault)
                state.inside = {value, mono, true};
            next_sensor = mono + 30000;
            state.redraw = true;
        }
        if ((state.view != View::agenda || state.selected_event >= 0 || state.brightness_open) &&
            !state.update_open && mono - state.last_touch >= 60000) {
            if (state.brightness_open) {
                state.brightness_open = false;
                state.brightness = state.runtime->brightness;
            }
            state.view = View::agenda;
            state.selected_event = -1;
            state.redraw = state.calendar_redraw = true;
        }
        const bool blank =
            state.runtime->settings.night_enabled && !state.health.setup &&
            !state.health.updating && !state.update_open && !state.brightness_open &&
            state.health.time_trusted &&
            night_active(local_minute(*state.snapshot, now), state.runtime->settings.night_start,
                         state.runtime->settings.night_end) &&
            mono - state.last_touch >= 120000;
        if (state.backlight == blank && pace_board_backlight(!blank) == ESP_OK)
            state.backlight = !blank;
        if (now / 60 != last_minute) {
            last_minute = now / 60;
            state.redraw = state.calendar_redraw = true;
        }
        if (update_route_map(mono))
            state.redraw = state.calendar_redraw = true;
        if (state.redraw) {
            render(now, mono);
            state.redraw = false;
        }
        lv_timer_handler();
        services::display_ready(!state.display_fault);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
} // namespace pace
