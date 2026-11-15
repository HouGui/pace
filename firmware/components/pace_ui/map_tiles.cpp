// SPDX-License-Identifier: MIT
#include "ui_state.hpp"
#define LODEPNG_NO_COMPILE_CPP
#include "src/libs/lodepng/lodepng.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <algorithm>
#include <cstring>

namespace pace::ui_detail {
namespace {
bool decode_tile(unsigned index) {
    const auto &frame = *state.map_frame;
    const auto &tile = frame.tiles[index];
    if (!services::png_tile(tile.bytes.data(), tile.size))
        return false;
    LodePNGState decoder;
    lodepng_state_init(&decoder);
    decoder.info_raw.colortype = LCT_RGBA;
    decoder.info_raw.bitdepth = 8;
    decoder.decoder.read_text_chunks = 0;
    decoder.decoder.remember_unknown_chunks = 0;
    decoder.decoder.zlibsettings.max_output_size = 300000;
    unsigned char *raw = nullptr;
    unsigned width = 0, height = 0;
    const auto error =
        lodepng_decode(&raw, &width, &height, &decoder, tile.bytes.data(), tile.size);
    lodepng_state_cleanup(&decoder);
    // LVGL's pinned LodePNG integration returns a draw buffer, not a pixel allocation.
    auto *decoded = reinterpret_cast<lv_draw_buf_t *>(raw);
    if (error || !decoded || width != 256 || height != 256) {
        if (decoded)
            lv_draw_buf_destroy(decoded);
        return false;
    }
    const auto &view = frame.view;
    const int left = (view.first_x + static_cast<int>(index) % view.columns) * 256 - view.left;
    const int top = (view.first_y + static_cast<int>(index) / view.columns) * 256 - view.top;
    for (int y = std::max(0, top); y < std::min(kMapHeight, top + 256); ++y) {
        const auto *source = decoded->data + (y - top) * decoded->header.stride;
        auto *target = reinterpret_cast<std::uint16_t *>(state.map_pixels->data +
                                                         y * state.map_pixels->header.stride);
        for (int x = std::max(0, left); x < std::min(kMapWidth, left + 256); ++x) {
            const auto *rgba = source + (x - left) * 4;
            const unsigned alpha = rgba[3];
            const unsigned red = (rgba[0] * alpha + 255 * (255 - alpha)) / 255;
            const unsigned green = (rgba[1] * alpha + 255 * (255 - alpha)) / 255;
            const unsigned blue = (rgba[2] * alpha + 255 * (255 - alpha)) / 255;
            target[x] = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
        }
    }
    lv_draw_buf_destroy(decoded);
    return true;
}

// Flatten the fixed route once so scrolling only copies the RGB565 image.
void draw_route() {
    const auto &frame = *state.map_frame;
    auto *canvas = lv_canvas_create(lv_screen_active());
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    lv_canvas_set_draw_buf(canvas, state.map_pixels);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(0x1454c4);
    line.width = 4;
    line.round_start = line.round_end = true;
    auto previous = map_point(frame.view, frame.geometry.points[0]);
    for (unsigned i = 1; i < frame.geometry.count; ++i) {
        const auto next = map_point(frame.view, frame.geometry.points[i]);
        if (next.x == previous.x && next.y == previous.y)
            continue;
        line.p1 = {static_cast<lv_value_precise_t>(previous.x),
                   static_cast<lv_value_precise_t>(previous.y)};
        line.p2 = {static_cast<lv_value_precise_t>(next.x),
                   static_cast<lv_value_precise_t>(next.y)};
        lv_draw_line(&layer, &line);
        // Dispatch each segment to bound temporary draw-task memory.
        lv_canvas_finish_layer(canvas, &layer);
        previous = next;
    }
    lv_obj_delete(canvas);
}
} // namespace

bool update_route_map(std::int64_t mono) {
    auto &runtime = *state.runtime;
    int wanted = state.view == View::traffic && !state.health.setup && !state.health.updating &&
                         !state.update_open && !state.brightness_open && state.health.network &&
                         state.health.time_trusted &&
                         runtime.settings.traffic.provider == RoutingProvider::open
                     ? state.selected_route
                     : -1;
    if (wanted >= static_cast<int>(runtime.settings.traffic.count) ||
        (wanted >= 0 && !route_current(state.health.routes[wanted], mono)))
        wanted = -1;
    runtime.map_requested = wanted;
    bool changed = false;
    const auto current = [&](const services::MapFrame *frame) {
        return wanted >= 0 && frame->route == wanted &&
               frame->route_fetched_ms == state.health.routes[wanted].fetched_at_monotonic_ms;
    };
    if (state.map_frame && !current(state.map_frame)) {
        // Delete image users before releasing data or changing the composite buffer.
        lv_obj_clean(state.agenda);
        if (state.map_pixels)
            lv_image_cache_drop(state.map_pixels);
        xQueueSend(runtime.map_available, &state.map_frame, 0);
        state.map_frame = nullptr;
        state.map_decoded = 0;
        state.map_failed = false;
        changed = true;
    }
    if (!state.map_frame && runtime.map_completed) {
        services::MapFrame *frame = nullptr;
        if (xQueueReceive(runtime.map_completed, &frame, 0) == pdTRUE) {
            if (!current(frame))
                xQueueSend(runtime.map_available, &frame, 0);
            else {
                state.map_frame = frame;
                state.map_failed = !frame->ready;
                if (frame->ready && state.map_pixels && state.map_cached_route == frame->route &&
                    state.map_cached_fetched_ms == frame->route_fetched_ms)
                    state.map_decoded = frame->view.columns * frame->view.rows;
                changed = true;
            }
        }
    }
    if (!state.map_frame || state.map_failed)
        return changed;
    const unsigned count = state.map_frame->view.columns * state.map_frame->view.rows;
    if (state.map_decoded >= count)
        return changed;
    // A new composition replaces the sole decoded cache; failed/partial images cannot be reused.
    state.map_cached_route = -1;
    if (!state.map_pixels)
        state.map_pixels =
            lv_draw_buf_create(kMapWidth, kMapHeight, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    // Decode one bounded tile per UI iteration, then return to touch/event handling.
    if (!state.map_pixels || !decode_tile(state.map_decoded++)) {
        state.map_failed = true;
        return true;
    }
    if (state.map_decoded == count) {
        draw_route();
        state.map_cached_route = state.map_frame->route;
        state.map_cached_fetched_ms = state.map_frame->route_fetched_ms;
    }
    return changed || state.map_decoded == count;
}
} // namespace pace::ui_detail
