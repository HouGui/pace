// SPDX-License-Identifier: MIT
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "services_internal.hpp"
#include <cstring>

namespace pace::services {
bool cache_header(const Settings &settings, char *header) {
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    const unsigned char profile = settings.google_redirect ? 1 : 0;
    const bool ok =
        mbedtls_sha256_starts(&hash, 0) == 0 &&
        mbedtls_sha256_update(&hash,
                              reinterpret_cast<const unsigned char *>(settings.endpoint.data()),
                              std::strlen(settings.endpoint.data()) + 1) == 0 &&
        mbedtls_sha256_update(&hash,
                              reinterpret_cast<const unsigned char *>(settings.device_token.data()),
                              std::strlen(settings.device_token.data()) + 1) == 0 &&
        mbedtls_sha256_update(&hash, &profile, 1) == 0 &&
        mbedtls_sha256_finish(&hash, reinterpret_cast<unsigned char *>(header + 4)) == 0;
    mbedtls_sha256_free(&hash);
    if (ok)
        std::memcpy(header, "PC02", 4);
    return ok;
}

esp_err_t storage_init() {
    // Never erase unknown/newer settings automatically (including after rollback).
    auto error = nvs_flash_init();
    if (error != ESP_OK)
        return error;
#if CONFIG_NVS_ENCRYPTION
    // Use the same provisioned SDK security scheme for both private partitions.
    // Never silently open the calendar cache unencrypted in an encrypted build.
    nvs_sec_cfg_t keys{};
    auto *scheme = nvs_flash_get_default_security_scheme();
    if (!scheme)
        return ESP_ERR_INVALID_STATE;
    error = nvs_flash_read_security_cfg_v2(scheme, &keys);
    if (error == ESP_OK)
        error = nvs_flash_secure_init_partition("calendar", &keys);
    volatile unsigned char *secret = reinterpret_cast<volatile unsigned char *>(&keys);
    for (std::size_t i = 0; i < sizeof(keys); ++i)
        secret[i] = 0;
    return error;
#else
    return nvs_flash_init_partition("calendar");
#endif
}

esp_err_t load_blob(const char *partition, const char *key, char *out, std::size_t capacity,
                    std::size_t &size) {
    nvs_handle_t handle;
    size = 0;
    auto error = nvs_open_from_partition(partition, "pace", NVS_READONLY, &handle);
    if (error != ESP_OK)
        return error;
    error = nvs_get_blob(handle, key, nullptr, &size);
    if (error == ESP_OK && (size == 0 || size > capacity))
        error = ESP_ERR_INVALID_SIZE;
    if (error == ESP_OK)
        error = nvs_get_blob(handle, key, out, &size);
    nvs_close(handle);
    return error;
}

esp_err_t save_blob(const char *partition, const char *key, const char *bytes, std::size_t size) {
    nvs_handle_t handle;
    auto error = nvs_open_from_partition(partition, "pace", NVS_READWRITE, &handle);
    if (error != ESP_OK)
        return error;
    error = nvs_set_blob(handle, key, bytes, size);
    if (error == ESP_OK)
        error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

esp_err_t erase_user_data() {
    // Explicit local confirmation required by the portal, never a boot fallback.
    for (const char *part : {"calendar", "nvs"}) {
        nvs_handle_t handle;
        auto error = nvs_open_from_partition(part, "pace", NVS_READWRITE, &handle);
        if (error != ESP_OK)
            return error;
        error = nvs_erase_all(handle);
        if (error == ESP_OK)
            error = nvs_commit(handle);
        nvs_close(handle);
        if (error != ESP_OK)
            return error;
    }
    return ESP_OK;
}
} // namespace pace::services
