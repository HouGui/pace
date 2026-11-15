// SPDX-License-Identifier: MIT
#pragma once
// Minimal SDK seam for testing PACE's HTTP orchestration, not a TLS simulator.
using esp_err_t = int;
inline constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_ARG = 1, ESP_ERR_TIMEOUT = 2,
                           ESP_ERR_NO_MEM = 3, ESP_ERR_INVALID_SIZE = 4;

inline constexpr esp_err_t ESP_ERR_NOT_SUPPORTED = 5, ESP_ERR_INVALID_STATE = 6,
                           ESP_ERR_INVALID_VERSION = 7, ESP_ERR_INVALID_CRC = 8,
                           ESP_ERR_NOT_FOUND = 9;
