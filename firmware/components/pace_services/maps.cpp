// SPDX-License-Identifier: MIT
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "pace/traffic_protocol.hpp"
#include "pace_http.hpp"
#include "services_internal.hpp"
#include <cstdio>
#include <cstring>
#include <new>
#include <string_view>

namespace pace::services {
namespace {
constexpr std::int64_t kTileLifetime = 7LL * 24 * 60 * 60 * 1000;

struct CachedTile {
    MapTile tile{};
    int x{}, y{}, zoom{-1};
    std::int64_t expires{}, used{};
};

} // namespace

struct MapWorker {
    std::array<CachedTile, 16> cache{};
    MapFrame storage{};
    MapFrame *frame{};
    unsigned next_tile{};
    std::int64_t next_request{};
};

void init_maps(Runtime &runtime) {
    if (!runtime.settings.traffic.count ||
        runtime.settings.traffic.provider != RoutingProvider::open)
        return;
    auto *memory = heap_caps_malloc(sizeof(MapWorker), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory)
        return;
    runtime.map_worker = new (memory) MapWorker;
    runtime.map_available = xQueueCreate(1, sizeof(MapFrame *));
    runtime.map_completed = xQueueCreate(1, sizeof(MapFrame *));
    if (!runtime.map_available || !runtime.map_completed) {
        if (runtime.map_available)
            vQueueDelete(runtime.map_available);
        if (runtime.map_completed)
            vQueueDelete(runtime.map_completed);
        runtime.map_available = runtime.map_completed = nullptr;
        runtime.map_worker->~MapWorker();
        heap_caps_free(memory);
        runtime.map_worker = nullptr;
        return;
    }
    auto *frame = &runtime.map_worker->storage;
    xQueueSend(runtime.map_available, &frame, 0);
}

void poll_maps(Runtime &runtime, const Health &health, std::int64_t mono) {
    auto *worker = runtime.map_worker;
    if (!worker || !runtime.map_available || !runtime.map_completed)
        return;
    const int wanted = runtime.map_requested;
    const bool visible = runtime.traffic_visible && wanted >= 0 &&
                         static_cast<unsigned>(wanted) < runtime.settings.traffic.count;
    if (!visible || !health.network || !health.time_trusted ||
        !route_current(health.routes[wanted], mono)) {
        if (worker->frame) {
            xQueueSend(runtime.map_available, &worker->frame, 0);
            worker->frame = nullptr;
        }
        return;
    }
    if (runtime.update_requested || runtime.sync_requested || mono < worker->next_request)
        return;
    if (!worker->frame) {
        if (xQueueReceive(runtime.map_available, &worker->frame, 0) != pdTRUE)
            return;
        worker->frame->route = -1;
    }
    auto &frame = *worker->frame;
    const auto &route = health.routes[wanted];
    if (frame.route != wanted || frame.route_fetched_ms != route.fetched_at_monotonic_ms) {
        frame.route = wanted;
        frame.route_fetched_ms = route.fetched_at_monotonic_ms;
        frame.geometry = route.geometry;
        frame.ready = false;
        worker->next_tile = 0;
        if (!map_viewport(frame.geometry, frame.view)) {
            xQueueSend(runtime.map_completed, &worker->frame, 0);
            worker->frame = nullptr;
            return;
        }
    }
    const auto index = worker->next_tile;
    const int x = wrapped_tile_x(frame.view.first_x + static_cast<int>(index) % frame.view.columns,
                                 frame.view.zoom);
    const int y = frame.view.first_y + static_cast<int>(index) / frame.view.columns;
    CachedTile *cached = nullptr, *oldest = &worker->cache[0];
    for (auto &entry : worker->cache) {
        if (entry.zoom == frame.view.zoom && entry.x == x && entry.y == y && mono < entry.expires)
            cached = &entry;
        if (entry.used < oldest->used)
            oldest = &entry;
    }
    if (!cached) {
        char url[384], agent[192];
        const auto &settings = runtime.settings.traffic;
        auto endpoint = std::string_view(settings.tile_endpoint.data());
        while (!endpoint.empty() && endpoint.back() == '/')
            endpoint.remove_suffix(1);
        std::snprintf(url, sizeof(url), "%.*s/%d/%d/%d.png", static_cast<int>(endpoint.size()),
                      endpoint.data(), frame.view.zoom, x, y);
        std::snprintf(agent, sizeof(agent), "PACE/%s%s%s%s", esp_app_get_description()->version,
                      settings.contact[0] ? " (" : "", settings.contact.data(),
                      settings.contact[0] ? ")" : "");
        RequestHeaders headers{nullptr, nullptr, agent, false, "image/png"};
        std::size_t size;
        int status;
        oldest->zoom = -1;
        const auto error =
            fetch(url, nullptr, false, reinterpret_cast<char *>(oldest->tile.bytes.data()),
                  kMapTileBytes, mono + 10000, size, status, &headers);
        worker->next_request = mono + 1000;
        if (error != ESP_OK || !png_tile(oldest->tile.bytes.data(), size)) {
            worker->next_request = mono + 60000;
            xQueueSend(runtime.map_completed, &worker->frame, 0);
            worker->frame = nullptr;
            return;
        }
        oldest->tile.size = size;
        oldest->x = x;
        oldest->y = y;
        oldest->zoom = frame.view.zoom;
        oldest->expires = mono + kTileLifetime;
        cached = oldest;
    }
    cached->used = mono + 1;
    // This frame is worker-owned until the complete bounded viewport is published.
    frame.tiles[index].size = cached->tile.size;
    std::memcpy(frame.tiles[index].bytes.data(), cached->tile.bytes.data(), cached->tile.size);
    if (++worker->next_tile == static_cast<unsigned>(frame.view.columns * frame.view.rows)) {
        frame.ready = true;
        xQueueSend(runtime.map_completed, &worker->frame, 0);
        worker->frame = nullptr;
    }
}
} // namespace pace::services
