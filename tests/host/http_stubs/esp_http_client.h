// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include <cstdint>
struct TestClient;
using esp_http_client_handle_t = TestClient *;

enum esp_http_client_event_id_t { HTTP_EVENT_ON_HEADER };

enum esp_http_client_method_t { HTTP_METHOD_GET, HTTP_METHOD_POST };

struct esp_http_client_event_t {
    esp_http_client_event_id_t event_id;
    void *user_data;
    char *header_key;
    char *header_value;
};

struct esp_http_client_config_t {
    const char *url{};
    esp_err_t (*crt_bundle_attach)(void *){};
    int timeout_ms{};
    bool disable_auto_redirect{};
    esp_err_t (*event_handler)(esp_http_client_event_t *){};
    void *user_data{};
    int buffer_size{}, buffer_size_tx{};
};

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t, esp_http_client_method_t);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
esp_err_t esp_http_client_open(esp_http_client_handle_t, int);
int esp_http_client_write(esp_http_client_handle_t, const char *, int);
std::int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t, int);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
