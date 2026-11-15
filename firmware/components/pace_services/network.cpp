// SPDX-License-Identifier: MIT
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "network_state.hpp"
#include "pace/core.hpp"
#include "pace/sync.hpp"
#include "pace_http.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>

namespace pace::services {
namespace network_detail {
std::atomic<bool> connected{false}, synchronized{false}, screen_ready{false};
std::atomic<std::int64_t> screen_progress{0};

void publish(Runtime &runtime, const Health &health) { xQueueOverwrite(runtime.health, &health); }

bool trustworthy(UtcSeconds now, std::int64_t mono, UtcSeconds &previous,
                 std::int64_t &previous_mono) {
    bool valid = synchronized && now >= 1767225600 && now < 4102444800;
    if (previous >= 1767225600 &&
        std::llabs((now - previous) - (mono - previous_mono) / 1000) > 300) {
        synchronized = false;
        valid = false;
    }
    previous = now;
    previous_mono = mono;
    return valid;
}

bool make_request(const Settings &s, char (&out)[2048]) {
    auto *root = cJSON_CreateObject();
    if (!root)
        return false;
    cJSON_AddNumberToObject(root, "schema_version", kFeedSchemaVersion);
    cJSON_AddBoolToObject(root, "month_view", true);
    cJSON_AddBoolToObject(root, "secondary_calendar", true);
    cJSON_AddStringToObject(root, "device_token", s.device_token.data());
    auto *prefs = cJSON_AddObjectToObject(root, "preferences");
    cJSON_AddStringToObject(prefs, "time_zone", s.preferences.time_zone.data());
    cJSON_AddStringToObject(prefs, "locale", s.preferences.locale.data());
    cJSON_AddStringToObject(prefs, "hour_cycle", s.preferences.twelve_hour ? "h12" : "h23");
    const bool ok = cJSON_PrintPreallocated(root, out, sizeof(out), false);
    cJSON_Delete(root);
    return ok;
}

void worker(void *context) {
    esp_task_wdt_add(nullptr);
    auto &runtime = *static_cast<Runtime *>(context);
    // Route previews enlarge health messages. Allocate once rather than growing the task stack.
    auto owned_health = std::unique_ptr<Health>(new (std::nothrow) Health);
    if (!owned_health) {
        ESP_LOGE("services", "No health buffer");
        vTaskDelete(nullptr);
        return;
    }
    auto &health = *owned_health;
    auto *storage = static_cast<char *>(
        heap_caps_malloc(65536 + kCachePrefix, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!storage) {
        ESP_LOGE("services", "No network buffer");
        owned_health.reset();
        vTaskDelete(nullptr);
        return;
    }
    auto *bytes = storage + kCachePrefix;
    if (!runtime.configured) {
        if (portal_start(runtime, health) != ESP_OK)
            health.notice = Notice::setup_failed;
    }
    std::int64_t next_connect = 0, next_weather = 0, next_metrics = 60000;
    UtcSeconds previous = 0;
    std::int64_t previous_mono = 0;
    SyncSchedule calendar_schedule;
    TrafficSchedule traffic_schedule;
    bool candidate_checked = false;
    std::int64_t next_boot_check = 30000;
    for (;;) {
        esp_task_wdt_reset();
        const auto mono = esp_timer_get_time() / 1000;
        const auto now = static_cast<UtcSeconds>(std::time(nullptr));
        health.network = connected;
        health.sync_pending = runtime.sync_requested;
        health.time_trusted = trustworthy(now, mono, previous, previous_mono);
        save_brightness(runtime, health);
        Command command;
        if (xQueueReceive(runtime.commands, &command, 0) == pdTRUE) {
            if (command == Command::setup && !health.setup) {
                if (portal_start(runtime, health) != ESP_OK)
                    health.notice = Notice::setup_failed;
            } else if (command == Command::close_setup && !runtime.update_requested) {
                portal_stop();
                health.setup = false;
            } else if (command == Command::restart) {
                vTaskDelay(pdMS_TO_TICKS(700));
                esp_restart();
            } else if (command == Command::reset) {
                if (erase_user_data() == ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(700));
                    esp_restart();
                } else
                    health.notice = Notice::reset_failed;
            } else if ((command == Command::upload ||
                        (command == Command::update && health.time_trusted && connected))) {
                health.updating = true;
                health.update_stage = UpdateStage::checking;
                health.update_percent = 0;
                publish(runtime, health);

                struct ProgressContext {
                    Runtime &runtime;
                    Health &health;
                } progress{runtime, health};

                const auto report = [](UpdateStage stage, unsigned percent, void *ctx) {
                    auto &p = *static_cast<ProgressContext *>(ctx);
                    p.health.update_stage = stage;
                    p.health.update_percent = percent;
                    publish(p.runtime, p.health);
                };
                const auto error =
                    command == Command::upload
                        ? install_uploaded_image(runtime.upload_size, receive_upload, &runtime,
                                                 report, &progress)
                        : update_application(report, &progress,
                                             runtime.settings.ota_manifest_url.data());
                if (command == Command::upload)
                    xQueueOverwrite(runtime.upload_result, &error);
                health.update_error = error;
                if (error != ESP_OK)
                    ESP_LOGW("ota", "Update result: %s", esp_err_to_name(error));
                health.updating = false;
                health.update_stage = error == ESP_OK                    ? UpdateStage::restarting
                                      : error == ESP_ERR_NOT_SUPPORTED   ? UpdateStage::unavailable
                                      : error == ESP_ERR_INVALID_VERSION ? UpdateStage::current
                                                                         : UpdateStage::failed;
                publish(runtime, health);
                if (error == ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(700));
                    esp_restart();
                }
            } else if (command == Command::update)
                health.update_stage = UpdateStage::needs_network;
            if (command == Command::update || command == Command::upload)
                runtime.update_requested = false;
        }
        if (health.setup && !runtime.update_requested && portal_expired()) {
            portal_stop();
            health.setup = false;
        }
        if (runtime.configured && !health.setup && !connected && mono >= next_connect) {
            const auto result = esp_wifi_connect();
            if (result == ESP_OK)
                ESP_LOGI("network", "Wi-Fi connection requested");
            else
                ESP_LOGW("network", "Wi-Fi connection request failed: %s", esp_err_to_name(result));
            next_connect = mono + 30000;
        }
        if (runtime.configured && health.time_trusted &&
            calendar_schedule.due(mono, runtime.sync_requested.load()) &&
            (connected || runtime.settings.demo)) {
            Snapshot *candidate = nullptr;
            if (xQueueReceive(runtime.available, &candidate, 0) == pdTRUE) {
                runtime.sync_in_progress = true;
                runtime.sync_requested = false;
                health.sync_pending = false;
                health.syncing = true;
                publish(runtime, health);
                const auto sync_started = esp_timer_get_time() / 1000;
                bool accepted = false;
                if (runtime.settings.demo) {
                    make_demo(*candidate, runtime.settings.preferences, now);
                    accepted = true;
                    health.calendar = FeedResult::ok;
                } else {
                    char body[2048];
                    std::size_t size = 0;
                    int status = 0;
                    auto result = make_request(runtime.settings, body)
                                      ? fetch(runtime.settings.endpoint.data(), body,
                                              runtime.settings.google_redirect, bytes, 65536,
                                              mono + 60000, size, status)
                                      : ESP_ERR_NO_MEM;
                    std::memset(body, 0, sizeof(body));
                    health.calendar =
                        result == ESP_OK
                            ? parse_feed(bytes, size, runtime.settings.preferences, *candidate)
                            : (status == 401 || status == 403
                                   ? FeedResult::access_denied
                                   : FeedResult::temporarily_unavailable);
                    accepted = health.calendar == FeedResult::ok &&
                               candidate->generated_at_utc <= now + 300 &&
                               candidate->generated_at_utc >= now - 1800;
                    if (health.calendar == FeedResult::ok && !accepted)
                        health.calendar = FeedResult::invalid;
                    if (accepted)
                        health.cache_error =
                            !cache_header(runtime.settings, storage) ||
                            save_blob("calendar", "feed", storage, size + kCachePrefix) != ESP_OK;
                }
                if (accepted) {
                    // A free slot proves the completion queue is empty by ownership protocol.
                    if (xQueueSend(runtime.completed, &candidate, 0) != pdTRUE)
                        xQueueSend(runtime.available, &candidate, 0);
                } else {
                    xQueueSend(runtime.available, &candidate, 0);
                }
                calendar_schedule.complete(sync_started, accepted, esp_random());
                ++health.sync_attempt;
                health.syncing = false;
                runtime.sync_in_progress = false;
                publish(runtime, health);
            }
        }
        if (runtime.configured && runtime.settings.weather_enabled && connected &&
            health.time_trusted && mono >= next_weather) {
            char url[256];
            std::snprintf(url, sizeof(url),
                          "https://api.open-meteo.com/v1/"
                          "forecast?latitude=%.5f&longitude=%.5f&current=temperature_2m&timeformat="
                          "unixtime&timezone=UTC",
                          runtime.settings.latitude, runtime.settings.longitude);
            std::size_t size;
            int status;
            if (fetch(url, nullptr, false, bytes, 16384, esp_timer_get_time() / 1000 + 15000, size,
                      status) == ESP_OK &&
                parse_weather(bytes, size, now, health.outside))
                health.outside.fetched_at_monotonic_ms = esp_timer_get_time() / 1000;
            next_weather = mono + 1800000;
        }
        poll_traffic(runtime, health, traffic_schedule, bytes, esp_timer_get_time() / 1000);
        poll_maps(runtime, health, esp_timer_get_time() / 1000);
        if (!candidate_checked && mono >= next_boot_check) {
            const bool healthy = screen_ready &&
                                 esp_timer_get_time() / 1000 - screen_progress.load() < 1500 &&
                                 runtime.storage_ok;
            candidate_checked = confirm_boot_health(healthy) == ESP_OK;
            next_boot_check = mono + 1000;
        }
        if (mono >= next_metrics) {
            ESP_LOGI("health", "heap=%u largest=%u stack=%u network=%d time=%d",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                     (unsigned)uxTaskGetStackHighWaterMark(nullptr), health.network,
                     health.time_trusted);
            next_metrics = mono + 60000;
        }
        publish(runtime, health);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
} // namespace network_detail

using namespace network_detail;

void display_ready(bool ready) {
    screen_ready = ready;
    screen_progress = esp_timer_get_time() / 1000;
}
} // namespace pace::services
