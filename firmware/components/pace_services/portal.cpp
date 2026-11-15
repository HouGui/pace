// SPDX-License-Identifier: MIT
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "portal_address.hpp"
#include "services_internal.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string_view>

extern const char setup_html_start[] asm("_binary_setup_html_start");

namespace pace::services {
namespace {
httpd_handle_t server;
Runtime *active;
char csrf[65];
std::atomic<std::int64_t> last_activity{0};
constexpr std::int64_t setup_idle_ms = kSetupIdleMinutes * 60 * 1000;
// The HTTP task serializes handlers. Shared bounded scratch stays off its small stack.
char settings_bytes[kSettingsMaxBytes], stored_bytes[kSettingsMaxBytes];

void clear_settings_scratch() {
    std::memset(settings_bytes, 0, sizeof(settings_bytes));
    std::memset(stored_bytes, 0, sizeof(stored_bytes));
}

void random_hex(char *out, std::size_t bytes) {
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < bytes; ++i) {
        const auto byte = static_cast<unsigned char>(esp_random());
        out[2 * i] = digits[byte >> 4];
        out[2 * i + 1] = digits[byte & 15];
    }
    out[2 * bytes] = 0;
}

bool header_equals(httpd_req_t *request, const char *name, const char *expected) {
    char value[128];
    return httpd_req_get_hdr_value_len(request, name) == std::strlen(expected) &&
           httpd_req_get_hdr_value_str(request, name, value, sizeof(value)) == ESP_OK &&
           !std::strcmp(value, expected);
}

bool csrf_matches(httpd_req_t *request) {
    char token[65]{};
    if (httpd_req_get_hdr_value_len(request, "X-PACE-CSRF") != 64 ||
        httpd_req_get_hdr_value_str(request, "X-PACE-CSRF", token, sizeof(token)) != ESP_OK)
        return false;
    unsigned mismatch = 0;
    for (unsigned i = 0; i < 64; ++i)
        mismatch |= token[i] ^ csrf[i];
    return mismatch == 0;
}

bool authorized(httpd_req_t *request, bool mutation,
                const char *content_type = "application/json") {
    sockaddr_storage local{};
    socklen_t size = sizeof(local);
    if (getsockname(httpd_req_to_sockfd(request), reinterpret_cast<sockaddr *>(&local), &size) !=
            0 ||
        !is_setup_address(local, size) ||
        !(header_equals(request, "Host", "192.168.4.1") ||
          header_equals(request, "Host", "192.168.4.1:80")))
        return false;
    if (mutation) {
        if (!header_equals(request, "Origin", "http://192.168.4.1") ||
            !header_equals(request, "Content-Type", content_type) || !csrf_matches(request))
            return false;
    }
    last_activity = esp_timer_get_time() / 1000;
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
    return true;
}

esp_err_t denied(httpd_req_t *r) {
    return httpd_resp_send_err(r, HTTPD_403_FORBIDDEN, "Open setup from the device Wi-Fi network.");
}

esp_err_t page(httpd_req_t *r) {
    if (!authorized(r, false))
        return denied(r);
    char policy[256];
    std::snprintf(policy, sizeof(policy),
                  "default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-%s'; "
                  "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'",
                  csrf);
    httpd_resp_set_hdr(r, "Content-Security-Policy", policy);
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    const char *cursor = setup_html_start;
    while (const char *token = std::strstr(cursor, "@TOKEN@")) {
        if (httpd_resp_send_chunk(r, cursor, token - cursor) != ESP_OK ||
            httpd_resp_send_chunk(r, csrf, 64) != ESP_OK)
            return ESP_FAIL;
        cursor = token + 7;
    }
    if (httpd_resp_send_chunk(r, cursor, std::strlen(cursor)) != ESP_OK)
        return ESP_FAIL;
    return httpd_resp_send_chunk(r, nullptr, 0);
}

bool receive(httpd_req_t *r, char *out, std::size_t capacity, std::size_t &size) {
    if (r->content_len <= 0 || static_cast<std::size_t>(r->content_len) > capacity)
        return false;
    size = 0;
    const auto deadline = esp_timer_get_time() + 10000000;
    while (size < static_cast<std::size_t>(r->content_len)) {
        if (esp_timer_get_time() > deadline)
            return false;
        const int count = httpd_req_recv(r, out + size, r->content_len - size);
        if (count <= 0)
            return false;
        size += count;
    }
    return true;
}

esp_err_t save(httpd_req_t *r) {
    if (!authorized(r, true))
        return denied(r);
    std::size_t size = 0;
    static Settings settings; // Serialized handler; keep bounded settings off the server stack.
    std::size_t stored_size = 0, merged_size = 0;
    const auto loaded =
        load_blob("nvs", "settings", stored_bytes, sizeof(stored_bytes), stored_size);
    if (loaded != ESP_OK && loaded != ESP_ERR_NVS_NOT_FOUND) {
        clear_settings_scratch();
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Saved settings could not be read. Nothing was changed.");
    }
    if (!receive(r, settings_bytes, sizeof(settings_bytes), size) ||
        !restore_settings_secrets(settings_bytes, size, stored_bytes, stored_size, settings_bytes,
                                  sizeof(settings_bytes), merged_size) ||
        !parse_settings(settings_bytes, merged_size, settings)) {
        clear_settings_scratch();
        return httpd_resp_send_err(
            r, HTTPD_400_BAD_REQUEST,
            "Check Wi-Fi, feed URL/secret, preferences and category colors.");
    }
    const auto result = save_blob("nvs", "settings", settings_bytes, merged_size);
    clear_settings_scratch();
    if (result != ESP_OK)
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Settings were not saved. Check storage diagnostics.");
    if (!request(*active, Command::restart))
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Saved; restart the device manually.");
    return httpd_resp_sendstr(r, "Saved. Restarting; reconnect to your usual Wi-Fi. If connection "
                                 "fails, hold Settings on the screen to correct it.");
}

esp_err_t saved_settings(httpd_req_t *r) {
    if (!csrf_matches(r) || !authorized(r, false))
        return denied(r);
    std::size_t size = 0;
    const auto loaded = load_blob("nvs", "settings", stored_bytes, sizeof(stored_bytes), size);
    httpd_resp_set_type(r, "application/json");
    if (loaded == ESP_ERR_NVS_NOT_FOUND && !active->configured) {
        clear_settings_scratch();
        return httpd_resp_sendstr(r, "{\"configured\":false}");
    }
    if (loaded != ESP_OK || !active->storage_ok ||
        !redact_settings(stored_bytes, size, settings_bytes, sizeof(settings_bytes))) {
        clear_settings_scratch();
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Saved settings could not be read. Nothing was changed.");
    }
    const auto result = httpd_resp_sendstr(r, settings_bytes);
    clear_settings_scratch();
    return result;
}

esp_err_t activity(httpd_req_t *r) {
    if (!authorized(r, true))
        return denied(r);
    if (r->content_len != 0)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Invalid action.");
    return httpd_resp_sendstr(r, "OK");
}

esp_err_t action(httpd_req_t *r) {
    if (!authorized(r, true))
        return denied(r);
    char bytes[128];
    std::size_t size;
    if (!receive(r, bytes, sizeof(bytes), size))
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Invalid action.");
    Json json(bytes, size, sizeof(bytes));
    auto *command = cJSON_GetObjectItemCaseSensitive(json.get(), "action");
    if (!cJSON_IsString(command))
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Invalid action.");
    Command selected;
    if (!std::strcmp(command->valuestring, "reset")) {
        auto *confirmation = cJSON_GetObjectItemCaseSensitive(json.get(), "confirmation");
        if (!cJSON_IsString(confirmation) || std::strcmp(confirmation->valuestring, "ERASE"))
            return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Type ERASE to confirm.");
        selected = Command::reset;
    } else if (!std::strcmp(command->valuestring, "update")) {
        if (!update_available())
            return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST,
                                       "Update unavailable: install a signed build once by USB.");
        selected = Command::update;
    } else if (!std::strcmp(command->valuestring, "sync"))
        selected = Command::sync;
    else
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Unknown action.");
    if (!request(*active, selected))
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Busy. Try again.");
    return httpd_resp_sendstr(r, "Requested. Check the device screen for progress.");
}

esp_err_t upload(httpd_req_t *r) {
    if (!authorized(r, true, "application/octet-stream"))
        return denied(r);
    if (!update_available())
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST,
                                   "Update unavailable: install a signed build once by USB.");
    if (r->content_len <= 0 || r->content_len > 3 * 1024 * 1024)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Invalid firmware size.");
    if (active->update_requested.exchange(true))
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Busy. Try again.");
    xQueueReset(active->upload_chunks);
    xQueueReset(active->upload_result);
    active->upload_size = r->content_len;
    const Command command = Command::upload;
    if (xQueueSend(active->commands, &command, 0) != pdTRUE) {
        active->update_requested = false;
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Busy. Try again.");
    }
    // Wait for the serialized worker to finish any calendar request, then stream
    // through two fixed chunks. Never buffer a complete image in RAM or flash.
    esp_err_t result = ESP_FAIL;
    UploadChunk chunk;
    if (xQueueReceive(active->upload_result, &result, pdMS_TO_TICKS(60000)) != pdTRUE ||
        result != ESP_OK) {
        chunk.size = -1;
        xQueueSend(active->upload_chunks, &chunk, 0);
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Update could not start. Wait and retry.");
    }
    const auto deadline = esp_timer_get_time() / 1000 + 300000;
    std::size_t received = 0;
    while (received < active->upload_size && esp_timer_get_time() / 1000 < deadline) {
        const auto remaining = active->upload_size - received;
        const auto size = remaining < sizeof(chunk.bytes) ? remaining : sizeof(chunk.bytes);
        chunk.size = httpd_req_recv(r, chunk.bytes, size);
        if (chunk.size <= 0 || !active->update_requested ||
            xQueueSend(active->upload_chunks, &chunk, pdMS_TO_TICKS(5000)) != pdTRUE)
            break;
        received += chunk.size;
    }
    chunk.size = received == active->upload_size ? 0 : -1;
    if (xQueueSend(active->upload_chunks, &chunk, pdMS_TO_TICKS(5000)) != pdTRUE ||
        xQueueReceive(active->upload_result, &result, pdMS_TO_TICKS(10000)) != pdTRUE ||
        result != ESP_OK)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST,
                                   "Update failed or was rejected. Current firmware retained.");
    return httpd_resp_sendstr(r, "Update installed. Restarting. Keep USB power connected.");
}

esp_err_t diagnostics(httpd_req_t *r) {
    if (!authorized(r, false))
        return denied(r);
    // Route geometry makes Health larger than 5 KiB; never put it on the HTTP task stack.
    static Health health;
    xQueuePeek(active->health, &health, 0);
    auto *json = cJSON_CreateObject();
    if (!json)
        return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(json, "version", esp_app_get_description()->version);
    cJSON_AddStringToObject(json, "network", health.network ? "connected" : "disconnected");
    cJSON_AddStringToObject(json, "time", health.time_trusted ? "synchronized" : "unknown");
    cJSON_AddStringToObject(json, "calendar", feed_result_text(health.calendar));
    cJSON_AddStringToObject(json, "storage",
                            active->storage_ok && !health.cache_error ? "ready" : "fault");
    cJSON_AddBoolToObject(json, "ota_available", update_available());
    cJSON_AddStringToObject(json, "ota_result", esp_err_to_name(health.update_error));
    cJSON_AddBoolToObject(json, "ota_source",
                          update_source_configured(active->settings.ota_manifest_url.data()));
    cJSON_AddBoolToObject(json, "updating", active->update_requested.load());
    char result[512];
    const bool printed = cJSON_PrintPreallocated(json, result, sizeof(result), false);
    cJSON_Delete(json);
    if (!printed)
        return ESP_ERR_NO_MEM;
    httpd_resp_set_type(r, "application/json");
    ESP_LOGI("setup", "Diagnostics; server stack headroom=%u",
             unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    return httpd_resp_sendstr(r, result);
}
} // namespace

esp_err_t portal_start(Runtime &runtime, Health &health) {
    if (server)
        return ESP_ERR_INVALID_STATE;
    active = &runtime;
    // A reconnect scan can move the AP's radio channel while a phone is joining.
    // Keep an established home connection, but cancel unfinished attempts during setup.
    if (!health.network) {
        const auto error = esp_wifi_disconnect();
        if (error != ESP_OK)
            return error;
    }
    random_hex(csrf, 32);
    random_hex(health.setup_password.data(), 8);
    std::snprintf(health.setup_ssid.data(), health.setup_ssid.size(), "PACE-%06x",
                  static_cast<unsigned>(esp_random() & 0xffffff));
    wifi_config_t wifi{};
    std::strcpy(reinterpret_cast<char *>(wifi.ap.ssid), health.setup_ssid.data());
    std::strcpy(reinterpret_cast<char *>(wifi.ap.password), health.setup_password.data());
    wifi.ap.ssid_len = std::strlen(health.setup_ssid.data());
    wifi.ap.channel = 1;
    wifi.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi.ap.max_connection = 2;
    wifi.ap.pmf_cfg.capable = true;
    auto error = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (error == ESP_OK)
        error = esp_wifi_set_config(WIFI_IF_AP, &wifi);
    if (error != ESP_OK) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        return error;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    error = httpd_start(&server, &config);
    if (error != ESP_OK) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        return error;
    }
    const auto register_route = [](const char *uri, httpd_method_t method,
                                   esp_err_t (*handler)(httpd_req_t *)) {
        httpd_uri_t route{};
        route.uri = uri;
        route.method = method;
        route.handler = handler;
        return httpd_register_uri_handler(server, &route);
    };
    error = register_route("/", HTTP_GET, page);
    if (error == ESP_OK)
        error = register_route("/settings", HTTP_GET, saved_settings);
    if (error == ESP_OK)
        error = register_route("/settings", HTTP_POST, save);
    if (error == ESP_OK)
        error = register_route("/activity", HTTP_POST, activity);
    if (error == ESP_OK)
        error = register_route("/action", HTTP_POST, action);
    if (error == ESP_OK)
        error = register_route("/diagnostics", HTTP_GET, diagnostics);
    if (error == ESP_OK)
        error = register_route("/update", HTTP_POST, upload);
    if (error != ESP_OK) {
        portal_stop();
        return error;
    }
    last_activity = esp_timer_get_time() / 1000;
    health.setup = true;
    ESP_LOGI("setup", "Setup started; idle timeout is %u minutes", kSetupIdleMinutes);
    return ESP_OK;
}

void portal_stop() {
    if (server) {
        httpd_stop(server);
        server = nullptr;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
    std::memset(csrf, 0, sizeof(csrf));
    clear_settings_scratch();
    ESP_LOGI("setup", "Setup closed");
}

bool portal_expired() {
    return esp_timer_get_time() / 1000 - last_activity.load() >= setup_idle_ms;
}
} // namespace pace::services
