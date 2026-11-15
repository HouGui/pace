// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
// SDK descriptor layout at the fields used by the updater; no image verification mock claim.
inline constexpr std::uint32_t ESP_APP_DESC_MAGIC_WORD = 0xabcd5432;

struct esp_app_desc_t {
    std::uint32_t magic_word{}, secure_version{}, reserved[2]{};
    char version[32]{}, project_name[32]{};
    char remainder[176]{};
};

static_assert(sizeof(esp_app_desc_t) == 256);
const esp_app_desc_t *esp_app_get_description();
