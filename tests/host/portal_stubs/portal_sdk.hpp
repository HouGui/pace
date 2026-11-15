// SPDX-License-Identifier: MIT
#pragma once
// Minimal SDK seams for exercising actual setup handlers on the host.
#include "esp_err.h"
#include "freertos/queue.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <sys/socket.h>

inline constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 10;
inline constexpr int WIFI_MODE_STA = 1, WIFI_MODE_APSTA = 2, WIFI_IF_AP = 1, WIFI_AUTH_WPA2_PSK = 3;

struct wifi_config_t {
    struct {
        unsigned char ssid[32]{}, password[64]{};
        unsigned ssid_len{}, channel{}, authmode{}, max_connection{};

        struct {
            bool capable{};
        } pmf_cfg;
    } ap;
};

esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int, const wifi_config_t *);
esp_err_t esp_wifi_disconnect();
std::uint32_t esp_random();
unsigned uxTaskGetStackHighWaterMark(void *);
int xQueuePeek(QueueHandle_t, void *, TickType_t);
int xQueueReset(QueueHandle_t);
const char *esp_err_to_name(esp_err_t);
#define ESP_LOGI(...) static_cast<void>(0)

enum httpd_method_t { HTTP_GET, HTTP_POST };

inline constexpr int HTTPD_403_FORBIDDEN = 403, HTTPD_400_BAD_REQUEST = 400,
                     HTTPD_500_INTERNAL_SERVER_ERROR = 500;

struct httpd_req_t {
    int content_len{}, status{200};
    std::size_t received{};
    std::map<std::string, std::string> headers, response_headers;
    std::string body, response, type;
};

using httpd_handle_t = void *;

struct httpd_config_t {
    unsigned stack_size{}, max_open_sockets{}, recv_wait_timeout{}, send_wait_timeout{};
    bool lru_purge_enable{};
};

inline httpd_config_t HTTPD_DEFAULT_CONFIG() { return {}; }

struct httpd_uri_t {
    const char *uri{};
    httpd_method_t method{};
    esp_err_t (*handler)(httpd_req_t *){};
};

esp_err_t httpd_start(httpd_handle_t *, const httpd_config_t *);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t *);
std::size_t httpd_req_get_hdr_value_len(httpd_req_t *, const char *);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *, const char *, char *, std::size_t);
int httpd_req_to_sockfd(httpd_req_t *);
int pace_test_getsockname(int, sockaddr *, socklen_t *);
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_sendstr(httpd_req_t *, const char *);
esp_err_t httpd_resp_send_err(httpd_req_t *, int, const char *);
esp_err_t httpd_resp_send_chunk(httpd_req_t *, const char *, std::size_t);
int httpd_req_recv(httpd_req_t *, char *, std::size_t);
