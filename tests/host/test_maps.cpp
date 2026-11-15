// SPDX-License-Identifier: MIT
#include "esp_app_desc.h"
#include "pace_http.hpp"
#include "services_internal.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
unsigned checks{}, requests{};
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                                        \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

struct Queue {
    pace::services::MapFrame *frame{};
};

std::vector<Queue *> queues;
std::vector<void *> allocations;
pace::services::Runtime runtime;
pace::services::Health health;
pace::services::MapTile tile;
esp_app_desc_t description;
bool fail_request{};
} // namespace

void *heap_caps_malloc(std::size_t size, int) {
    auto *memory = std::malloc(size);
    allocations.push_back(memory);
    return memory;
}

void heap_caps_free(void *memory) { std::free(memory); }

QueueHandle_t xQueueCreate(unsigned count, unsigned size) {
    CHECK(count == 1 && size == sizeof(pace::services::MapFrame *));
    auto *queue = new Queue;
    queues.push_back(queue);
    return queue;
}

void vQueueDelete(QueueHandle_t queue) { delete static_cast<Queue *>(queue); }

int xQueueSend(QueueHandle_t handle, const void *item, TickType_t) {
    auto &queue = *static_cast<Queue *>(handle);
    if (queue.frame)
        return pdFALSE;
    queue.frame = *static_cast<pace::services::MapFrame *const *>(item);
    return pdTRUE;
}

int xQueueReceive(QueueHandle_t handle, void *item, TickType_t) {
    auto &queue = *static_cast<Queue *>(handle);
    if (!queue.frame)
        return pdFALSE;
    *static_cast<pace::services::MapFrame **>(item) = queue.frame;
    queue.frame = nullptr;
    return pdTRUE;
}

const esp_app_desc_t *esp_app_get_description() { return &description; }

namespace pace::services {
esp_err_t fetch(const char *url, const char *body, bool redirect, char *out, std::size_t capacity,
                std::int64_t deadline, std::size_t &size, int &status,
                const RequestHeaders *headers) {
    ++requests;
    CHECK(std::strstr(url, "https://example.org/tiles/") == url);
    CHECK(!body && !redirect && deadline > 0 && capacity == kMapTileBytes);
    CHECK(headers && !headers->api_key && !headers->field_mask);
    CHECK(!std::strcmp(headers->accept, "image/png"));
    CHECK(std::strstr(headers->user_agent, "PACE/test"));
    size = tile.size;
    status = fail_request ? 429 : 200;
    std::memcpy(out, tile.bytes.data(), size);
    return fail_request ? ESP_FAIL : ESP_OK;
}
} // namespace pace::services

int main() {
    using namespace pace;
    using namespace pace::services;
    auto *file = std::fopen(PACE_SOURCE_DIR "/tests/fixtures/map-tile.png", "rb");
    CHECK(file);
    tile.size = std::fread(tile.bytes.data(), 1, tile.bytes.size(), file);
    std::fclose(file);
    CHECK(png_tile(tile.bytes.data(), tile.size));
    CHECK(!png_tile(tile.bytes.data(), tile.size - 1));
    CHECK(!png_tile(tile.bytes.data(), kMapTileBytes + 1));
    const auto saved_type = tile.bytes[37];
    tile.bytes[37] = 'z'; // Unknown/compressed ancillary chunks are not accepted.
    CHECK(!png_tile(tile.bytes.data(), tile.size));
    tile.bytes[37] = saved_type;
    tile.bytes[19] ^= 1;
    CHECK(!png_tile(tile.bytes.data(), tile.size));
    tile.bytes[19] ^= 1;
    auto &route = health.routes[0];
    route.status = RouteStatus::ok;
    route.fetched_at_monotonic_ms = 1000;
    route.geometry.count = 2;
    route.geometry.points[0] = {48000000, 8000000};
    route.geometry.points[1] = {48010000, 8010000};
    MapViewport view;
    CHECK(map_viewport(route.geometry, view));
    CHECK(view.columns <= 2 && view.rows <= 2 && view.zoom > 0);
    for (unsigned i = 0; i < 2; ++i) {
        const auto point = map_point(view, route.geometry.points[i]);
        CHECK(point.x >= 0 && point.x < kMapWidth && point.y >= 0 && point.y < kMapHeight);
    }
    auto crossing = route.geometry;
    crossing.points[0] = {0, 179999000};
    crossing.points[1] = {0, -179999000};
    CHECK(map_viewport(crossing, view) && view.zoom > 10);
    CHECK(wrapped_tile_x(-1, 4) == 15);
    crossing.points[0].latitude_e6 = crossing.points[1].latitude_e6 = 90000000;
    CHECK(map_viewport(crossing, view) && view.top == 0);
    crossing.count = 0;
    CHECK(!map_viewport(crossing, view));
    runtime.settings.traffic.count = 1;
    runtime.settings.traffic.provider = RoutingProvider::open;
    std::strcpy(runtime.settings.traffic.tile_endpoint.data(), "https://example.org/tiles");
    std::strcpy(description.version, "test");
    health.network = health.time_trusted = true;
    init_maps(runtime);
    CHECK(runtime.map_worker && runtime.map_available && runtime.map_completed);
    poll_maps(runtime, health, 1000);
    CHECK(requests == 0);
    runtime.traffic_visible = true;
    runtime.map_requested = 0;
    runtime.sync_requested = true;
    poll_maps(runtime, health, 1000);
    CHECK(requests == 0);
    runtime.sync_requested = false;
    for (int i = 1; i <= 4; ++i)
        poll_maps(runtime, health, i * 1000);
    MapFrame *frame = nullptr;
    CHECK(xQueueReceive(runtime.map_completed, &frame, 0) && frame->ready);
    CHECK(requests > 0 && requests <= 4);
    const auto count = requests;
    // While the UI owns the completed frame the worker cannot overwrite it.
    poll_maps(runtime, health, 5000);
    CHECK(requests == count);
    CHECK(xQueueSend(runtime.map_available, &frame, 0));
    for (int i = 6; i <= 9; ++i)
        poll_maps(runtime, health, i * 1000);
    CHECK(requests == count);
    CHECK(xQueueReceive(runtime.map_completed, &frame, 0) && frame->ready);
    CHECK(xQueueSend(runtime.map_available, &frame, 0));
    // A different viewport fails visibly and observes a one-minute retry delay.
    route.geometry.points[0].longitude_e6 += 1000000;
    route.geometry.points[1].longitude_e6 += 1000000;
    route.fetched_at_monotonic_ms = 10000;
    fail_request = true;
    poll_maps(runtime, health, 10000);
    CHECK(xQueueReceive(runtime.map_completed, &frame, 0) && !frame->ready);
    CHECK(xQueueSend(runtime.map_available, &frame, 0));
    poll_maps(runtime, health, 11000);
    CHECK(requests == count + 1);
    runtime.map_requested = -1;
    poll_maps(runtime, health, 80000);
    CHECK(requests == count + 1);
    for (auto *queue : queues)
        delete queue;
    for (auto *memory : allocations)
        std::free(memory);
    std::printf("PASS: bounded maps, projection, ownership, caching and errors (%u assertions)\n",
                checks);
}
