// SPDX-License-Identifier: MIT
#include "esp_http_client.h"
#include "pace_http.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
unsigned checks;

void check(bool ok, int line) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "HTTP check failed at line %d\n", line);
        std::exit(1);
    }
}

#define CHECK(condition) check((condition), __LINE__)

struct Response {
    std::string body;
    int status{200};
    bool buffered{true}, truncated{};
    std::int64_t length{-1}; // -1: body length; 0: chunked/unknown to the caller.
    std::size_t chunk{1024};
    std::vector<std::pair<std::string, std::string>> headers{};
};

struct Request {
    std::string url, body;
    esp_http_client_method_t method{HTTP_METHOD_GET};
    std::vector<std::pair<std::string, std::string>> headers{};
};

std::vector<Response> responses;
std::vector<Request> requests;
unsigned cleaned, reads;
esp_err_t open_error = ESP_OK;
std::int64_t clock_us, read_duration_us, open_duration_us;
} // namespace

struct TestClient {
    esp_http_client_config_t config;
    std::size_t index{}, cursor{};
};

namespace {
TestClient client;

void prepare(std::vector<Response> pages) {
    responses = std::move(pages);
    requests.clear();
    cleaned = reads = 0;
    open_error = ESP_OK;
    clock_us = read_duration_us = open_duration_us = 0;
}

esp_err_t fetch(std::string &result, bool google = false, std::size_t limit = 4096,
                std::int64_t deadline = 60000) {
    std::array<char, 4097> bytes{};
    bytes.fill('!');
    std::size_t size;
    int status;
    const auto error = pace::services::fetch(
        google ? "https://script.google.com/macros/s/synthetic/exec"
               : "https://example.invalid/feed",
        "synthetic-secret", google, bytes.data(), limit, deadline, size, status);
    CHECK(bytes[limit] == '!');
    result.assign(bytes.data(), size);
    CHECK(requests.size() == cleaned);
    return error;
}
} // namespace

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    CHECK(requests.size() < responses.size());
    CHECK(config->crt_bundle_attach && config->disable_auto_redirect && config->timeout_ms > 0);
    client = {*config, requests.size(), 0};
    requests.push_back({config->url, {}, HTTP_METHOD_GET});
    return &client;
}

esp_err_t esp_http_client_set_method(esp_http_client_handle_t c, esp_http_client_method_t method) {
    requests[c->index].method = method;
    return ESP_OK;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key,
                                     const char *value) {
    requests[c->index].headers.emplace_back(key, value);
    return ESP_OK;
}

esp_err_t esp_http_client_open(esp_http_client_handle_t, int) {
    clock_us += open_duration_us;
    return open_error;
}

int esp_http_client_write(esp_http_client_handle_t c, const char *body, int size) {
    requests[c->index].body.append(body, size);
    return size;
}

std::int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c) {
    auto &response = responses[c->index];
    for (auto &header : response.headers) {
        esp_http_client_event_t event{HTTP_EVENT_ON_HEADER, c->config.user_data,
                                      header.first.data(), header.second.data()};
        c->config.event_handler(&event);
    }
    return response.length < 0 ? static_cast<std::int64_t>(response.body.size()) : response.length;
}

int esp_http_client_get_status_code(esp_http_client_handle_t c) {
    return responses[c->index].status;
}

bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) {
    const auto &r = responses[c->index];
    // Mirrors the SDK distinction: bytes received != cached bytes consumed.
    return !r.truncated && (r.buffered || c->cursor == r.body.size());
}

esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t, int ms) {
    CHECK(ms > 0 && ms <= 5000);
    return ESP_OK;
}

int esp_http_client_read(esp_http_client_handle_t c, char *out, int capacity) {
    ++reads;
    clock_us += read_duration_us;
    const auto &r = responses[c->index];
    const auto count =
        std::min({r.chunk, r.body.size() - c->cursor, static_cast<std::size_t>(capacity)});
    std::memcpy(out, r.body.data() + c->cursor, count);
    c->cursor += count;
    return static_cast<int>(count);
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t) {
    ++cleaned;
    return ESP_OK;
}

esp_err_t esp_crt_bundle_attach(void *) { return ESP_OK; }

esp_err_t esp_task_wdt_reset() { return ESP_OK; }

std::int64_t esp_timer_get_time() { return clock_us; }

int main() {
    std::string body;
    prepare({Response{"{}"}});
    char bytes[32];
    std::size_t size;
    int status;
    pace::services::RequestHeaders headers{"synthetic-key", "routes.duration"};
    CHECK(pace::services::fetch("https://routes.googleapis.com/directions/v2:computeRoutes", "{}",
                                false, bytes, sizeof(bytes), 10000, size, status,
                                &headers) == ESP_OK);
    CHECK(std::find(requests[0].headers.begin(), requests[0].headers.end(),
                    std::make_pair(std::string("X-Goog-Api-Key"), std::string("synthetic-key"))) !=
          requests[0].headers.end());
    prepare({Response{""}});
    CHECK(pace::services::fetch("https://script.google.com/macros/s/test/exec", "{}", true, bytes,
                                sizeof(bytes), 10000, size, status,
                                &headers) == ESP_ERR_INVALID_ARG);
    CHECK(requests.empty());
    Response no_route{"{\"code\":\"NoRoute\"}"};
    no_route.status = 400;
    prepare({no_route});
    pace::services::RequestHeaders open_headers{nullptr, nullptr, "PACE/test", true};
    CHECK(pace::services::fetch("https://example.org/route", nullptr, false, bytes, sizeof(bytes),
                                10000, size, status, &open_headers) == ESP_OK);
    CHECK(status == 400 && size == no_route.body.size());
    CHECK(std::find(requests[0].headers.begin(), requests[0].headers.end(),
                    std::make_pair(std::string("User-Agent"), std::string("PACE/test"))) !=
          requests[0].headers.end());
    for (const auto &header : requests[0].headers)
        CHECK(header.first != "X-Goog-Api-Key" && header.first != "X-Goog-FieldMask");
    prepare({no_route});
    CHECK(fetch(body) != ESP_OK); // Error bodies are opt-in for routing, never feeds or updates.
    Response small{"{\"ok\":true}"};
    prepare({small});
    CHECK(fetch(body) == ESP_OK && body == small.body);
    CHECK(reads >= 1); // Regression: the old loop skipped this already-buffered body.
    Response streamed{std::string(2500, 'a')};
    streamed.buffered = false;
    streamed.length = 0;
    streamed.chunk = 333;
    prepare({streamed});
    CHECK(fetch(body) == ESP_OK && body == streamed.body);
    streamed.truncated = true;
    prepare({streamed});
    CHECK(fetch(body) != ESP_OK);
    streamed.truncated = false;
    prepare({streamed});
    CHECK(fetch(body, false, 1000) == ESP_ERR_INVALID_SIZE);
    prepare({small});
    CHECK(fetch(body, false, 2) != ESP_OK && reads == 0);
    small.headers = {{"Content-Encoding", "gzip"}};
    prepare({small});
    CHECK(fetch(body) != ESP_OK && reads == 0);
    Response redirect;
    redirect.status = 302;
    redirect.headers = {
        {"Location", "https://script.googleusercontent.com/macros/echo?id=synthetic"}};
    prepare({redirect, Response{"complete"}});
    CHECK(fetch(body, true) == ESP_OK && body == "complete");
    CHECK(requests[0].method == HTTP_METHOD_POST && !requests[0].body.empty());
    CHECK(requests[1].method == HTTP_METHOD_GET && requests[1].body.empty());
    prepare({redirect});
    CHECK(fetch(body) != ESP_OK && requests.size() == 1);
    redirect.headers[0].second = "https://attacker.invalid/feed";
    prepare({redirect});
    CHECK(fetch(body, true) != ESP_OK && requests.size() == 1);
    prepare({Response{""}});
    CHECK(fetch(body) == ESP_OK && body.empty());
    prepare({Response{"denied", 401}});
    CHECK(fetch(body) != ESP_OK);
    prepare({Response{"late"}});
    CHECK(fetch(body, false, 4096, 0) == ESP_ERR_TIMEOUT && requests.empty());
    prepare({streamed});
    read_duration_us = 1000;
    CHECK(fetch(body, false, 4096, 2) == ESP_ERR_TIMEOUT);
    prepare({Response{"too late"}});
    open_duration_us = 5000;
    CHECK(fetch(body, false, 4096, 5) == ESP_ERR_TIMEOUT);
    CHECK(reads == 0 && requests[0].body.empty());
    prepare({Response{"unused"}});
    open_error = ESP_ERR_NO_MEM;
    CHECK(fetch(body) == ESP_ERR_NO_MEM && reads == 0);
    CHECK(cleaned == 1 && body.empty());
    std::printf("PASS: %u HTTP assertions (SDK seam; no real TLS/network)\n", checks);
}
