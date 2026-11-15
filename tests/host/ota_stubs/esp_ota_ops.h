// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include <cstddef>
#include <cstdint>
using esp_ota_handle_t = unsigned;

struct esp_partition_t {
    std::size_t size;
};

enum esp_ota_img_states_t { ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID, ESP_OTA_IMG_UNDEFINED };

inline constexpr std::size_t OTA_SIZE_UNKNOWN = ~std::size_t{0};
const esp_partition_t *esp_ota_get_running_partition();
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *, esp_ota_img_states_t *);
esp_err_t esp_ota_begin(const esp_partition_t *, std::size_t, esp_ota_handle_t *);
esp_err_t esp_ota_write(esp_ota_handle_t, const void *, std::size_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *);
esp_err_t esp_ota_mark_app_valid_cancel_rollback();
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot();
