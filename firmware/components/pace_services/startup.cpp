// SPDX-License-Identifier: MIT
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "network_state.hpp"
#include "nvs.h"
#include <cstdlib>
#include <cstring>
#include <new>

namespace pace::services {
using namespace network_detail;

namespace {
void network_event(void *, esp_event_base_t base, int32_t id, void *data) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        connected = true;
        ESP_LOGI("network", "Wi-Fi obtained an IP address");
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        connected = false;
        if (data) {
            const auto *event = static_cast<const wifi_event_sta_disconnected_t *>(data);
            ESP_LOGW("network", "Wi-Fi disconnected; reason=%u", unsigned(event->reason));
        }
    }
}

void time_event(timeval *) { synchronized = true; }
} // namespace

esp_err_t start(Runtime &r) {
    r.storage_ok = storage_init() == ESP_OK;
    if (r.storage_ok) {
        const auto error = load_brightness(r);
        if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND)
            ESP_LOGW("services", "Brightness preference unavailable; using full brightness");
    }
    char settings[kSettingsMaxBytes];
    std::size_t size = 0;
    if (r.storage_ok) {
        const auto loaded = load_blob("nvs", "settings", settings, sizeof(settings), size);
        if (loaded == ESP_OK) {
            r.configured = parse_settings(settings, size, r.settings);
            if (!r.configured)
                r.storage_ok = false;
        } else if (loaded != ESP_ERR_NVS_NOT_FOUND)
            r.storage_ok = false;
    }
    std::memset(settings, 0, sizeof(settings));
    init_maps(r);
    r.initial = new (std::nothrow) Snapshot;
    auto *scratch = new (std::nothrow) Snapshot;
    if (!r.initial || !scratch)
        return ESP_ERR_NO_MEM;
    auto *cached = static_cast<char *>(
        heap_caps_malloc(65536 + kCachePrefix, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!cached)
        return ESP_ERR_NO_MEM;
    char expected[kCachePrefix];
    if (r.configured && !r.settings.demo && cache_header(r.settings, expected) &&
        load_blob("calendar", "feed", cached, 65536 + kCachePrefix, size) == ESP_OK &&
        size > kCachePrefix && !std::memcmp(cached, expected, kCachePrefix)) {
        if (parse_feed(cached + kCachePrefix, size - kCachePrefix, r.settings.preferences,
                       *r.initial) != FeedResult::ok)
            r.initial->clock_count = 0;
    }
    free(cached);
    r.available = xQueueCreate(1, sizeof(Snapshot *));
    r.completed = xQueueCreate(1, sizeof(Snapshot *));
    r.health = xQueueCreate(1, sizeof(Health));
    r.commands = xQueueCreate(4, sizeof(Command));
    r.upload_chunks = xQueueCreate(2, sizeof(UploadChunk));
    r.upload_result = xQueueCreate(1, sizeof(esp_err_t));
    if (!r.available || !r.completed || !r.health || !r.commands || !r.upload_chunks ||
        !r.upload_result)
        return ESP_ERR_NO_MEM;
    xQueueSend(r.available, &scratch, 0);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                               network_event, nullptr));
    ESP_ERROR_CHECK(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    if (r.configured) {
        wifi_config_t wifi{};
        std::memcpy(wifi.sta.ssid, r.settings.ssid.data(), 32);
        std::memcpy(wifi.sta.password, r.settings.wifi_password.data(), 64);
        wifi.sta.threshold.authmode =
            r.settings.wifi_password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    time_config.sync_cb = time_event;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&time_config));
    // Suppress SDK URL/body diagnostics. Application logs contain only health metrics.
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);
    esp_log_level_set("esp-tls", ESP_LOG_NONE);
    return xTaskCreate(worker, "pace_network", 16384, &r, 4, nullptr) == pdPASS ? ESP_OK
                                                                                : ESP_ERR_NO_MEM;
}
} // namespace pace::services
