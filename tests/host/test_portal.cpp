// SPDX-License-Identifier: MIT
#include "esp_app_desc.h"
#include "portal_sdk.hpp"
#include "services_internal.hpp"
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>

using namespace pace;
using namespace pace::services;

namespace {
unsigned checks{}, saves{}, restarts{}, disconnects{};
std::int64_t clock_ms{};
std::string persisted, csrf;
esp_err_t read_error = ESP_OK, write_error = ESP_OK;
bool setup_interface = true;
Health status;
std::map<std::pair<std::string, httpd_method_t>, esp_err_t (*)(httpd_req_t *)> routes;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                                        \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

std::string change(const std::string &bytes, const char *section, const char *name, cJSON *value) {
    Json json(bytes.data(), bytes.size(), kSettingsMaxBytes);
    auto *parent = cJSON_GetObjectItemCaseSensitive(json.get(), section);
    CHECK(cJSON_ReplaceItemInObjectCaseSensitive(parent, name, value));
    char output[kSettingsMaxBytes];
    CHECK(cJSON_PrintPreallocated(json.get(), output, sizeof(output), false));
    return output;
}

httpd_req_t call(const char *path, httpd_method_t method, const std::string &body = "",
                 bool token = true, const char *host = "192.168.4.1",
                 const char *origin = "http://192.168.4.1") {
    httpd_req_t r;
    r.body = body;
    r.content_len = body.size();
    r.headers = {{"Host", host}, {"Origin", origin}, {"Content-Type", "application/json"}};
    if (token)
        r.headers["X-PACE-CSRF"] = csrf;
    CHECK(routes.count({path, method}) == 1);
    routes.at({path, method})(&r);
    return r;
}
} // namespace

// A small embedded page is sufficient for testing nonce injection and request handling.
extern const char
    setup_page[] asm("_binary_setup_html_start") = "<script nonce='@TOKEN@'>@TOKEN@</script>";

std::int64_t esp_timer_get_time() { return clock_ms * 1000; }

std::uint32_t esp_random() { return 0x12; }

unsigned uxTaskGetStackHighWaterMark(void *) { return 4096; }

esp_err_t esp_wifi_set_mode(int) { return ESP_OK; }

esp_err_t esp_wifi_set_config(int, const wifi_config_t *) { return ESP_OK; }

esp_err_t esp_wifi_disconnect() {
    ++disconnects;
    return ESP_OK;
}

const char *esp_err_to_name(esp_err_t) { return "ESP_OK"; }

const esp_app_desc_t *esp_app_get_description() {
    static esp_app_desc_t d{};
    return &d;
}

int xQueuePeek(QueueHandle_t, void *out, TickType_t) {
    std::memcpy(out, &status, sizeof(status));
    return pdTRUE;
}

int xQueueReset(QueueHandle_t) { return pdTRUE; }

int xQueueSend(QueueHandle_t, const void *, TickType_t) { return pdTRUE; }

int xQueueReceive(QueueHandle_t, void *, TickType_t) { return pdFALSE; }

namespace pace::services {
bool update_available() { return true; }

bool update_source_configured(const char *) { return false; }

bool request(Runtime &, Command command) {
    if (command == Command::restart)
        ++restarts;
    return true;
}

esp_err_t load_blob(const char *, const char *, char *out, std::size_t capacity,
                    std::size_t &size) {
    size = 0;
    if (read_error != ESP_OK)
        return read_error;
    if (persisted.empty())
        return ESP_ERR_NVS_NOT_FOUND;
    if (persisted.size() > capacity)
        return ESP_ERR_INVALID_SIZE;
    size = persisted.size();
    std::memcpy(out, persisted.data(), size);
    return ESP_OK;
}

esp_err_t save_blob(const char *, const char *, const char *bytes, std::size_t size) {
    if (write_error != ESP_OK)
        return write_error;
    persisted.assign(bytes, size);
    ++saves;
    return ESP_OK;
}
} // namespace pace::services

esp_err_t httpd_start(httpd_handle_t *server, const httpd_config_t *config) {
    CHECK(config->stack_size == 8192);
    *server = reinterpret_cast<void *>(1);
    return ESP_OK;
}

esp_err_t httpd_stop(httpd_handle_t) {
    routes.clear();
    return ESP_OK;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t *route) {
    CHECK(routes.emplace(std::make_pair(route->uri, route->method), route->handler).second);
    return ESP_OK;
}

std::size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *name) {
    return r->headers[name].size();
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *name, char *out,
                                      std::size_t size) {
    const auto &value = r->headers[name];
    if (value.size() >= size)
        return ESP_ERR_INVALID_SIZE;
    std::memcpy(out, value.c_str(), value.size() + 1);
    return ESP_OK;
}

int httpd_req_to_sockfd(httpd_req_t *) { return 0; }

int pace_test_getsockname(int, sockaddr *out, socklen_t *size) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(setup_interface ? "192.168.4.1" : "192.0.2.1");
    CHECK(*size >= sizeof(address));
    std::memcpy(out, &address, sizeof(address));
    *size = sizeof(address);
    return 0;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *name, const char *value) {
    r->response_headers[name] = value;
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type) {
    r->type = type;
    return ESP_OK;
}

esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *text) {
    r->response = text;
    return ESP_OK;
}

esp_err_t httpd_resp_send_err(httpd_req_t *r, int code, const char *text) {
    r->status = code;
    r->response = text;
    return ESP_FAIL;
}

esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *bytes, std::size_t size) {
    if (size)
        r->response.append(bytes, size);
    return ESP_OK;
}

int httpd_req_recv(httpd_req_t *r, char *out, std::size_t size) {
    const auto count = std::min(size, r->body.size() - r->received);
    std::memcpy(out, r->body.data() + r->received, count);
    r->received += count;
    return count;
}

int main() {
    Runtime runtime;
    runtime.storage_ok = true;
    CHECK(portal_start(runtime, status) == ESP_OK);
    CHECK(disconnects == 1);
    const auto page = call("/", HTTP_GET, "", false);
    CHECK(page.status == 200 && page.response.find("@TOKEN@") == std::string::npos);
    const auto marker = page.response.find("nonce='") + 7;
    csrf = page.response.substr(marker, 64);
    CHECK(csrf.size() == 64);
    CHECK(call("/settings", HTTP_GET, "", false).status == 403);
    CHECK(call("/settings", HTTP_GET, "", true, "example.invalid").status == 403);
    setup_interface = false;
    CHECK(call("/settings", HTTP_GET).status == 403);
    setup_interface = true;
    CHECK(call("/settings", HTTP_GET).response == "{\"configured\":false}");

    std::ifstream file(PACE_SOURCE_DIR "/docs/device.example.json");
    std::string original{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    original = change(original, "wifi", "password", cJSON_CreateString("test-password"));
    original =
        change(original, "calendar", "transport_profile", cJSON_CreateString("generic_https"));
    original = change(original, "calendar", "endpoint",
                      cJSON_CreateString("https://example.invalid/private-feed"));
    original = change(original, "calendar", "device_token",
                      cJSON_CreateString(std::string(43, 'q').c_str()));
    original = change(original, "traffic", "api_key", cJSON_CreateString("synthetic-api-key"));
    original = change(original, "updates", "manifest_url",
                      cJSON_CreateString("https://example.invalid/private-update"));
    Settings parsed;
    CHECK(parse_settings(original.data(), original.size(), parsed));
    CHECK(call("/settings", HTTP_POST, original).status == 200);
    CHECK(saves == 1 && restarts == 1);
    runtime.configured = true;
    runtime.settings = parsed;

    auto view = call("/settings", HTTP_GET);
    CHECK(view.status == 200 && view.response_headers["Cache-Control"] == "no-store");
    for (const auto *secret :
         {"test-password", "private-feed", "qqqqqq", "synthetic-api-key", "private-update"})
        CHECK(view.response.find(secret) == std::string::npos);
    auto edited = change(view.response, "preferences", "temperature_unit", cJSON_CreateString("F"));
    CHECK(call("/settings", HTTP_POST, edited).status == 200);
    CHECK(parse_settings(persisted.data(), persisted.size(), parsed));
    CHECK(parsed.fahrenheit && parsed.wifi_password == runtime.settings.wifi_password);
    CHECK(parsed.endpoint == runtime.settings.endpoint &&
          parsed.device_token == runtime.settings.device_token);
    CHECK(parsed.ota_manifest_url == runtime.settings.ota_manifest_url);
    CHECK(persisted.find("synthetic-api-key") !=
          std::string::npos); // Preserve disabled-feature settings too.

    const auto good = persisted;
    CHECK(call("/settings", HTTP_POST, edited, false).status == 403);
    CHECK(call("/settings", HTTP_POST, edited, true, "192.168.4.1", "http://example.invalid")
              .status == 403);
    CHECK(call("/settings", HTTP_POST, "{bad json").status == 400);
    CHECK(call("/settings", HTTP_POST, std::string(kSettingsMaxBytes + 1, 'x')).status == 400);
    CHECK(call("/settings", HTTP_POST,
               change(edited, "wifi", "ssid", cJSON_CreateString("Another network")))
              .status == 400);
    CHECK(call("/settings", HTTP_POST,
               change(edited, "calendar", "endpoint",
                      cJSON_CreateString("https://example.invalid/new")))
              .status == 400);
    CHECK(call("/settings", HTTP_POST,
               change(edited, "calendar", "transport_profile",
                      cJSON_CreateString("google_apps_script")))
              .status == 400);
    CHECK(persisted == good && saves == 2 && restarts == 2);
    read_error = ESP_FAIL;
    CHECK(call("/settings", HTTP_GET).status == 500);
    CHECK(call("/settings", HTTP_POST, edited).status == 500);
    read_error = ESP_OK;
    write_error = ESP_FAIL;
    CHECK(call("/settings", HTTP_POST, edited).status == 500);
    CHECK(persisted == good && saves == 2 && restarts == 2);
    write_error = ESP_OK;

    auto cleared = change(edited, "wifi", "password", cJSON_CreateString(""));
    cleared = change(cleared, "updates", "manifest_url", cJSON_CreateString(""));
    CHECK(call("/settings", HTTP_POST, cleared).status == 200);
    CHECK(parse_settings(persisted.data(), persisted.size(), parsed));
    CHECK(!parsed.wifi_password[0] && !parsed.ota_manifest_url[0]);
    CHECK(parsed.device_token == runtime.settings.device_token);
    persisted.clear();
    CHECK(call("/settings", HTTP_POST, edited).status == 400); // Cannot keep absent secrets.
    CHECK(call("/settings", HTTP_GET).status ==
          500); // Never replace a missing configured record with defaults.
    persisted = good;

    for (unsigned i = 0; i < 100; ++i) {
        CHECK(call("/", HTTP_GET).status == 200);
        const auto diagnostics = call("/diagnostics", HTTP_GET);
        CHECK(diagnostics.status == 200 &&
              diagnostics.response.find("ota_available") != std::string::npos);
    }
    clock_ms = 10 * 60 * 1000;
    CHECK(!portal_expired());
    clock_ms = 15 * 60 * 1000 - 1;
    CHECK(!portal_expired());
    CHECK(call("/activity", HTTP_POST, "", false).status == 403);
    ++clock_ms;
    CHECK(portal_expired()); // Unauthorized traffic did not renew the timer.
    CHECK(call("/activity", HTTP_POST).status == 200);
    CHECK(!portal_expired());
    clock_ms += 15 * 60 * 1000;
    CHECK(portal_expired());
    portal_stop();
    status.network = true;
    CHECK(portal_start(runtime, status) == ESP_OK);
    CHECK(disconnects == 1); // Preserve an already established home connection.
    portal_stop();
    std::printf("%u setup handler checks passed; Health=%zu bytes\n", checks, sizeof(Health));
}
