// SPDX-License-Identifier: MIT
#include "display_transform.hpp"
#include "esp_app_desc.h"
#include "pace/calendar_view.hpp"
#include "pace/core.hpp"
#include "ui_state.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pace::ui_detail {
State state;
}

namespace {
unsigned checks{};
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                                        \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
pace::Snapshot snapshot;
pace::services::Runtime runtime;
esp_app_desc_t description;
pace::services::Command requested;
const char *output;
pace::services::MapFrame map_frame;
pace::services::MapFrame *map_available{}, *map_completed{};

lv_obj_t *find_text(lv_obj_t *parent, const char *text) {
    if (lv_obj_check_type(parent, &lv_label_class) && !std::strcmp(lv_label_get_text(parent), text))
        return parent;
    for (unsigned i = 0; i < lv_obj_get_child_count(parent); ++i)
        if (auto *found = find_text(lv_obj_get_child(parent, i), text))
            return found;
    return nullptr;
}

void screenshot(const char *name) {
    if (!output)
        return;
    auto *buffer = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    CHECK(buffer);
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s.ppm", output, name);
    auto *file = std::fopen(path, "wb");
    CHECK(file);
    std::fprintf(file, "P6\n%u %u\n255\n", buffer->header.w, buffer->header.h);
    for (unsigned y = 0; y < buffer->header.h; ++y) {
        const auto *row = buffer->data + y * buffer->header.stride;
        for (unsigned x = 0; x < buffer->header.w; ++x) {
            const unsigned char rgb[] = {row[x * 3 + 2], row[x * 3 + 1], row[x * 3]};
            std::fwrite(rgb, 1, 3, file);
        }
    }
    std::fclose(file);
    lv_draw_buf_destroy(buffer);
}
} // namespace

int xQueueSend(QueueHandle_t queue, const void *item, TickType_t) {
    auto &slot = *static_cast<pace::services::MapFrame **>(queue);
    if (slot)
        return pdFALSE;
    slot = *static_cast<pace::services::MapFrame *const *>(item);
    return pdTRUE;
}

int xQueueReceive(QueueHandle_t queue, void *item, TickType_t) {
    auto &slot = *static_cast<pace::services::MapFrame **>(queue);
    if (!slot)
        return pdFALSE;
    *static_cast<pace::services::MapFrame **>(item) = slot;
    slot = nullptr;
    return pdTRUE;
}

std::int64_t esp_timer_get_time() { return 60000000; }

const esp_app_desc_t *esp_app_get_description() { return &description; }

namespace pace::services {
bool request(Runtime &r, Command command) {
    requested = command;
    if (command == Command::sync)
        r.sync_requested = true;
    if (command == Command::update)
        r.update_requested = true;
    return true;
}

bool update_available() { return true; }

bool update_source_configured(const char *url) { return url && url[0]; }
} // namespace pace::services

int main(int argc, char **argv) {
    using namespace pace;
    using namespace pace::ui_detail;
    output = argc > 1 ? argv[1] : nullptr;
    std::strcpy(description.version, "0.3.0-dev");
    lv_init();
    auto *display = lv_display_create(800, 480);
    auto *screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, &pace_font_24, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0x1d303b), 0);
    runtime.configured = true;
    state.runtime = &runtime;
    state.snapshot = &snapshot;
    snapshot.day_count = 31;
    for (unsigned i = 0; i < 31; ++i) {
        auto &day = snapshot.days[i];
        std::snprintf(day.date.data(), day.date.size(), "2030-03-%02u", i + 1);
        day.start_utc = date_number(day.date.data()) * 86400;
        day.end_utc = day.start_utc + 86400;
        date_label(day.date, Language::german, day.label.data(), day.label.size());
    }
    snapshot.clock_count = 1;
    snapshot.clock[0] = {snapshot.days[0].start_utc, snapshot.days[30].end_utc, 0};
    snapshot.generated_at_utc = snapshot.days[7].start_utc + 36000;
    snapshot.event_count = 2;
    snapshot.events[0].interval =
        TimedInterval{snapshot.generated_at_utc + 1800, snapshot.generated_at_utc + 3600, {}, {}};
    auto &timed = std::get<TimedInterval>(snapshot.events[0].interval);
    std::strcpy(timed.start_label.data(), "10:30 GMT+0");
    std::strcpy(timed.end_label.data(), "11:00 GMT+0");
    std::strcpy(snapshot.events[0].title.data(), "Bürobesuch – Planung");
    std::strcpy(snapshot.events[0].location.data(), "München · Großes Büro, Straße 12");
    snapshot.events[1].interval = AllDayInterval{snapshot.days[8].date, snapshot.days[9].date};
    std::strcpy(snapshot.events[1].title.data(), "Family day");
    state.selected_day = state.month_day = 7;
    state.health.network = state.health.time_trusted = true;
    state.health.calendar = FeedResult::ok;
    state.inside = {21.5f, 60000, true};
    state.health.outside = {12.3f, snapshot.generated_at_utc, 60000, true};
    runtime.settings.weather_enabled = true;
    std::strcpy(runtime.settings.location.data(), "München");
    build_controls(screen);
    const auto render_now = [&] {
        render(snapshot.generated_at_utc, 60000);
        lv_obj_update_layout(screen);
    };
    render_now();
    CHECK(!state.unsupported_font);
    CHECK(find_text(state.agenda, "Next | 10:30  Bürobesuch – Planung"));
    CHECK(std::strstr(lv_label_get_text(state.heading), "GMT+00:00"));
    CHECK(!std::strstr(lv_label_get_text(state.next_label), "GMT"));
    screenshot("agenda-en");
    lv_obj_send_event(state.settings_button, LV_EVENT_SHORT_CLICKED, nullptr);
    render_now();
    CHECK(state.brightness_open);
    lv_slider_set_value(state.brightness_slider, 40, LV_ANIM_OFF);
    lv_obj_send_event(state.brightness_slider, LV_EVENT_VALUE_CHANGED, nullptr);
    render_now();
    CHECK(state.brightness == 40 && !lv_obj_has_flag(state.dimmer, LV_OBJ_FLAG_CLICKABLE));
    CHECK(lv_obj_get_style_bg_opa(state.dimmer, LV_PART_MAIN) == 153);
    screenshot("brightness-en");
    auto *save_brightness = find_text(state.brightness_panel, "Save");
    CHECK(save_brightness);
    lv_obj_send_event(lv_obj_get_parent(save_brightness), LV_EVENT_CLICKED, nullptr);
    CHECK(!state.brightness_open && runtime.brightness_pending == 40);
    state.health.setup = true;
    render_now();
    CHECK(lv_obj_has_flag(state.dimmer, LV_OBJ_FLAG_HIDDEN));
    CHECK(std::strstr(lv_label_get_text(state.setup_label), "15 minutes idle"));
    state.health.setup = false;
    state.brightness = 100;
    lv_obj_send_event(state.settings_button, LV_EVENT_LONG_PRESSED, nullptr);
    CHECK(requested == services::Command::setup && !state.brightness_open);
    lv_obj_send_event(state.sync_button, LV_EVENT_CLICKED, nullptr);
    CHECK(requested == services::Command::sync && runtime.sync_requested);
    render_now();
    CHECK(lv_obj_has_state(state.sync_button, LV_STATE_DISABLED));
    runtime.sync_requested = false;
    lv_obj_send_event(state.tabs[1], LV_EVENT_CLICKED, nullptr);
    render_now();
    CHECK(state.view == View::day);
    auto *appointment =
        find_text(state.agenda, "Next | 10:30 GMT+0 - 11:00 GMT+0  Bürobesuch – Planung");
    CHECK(appointment);
    lv_obj_send_event(lv_obj_get_parent(appointment), LV_EVENT_CLICKED, nullptr);
    render_now();
    CHECK(find_text(state.agenda, "München · Großes Büro, Straße 12"));
    screenshot("appointment-en");
    // Recreate controls as on reboot after an immutable language setting changes.
    lv_obj_clean(screen);
    runtime.settings.language = Language::german;
    state.selected_event = -1;
    state.view = View::month;
    state.calendar_redraw = true;
    build_controls(screen);
    render_now();
    CHECK(find_text(screen, "März 2030"));
    CHECK(find_text(screen, "21,5 °C"));
    CHECK(!state.unsupported_font);
    screenshot("month-de");
    auto *cell = find_text(state.agenda, "8  (1)");
    CHECK(cell);
    lv_obj_send_event(lv_obj_get_parent(cell), LV_EVENT_CLICKED, nullptr);
    render_now();
    CHECK(state.view == View::day && state.selected_day == 7);
    screenshot("day-de");
    CHECK(!std::strcmp(lv_label_get_text(state.outside_note), "München"));
    state.health.outside.source_time_utc = snapshot.generated_at_utc - 3600;
    render_now();
    CHECK(!std::strcmp(lv_label_get_text(state.outside_note), "München"));
    CHECK(std::strstr(lv_label_get_text(state.footer), "veraltet"));
    state.health.outside.source_time_utc = snapshot.generated_at_utc - 21601;
    render_now();
    CHECK(!std::strcmp(lv_label_get_text(state.outside_value), "--"));
    CHECK(!std::strcmp(lv_label_get_text(state.outside_note), "München"));
    CHECK(state.snapshot->event_count == 2);
    state.health.outside.source_time_utc = snapshot.generated_at_utc;
    CHECK(!std::strcmp(lv_label_get_text(state.network_icon), LV_SYMBOL_WIFI " " LV_SYMBOL_OK));
    state.health.network = false;
    render_now();
    CHECK(!std::strcmp(lv_label_get_text(state.network_icon), LV_SYMBOL_WIFI " " LV_SYMBOL_CLOSE));
    state.health.network = true;
    lv_obj_send_event(state.tabs[3], LV_EVENT_CLICKED, nullptr);
    render_now();
    CHECK(state.view == View::traffic && runtime.traffic_visible);
    CHECK(find_text(state.agenda, "Verkehr · deine täglichen Wege"));
    runtime.settings.traffic.count = 4;
    runtime.settings.traffic.provider = RoutingProvider::google_routes;
    const char *names[] = {"Arbeit", "Schule", "Sport", "Bahnhof"};
    for (unsigned i = 0; i < 4; ++i) {
        auto &route = runtime.settings.traffic.routes[i];
        std::strcpy(route.name.data(), names[i]);
        std::strcpy(route.origin.data(), "Testplatz 1");
        std::strcpy(route.destination.data(), "Zielstraße 2");
        route.mode = i % 2 ? TravelMode::transit : TravelMode::car;
        auto &result = state.health.routes[i];
        result.status = RouteStatus::ok;
        result.seconds = (i + 1) * 600;
        result.meters = 12500;
        result.geometry.count = 3;
        result.geometry.points[0] = {48000000, 8000000};
        result.geometry.points[1] = {48100000, 8100000};
        result.geometry.points[2] = {48050000, 8200000};
        result.fetched_at_monotonic_ms = 60000;
        std::strcpy(result.summary.data(), i % 2 ? "S 1 > Bus 12" : "A 1");
    }
    state.calendar_redraw = true;
    render_now();
    CHECK(find_text(state.agenda, "40 min") && find_text(state.agenda, "Google Maps"));
    CHECK(!state.unsupported_font);
    auto *last_card = lv_obj_get_parent(find_text(state.agenda, "Bahnhof"));
    lv_area_t bounds{};
    lv_obj_get_coords(last_card, &bounds);
    CHECK(bounds.y2 < 381 && bounds.x2 < 788);
    screenshot("traffic-de");
    lv_obj_send_event(last_card, LV_EVENT_CLICKED, nullptr);
    render_now();
    CHECK(state.selected_route == 3);
    CHECK(find_text(state.agenda, "Testplatz 1 > Zielstraße 2"));
    CHECK(find_text(state.agenda, "Straßenkarten benötigen offenes Routing"));
    screenshot("traffic-detail-de");
    runtime.settings.traffic.provider = RoutingProvider::open;
    state.health.routes[3].geometry.schematic = true;
    runtime.map_available = &map_available;
    runtime.map_completed = &map_completed;
    map_frame.route = 3;
    map_frame.route_fetched_ms = state.health.routes[3].fetched_at_monotonic_ms;
    map_frame.geometry = state.health.routes[3].geometry;
    CHECK(map_viewport(map_frame.geometry, map_frame.view));
    auto *tile =
        std::fopen(argc > 2 ? argv[2] : PACE_SOURCE_DIR "/tests/fixtures/map-tile.png", "rb");
    CHECK(tile);
    map_frame.tiles[0].size =
        std::fread(map_frame.tiles[0].bytes.data(), 1, services::kMapTileBytes, tile);
    std::fclose(tile);
    for (unsigned i = 1; i < 4; ++i)
        map_frame.tiles[i] = map_frame.tiles[0];
    map_frame.ready = true;
    map_completed = &map_frame;
    for (unsigned i = 0; i < 4; ++i)
        update_route_map(60000);
    CHECK(!state.map_failed && state.map_pixels && state.map_decoded > 0);
    state.calendar_redraw = true;
    render_now();
    CHECK(find_text(state.agenda, "ÖPNV · schematische Linie"));
    auto *credit = find_text(state.agenda, "© OpenStreetMap contributors");
    CHECK(credit);
    lv_obj_get_coords(credit, &bounds);
    CHECK(bounds.y2 < 400 && bounds.x2 < 788);
    lv_point_t credit_size;
    lv_text_get_size(&credit_size, "© OpenStreetMap contributors", &pace_font_16, 0, 0, 1000,
                     LV_TEXT_FLAG_NONE);
    CHECK(credit_size.x <= kMapWidth);
    CHECK(find_text(state.agenda, "© OpenStreetMap · OSRM / MOTIS"));
    screenshot("open-transit-detail-de");
    state.health.routes[3].fetched_at_monotonic_ms = -300000;
    state.calendar_redraw = true;
    render_now();
    CHECK(!find_text(state.agenda, "40 min"));
    lv_obj_send_event(state.tabs[0], LV_EVENT_CLICKED, nullptr);
    render_now();
    CHECK(state.view == View::agenda && !runtime.traffic_visible);
    CHECK(snapshot.event_count == 2);
    update_route_map(60000);
    CHECK(!state.map_frame && map_available == &map_frame);
    const auto cached_pixels = state.map_pixels;
    // Reopening the same route reuses the complete decoded map in one UI iteration.
    map_available = nullptr;
    map_completed = &map_frame;
    state.selected_route = 3;
    state.view = View::traffic;
    state.health.routes[3].fetched_at_monotonic_ms = map_frame.route_fetched_ms;
    CHECK(update_route_map(60000));
    CHECK(state.map_decoded == static_cast<unsigned>(map_frame.view.columns * map_frame.view.rows));
    CHECK(state.map_pixels == cached_pixels && !state.map_failed);
    state.view = View::agenda;
    update_route_map(60000);
    // A new route result must invalidate the decoded cache.
    --map_frame.route_fetched_ms;
    // A malformed compressed stream must fail without replacing the usable route result.
    map_available = nullptr;
    map_completed = &map_frame;
    map_frame.tiles[0].bytes[45] ^= 1;
    state.selected_route = 3;
    state.view = View::traffic;
    state.health.routes[3].fetched_at_monotonic_ms = map_frame.route_fetched_ms;
    update_route_map(60000);
    CHECK(state.map_failed);
    state.view = View::agenda;
    update_route_map(60000);
    snapshot.secondary_configured = snapshot.secondary_available = true;
    std::strcpy(snapshot.secondary_label.data(), "Birthdays");
    snapshot.event_count = 3;
    snapshot.events[2].secondary = true;
    snapshot.events[2].interval = AllDayInterval{snapshot.days[7].date, snapshot.days[8].date};
    std::strcpy(snapshot.events[2].title.data(), "Birthday example");
    state.calendar_redraw = true;
    render_now();
    CHECK(state.next_event == 0);
    CHECK(find_text(state.agenda, "*1 Birthdays"));
    screenshot("secondary-week-de");
    state.view = View::day;
    state.selected_day = 7;
    state.calendar_redraw = true;
    render_now();
    CHECK(find_text(state.agenda, "Birthdays"));
    screenshot("secondary-day-de");
    state.view = View::month;
    state.calendar_redraw = true;
    render_now();
    CHECK(find_text(state.agenda, "*"));
    screenshot("secondary-month-de");
    snapshot.secondary_available = false;
    snapshot.event_count = 2;
    state.calendar_redraw = true;
    render_now();
    CHECK(std::strstr(lv_label_get_text(state.footer), "Zweiter Kalender nicht verfügbar"));
    snapshot.secondary_configured = false;
    state.view = View::agenda;
    state.calendar_redraw = true;
    render_now();
    CHECK(!find_text(state.agenda, "*1 Birthdays") && state.next_event == 0);
    state.update_open = true;
    render_now();
    CHECK(lv_obj_has_state(state.update_confirm, LV_STATE_DISABLED));
    screenshot("update-de");
    std::uint8_t pixels[] = {1, 11, 2, 12, 3, 13, 4, 14, 5, 15, 6, 16};
    rotate_rgb565_180(pixels, 6);
    CHECK(pixels[0] == 6 && pixels[1] == 16 && pixels[10] == 1 && pixels[11] == 11);
    rotate_rgb565_180(pixels, 6);
    CHECK(pixels[0] == 1 && pixels[11] == 16);
    std::uint8_t odd[] = {1, 2, 3, 4, 5, 6};
    rotate_rgb565_180(odd, 3);
    CHECK(odd[0] == 5 && odd[2] == 3 && odd[4] == 1);
    rotate_rgb565_180(nullptr, 0);
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_180);
    lv_area_t area{10, 20, 12, 21};
    lv_display_rotate_area(display, &area);
    CHECK(area.x1 == 787 && area.x2 == 789 && area.y1 == 458 && area.y2 == 459);
    auto *input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input, display);
    lv_indev_set_read_cb(input, [](lv_indev_t *, lv_indev_data_t *data) {
        data->point = {799, 479};
        data->state = LV_INDEV_STATE_RELEASED;
    });
    lv_indev_read(input);
    lv_point_t touch{};
    lv_indev_get_point(input, &touch);
    CHECK(touch.x == 0 && touch.y == 0);
    lv_indev_set_read_cb(input, [](lv_indev_t *, lv_indev_data_t *data) {
        data->point = {0, 0};
        data->state = LV_INDEV_STATE_RELEASED;
    });
    lv_indev_read(input);
    lv_indev_get_point(input, &touch);
    CHECK(touch.x == 799 && touch.y == 479);
    lv_indev_delete(input);
    lv_display_delete(display);
    if (state.map_pixels)
        lv_draw_buf_destroy(state.map_pixels);
    lv_deinit();
    std::printf("PASS: real LVGL controls, fonts and views (%u assertions)\n", checks);
}
