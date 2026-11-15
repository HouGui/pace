// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "pace/model.hpp"
#include "pace/protocol.hpp"
#include "pace_maps.hpp"
#include "pace_ota.hpp"
#include <atomic>

namespace pace::services {
inline constexpr unsigned kSetupIdleMinutes = 15;

enum class Notice { none, setup_failed, reset_failed };

struct Health {
    bool network{};
    bool time_trusted{};
    bool setup{};
    bool cache_error{};
    bool brightness_error{};
    bool updating{};
    bool syncing{}, sync_pending{};
    std::uint32_t sync_attempt{};
    UpdateStage update_stage{};
    unsigned update_percent{};
    esp_err_t update_error{ESP_OK};
    Notice notice{};
    FeedResult calendar{FeedResult::temporarily_unavailable};
    OutdoorTemperatureSample outside{};
    std::array<RouteResult, kMaxRoutes> routes{};
    std::uint32_t traffic_revision{};
    std::array<char, 33> setup_ssid{};
    std::array<char, 17> setup_password{};
};
enum class Command { setup, close_setup, restart, reset, update, sync, upload };

struct Runtime {
    // Non-secret display preference; separate from immutable connection/settings data.
    std::atomic<unsigned> brightness{100}, brightness_pending{0};
    std::atomic<int> map_requested{-1};
    MapWorker *map_worker{}; // Worker owns cache and unpublished frame for the device lifetime.
    QueueHandle_t map_available{},
        map_completed{}; // One frame transfers ownership, never shared mutable pixels.
    std::atomic<bool> traffic_visible{false};
    std::atomic<bool> sync_requested{false}, sync_in_progress{false}, update_requested{false};
    Settings settings{}; // Immutable after startup; saved changes take effect on reboot.
    bool configured{};
    bool storage_ok{};
    QueueHandle_t available{}; // One scratch Snapshot*: worker receives, UI returns.
    QueueHandle_t completed{}; // One valid Snapshot*: worker sends, UI receives.
    QueueHandle_t health{};    // Latest status by value, xQueueOverwrite.
    QueueHandle_t commands{};
    QueueHandle_t upload_chunks{}, upload_result{};
    std::size_t upload_size{}; // Written before the upload command; immutable until completion.
    Snapshot *initial{};
};

esp_err_t start(Runtime &runtime);
bool request(Runtime &runtime, Command command);
void display_ready(bool ready);
} // namespace pace::services
