// SPDX-License-Identifier: MIT
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "pace/protocol.hpp"
#include "pace_http.hpp"
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <strings.h>

namespace pace::services {
namespace {
struct Headers {
    char location[2049]{};
    bool bad{};
};

struct TransferScratch {
    char target[2049]{};
    Headers headers;
    char chunk[1024];
};

esp_err_t http_event(esp_http_client_event_t *event) {
    if (event->event_id == HTTP_EVENT_ON_HEADER) {
        auto &h = *static_cast<Headers *>(event->user_data);
        if (!strcasecmp(event->header_key, "Location")) {
            if (h.location[0] || std::strlen(event->header_value) > 2048)
                h.bad = true;
            else
                std::strcpy(h.location, event->header_value);
        }
        if (!strcasecmp(event->header_key, "Content-Encoding") &&
            strcasecmp(event->header_value, "identity"))
            h.bad = true;
    }
    return ESP_OK;
}

struct Buffer {
    char *data;
    std::size_t used;
};

esp_err_t buffer_sink(const char *bytes, std::size_t size, void *context) {
    auto &b = *static_cast<Buffer *>(context);
    std::memcpy(b.data + b.used, bytes, size);
    b.used += size;
    return ESP_OK;
}
} // namespace

esp_err_t transfer(const char *url, const char *post, bool google_redirect, std::size_t limit,
                   std::int64_t deadline, BodySink sink, void *context, std::size_t &received,
                   int &status, const RequestHeaders *request_headers) {
    received = 0;
    status = 0;
    if (esp_timer_get_time() / 1000 >= deadline)
        return ESP_ERR_TIMEOUT;
    if (!https_url(url))
        return ESP_ERR_INVALID_ARG;
    // Authenticated provider requests must never carry credentials through redirects.
    if (request_headers && google_redirect)
        return ESP_ERR_INVALID_ARG;
    // TLS already has a deep call stack. One bounded buffer per transfer keeps
    // URLs, response headers and chunks off the network task's stack.
    auto scratch = std::unique_ptr<TransferScratch>(new (std::nothrow) TransferScratch);
    if (!scratch)
        return ESP_ERR_NO_MEM;
    auto &target = scratch->target;
    std::strcpy(target, url);
    for (unsigned hop = 0; hop < 2; ++hop) {
        auto &headers = scratch->headers;
        headers.location[0] = 0;
        headers.bad = false;
        esp_http_client_config_t config{};
        config.url = target;
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.timeout_ms = 5000;
        config.disable_auto_redirect = true;
        config.event_handler = http_event;
        config.user_data = &headers;
        config.buffer_size = 1024;
        config.buffer_size_tx = 2048;
        auto client = esp_http_client_init(&config);
        if (!client)
            return ESP_ERR_NO_MEM;
        const auto set_deadline = [&]() {
            const auto remaining = deadline - esp_timer_get_time() / 1000;
            return remaining <= 0
                       ? ESP_ERR_TIMEOUT
                       : esp_http_client_set_timeout_ms(
                             client, static_cast<int>(std::min<std::int64_t>(remaining, 5000)));
        };
        const int body_length = post ? std::strlen(post) : 0;
        esp_http_client_set_method(client, post ? HTTP_METHOD_POST : HTTP_METHOD_GET);
        esp_http_client_set_header(client, "Accept",
                                   request_headers ? request_headers->accept : "application/json");
        esp_http_client_set_header(client, "Accept-Encoding", "identity");
        if (post)
            esp_http_client_set_header(client, "Content-Type", "application/json");
        auto error = ESP_OK;
        if (request_headers) {
            if (request_headers->api_key)
                error =
                    esp_http_client_set_header(client, "X-Goog-Api-Key", request_headers->api_key);
            if (error == ESP_OK && request_headers->field_mask)
                error = esp_http_client_set_header(client, "X-Goog-FieldMask",
                                                   request_headers->field_mask);
            if (error == ESP_OK && request_headers->user_agent)
                error =
                    esp_http_client_set_header(client, "User-Agent", request_headers->user_agent);
        }
        if (error == ESP_OK)
            error = set_deadline();
        if (error == ESP_OK)
            error = esp_http_client_open(client, body_length);
        if (error == ESP_OK)
            error = set_deadline();
        if (error == ESP_OK && body_length &&
            esp_http_client_write(client, post, body_length) != body_length)
            error = ESP_FAIL;
        if (error == ESP_OK)
            error = set_deadline();
        const auto length = error == ESP_OK ? esp_http_client_fetch_headers(client) : -1;
        status = esp_http_client_get_status_code(client);
        if (error == ESP_OK && (length < 0 || headers.bad))
            error = ESP_FAIL;
        const bool redirect = error == ESP_OK && (status == 302 || status == 303) && hop == 0 &&
                              google_redirect && google_script_url(url) &&
                              google_content_url(headers.location);
        if (redirect) {
            std::strcpy(target, headers.location);
            post = nullptr;
            esp_http_client_cleanup(client);
            if (esp_timer_get_time() / 1000 >= deadline)
                return ESP_ERR_TIMEOUT;
            continue;
        }
        const bool accepted_status = status == 200 || (status == 400 && request_headers &&
                                                       request_headers->read_bad_request_body);
        if (error == ESP_OK &&
            (!accepted_status || (length > 0 && static_cast<std::uint64_t>(length) > limit)))
            error = ESP_FAIL;
        auto &chunk = scratch->chunk;
        // fetch_headers may already have received the entire body into the SDK's
        // cache. Always drain read() before treating transport completion as EOF.
        while (error == ESP_OK) {
            esp_task_wdt_reset();
            const auto remaining = deadline - esp_timer_get_time() / 1000;
            if (remaining <= 0) {
                error = ESP_ERR_TIMEOUT;
                break;
            }
            esp_http_client_set_timeout_ms(client, std::min<std::int64_t>(remaining, 5000));
            const int count = esp_http_client_read(client, chunk, sizeof(chunk));
            if (count < 0) {
                error = ESP_FAIL;
                break;
            }
            if (count == 0) {
                if (!esp_http_client_is_complete_data_received(client))
                    error = ESP_FAIL;
                break;
            }
            if (static_cast<std::size_t>(count) > limit - received) {
                error = ESP_ERR_INVALID_SIZE;
                break;
            }
            if (count)
                error = sink(chunk, count, context);
            received += count;
        }
        if (esp_timer_get_time() / 1000 >= deadline)
            error = ESP_ERR_TIMEOUT;
        esp_http_client_cleanup(client);
        return error;
    }
    return ESP_FAIL;
}

esp_err_t fetch(const char *url, const char *post, bool redirect, char *out, std::size_t capacity,
                std::int64_t deadline, std::size_t &size, int &status,
                const RequestHeaders *headers) {
    Buffer b{out, 0};
    return transfer(url, post, redirect, capacity, deadline, buffer_sink, &b, size, status,
                    headers);
}
} // namespace pace::services
