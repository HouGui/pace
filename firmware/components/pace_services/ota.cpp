// SPDX-License-Identifier: MIT
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "pace/protocol.hpp"
#include "pace_board.h"
#include "pace_http.hpp"
#include "pace_ota.hpp"
#include "sdkconfig.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace pace::services {
#if CONFIG_SECURE_SIGNED_ON_UPDATE
namespace {
struct Download {
    esp_ota_handle_t handle{};
    const esp_partition_t *partition{};
    mbedtls_sha256_context hash{};
    char header[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) +
                sizeof(esp_app_desc_t)]{};
    std::size_t header_size{};
    bool started{};
    std::size_t received{}, expected{};
    unsigned percent{101};
    UpdateProgress progress{};
    void *context{};
    char version[32]{};
    bool has_manifest{true};
};

esp_err_t write_image(const char *bytes, std::size_t size, void *context) {
    auto &d = *static_cast<Download *>(context);
    if (size > d.expected - d.received)
        return ESP_ERR_INVALID_SIZE;
    if (d.has_manifest &&
        mbedtls_sha256_update(&d.hash, reinterpret_cast<const unsigned char *>(bytes), size) != 0)
        return ESP_FAIL;
    d.received += size;
    const unsigned percent = d.expected ? d.received * 100 / d.expected : 0;
    if (d.progress && percent != d.percent) {
        d.progress(UpdateStage::downloading, percent, d.context);
        d.percent = percent;
    }
    if (!d.started) {
        const auto copy = std::min(size, sizeof(d.header) - d.header_size);
        std::memcpy(d.header + d.header_size, bytes, copy);
        d.header_size += copy;
        bytes += copy;
        size -= copy;
        if (d.header_size < sizeof(d.header))
            return ESP_OK;
        esp_app_desc_t description;
        std::memcpy(&description,
                    d.header + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
                    sizeof(description));
        if (description.magic_word != ESP_APP_DESC_MAGIC_WORD ||
            std::memcmp(description.project_name, esp_app_get_description()->project_name,
                        sizeof(description.project_name)) ||
            (d.has_manifest &&
             std::memcmp(description.version, d.version, sizeof(description.version))))
            return ESP_ERR_INVALID_ARG;
        if (!d.has_manifest && !std::memcmp(description.version, esp_app_get_description()->version,
                                            sizeof(description.version)))
            return ESP_ERR_INVALID_VERSION;
        auto error = esp_ota_begin(d.partition, OTA_SIZE_UNKNOWN, &d.handle);
        if (error != ESP_OK)
            return error;
        d.started = true;
        error = esp_ota_write(d.handle, d.header, d.header_size);
        if (error != ESP_OK)
            return error;
    }
    return size ? esp_ota_write(d.handle, bytes, size) : ESP_OK;
}

bool text(const cJSON *root, const char *name, char *out, std::size_t size) {
    auto *value = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsString(value) || std::strlen(value->valuestring) >= size)
        return false;
    std::strcpy(out, value->valuestring);
    return true;
}

std::string_view origin(const char *url) {
    const std::string_view s(url);
    return s.substr(0, s.find('/', 8));
}
} // namespace
#endif
esp_err_t confirm_boot_health(bool healthy) {
    const auto *running = esp_ota_get_running_partition();
    if (!running)
        return ESP_ERR_INVALID_STATE;
    esp_ota_img_states_t state;
    const auto result = esp_ota_get_state_partition(running, &state);
    if (result == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY)
        return healthy ? esp_ota_mark_app_valid_cancel_rollback()
                       : esp_ota_mark_app_invalid_rollback_and_reboot();
    if (healthy &&
        (result == ESP_ERR_NOT_FOUND || (result == ESP_OK && state == ESP_OTA_IMG_UNDEFINED))) {
        const auto error = esp_ota_set_boot_partition(running);
        return error == ESP_OK ? esp_ota_mark_app_valid_cancel_rollback() : error;
    }
    if (result == ESP_OK && state == ESP_OTA_IMG_UNDEFINED)
        return ESP_ERR_INVALID_STATE;
    return result;
}

bool update_source_configured(const char *manifest_url) {
    return https_url(manifest_url && manifest_url[0] ? manifest_url : CONFIG_PACE_OTA_MANIFEST_URL);
}

bool update_available() {
#if CONFIG_SECURE_SIGNED_ON_UPDATE
    return true;
#else
    return false;
#endif
}

esp_err_t update_application(UpdateProgress progress, void *context, const char *manifest_url) {
#if CONFIG_SECURE_SIGNED_ON_UPDATE
    const char *source =
        manifest_url && manifest_url[0] ? manifest_url : CONFIG_PACE_OTA_MANIFEST_URL;
    if (!https_url(source))
        return ESP_ERR_NOT_SUPPORTED;
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK ||
        state != ESP_OTA_IMG_VALID)
        return ESP_ERR_INVALID_STATE;
    if (progress)
        progress(UpdateStage::checking, 0, context);
    char body[2048];
    std::size_t size;
    int status;
    auto error = fetch(source, nullptr, false, body, sizeof(body),
                       esp_timer_get_time() / 1000 + 15000, size, status);
    if (error != ESP_OK)
        return error;
    Json json(body, size, sizeof(body));
    auto *root = json.get();
    char board[80]{}, url[513]{}, digest[65]{};
    Download download;
    auto *expected_size = cJSON_GetObjectItemCaseSensitive(root, "size");
    auto *schema = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
    download.partition = esp_ota_get_next_update_partition(nullptr);
    if (!download.partition || !cJSON_IsNumber(schema) || schema->valuedouble != 1 ||
        !text(root, "board_profile", board, sizeof(board)) ||
        std::strcmp(board, PACE_BOARD_PROFILE) ||
        !text(root, "version", download.version, sizeof(download.version)) ||
        !download.version[0] || !text(root, "image_url", url, sizeof(url)) || !https_url(url) ||
        origin(url) != origin(source) || !text(root, "sha256", digest, sizeof(digest)) ||
        std::strlen(digest) != 64 || !cJSON_IsNumber(expected_size) ||
        expected_size->valuedouble < sizeof(download.header) ||
        expected_size->valuedouble > download.partition->size ||
        std::floor(expected_size->valuedouble) != expected_size->valuedouble)
        return ESP_ERR_INVALID_ARG;
    if (!std::strcmp(download.version, esp_app_get_description()->version))
        return ESP_ERR_INVALID_VERSION;
    unsigned char expected_hash[32];
    for (unsigned i = 0; i < 32; ++i) {
        auto hex = [](char c) -> int {
            return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        };
        const int high = hex(digest[2 * i]), low = hex(digest[2 * i + 1]);
        if (high < 0 || low < 0)
            return ESP_ERR_INVALID_ARG;
        expected_hash[i] = (high << 4) | low;
    }
    download.expected = static_cast<std::size_t>(expected_size->valuedouble);
    download.progress = progress;
    download.context = context;
    mbedtls_sha256_init(&download.hash);
    if (mbedtls_sha256_starts(&download.hash, 0) != 0) {
        mbedtls_sha256_free(&download.hash);
        return ESP_FAIL;
    }
    error = transfer(url, nullptr, false, static_cast<std::size_t>(expected_size->valuedouble),
                     esp_timer_get_time() / 1000 + 300000, write_image, &download, size, status);
    unsigned char actual_hash[32];
    if (mbedtls_sha256_finish(&download.hash, actual_hash) != 0)
        error = ESP_FAIL;
    mbedtls_sha256_free(&download.hash);
    if (error == ESP_OK && (size != static_cast<std::size_t>(expected_size->valuedouble) ||
                            std::memcmp(actual_hash, expected_hash, 32) || !download.started))
        error = ESP_ERR_INVALID_CRC;
    if (error != ESP_OK) {
        if (download.started)
            esp_ota_abort(download.handle);
        return error;
    }
    // SDK verifies image/chip and signature here. No boot selection on failure.
    if (progress)
        progress(UpdateStage::verifying, 100, context);
    error = esp_ota_end(download.handle);
    if (error == ESP_OK)
        error = esp_ota_set_boot_partition(download.partition);
    return error;
#else
    (void)progress;
    (void)context;
    (void)manifest_url;
    return ESP_ERR_NOT_SUPPORTED; // Fail closed in development builds.
#endif
}

esp_err_t install_uploaded_image(std::size_t size, UploadReader reader, void *source,
                                 UpdateProgress progress, void *context) {
#if CONFIG_SECURE_SIGNED_ON_UPDATE
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK ||
        state != ESP_OTA_IMG_VALID)
        return ESP_ERR_INVALID_STATE;
    Download download;
    download.partition = esp_ota_get_next_update_partition(nullptr);
    if (!reader || !download.partition || size < sizeof(download.header) ||
        size > download.partition->size)
        return ESP_ERR_INVALID_SIZE;
    download.expected = size;
    download.has_manifest = false;
    download.progress = progress;
    download.context = context;
    // The SDK verifies the signed image digest. No redundant whole-file hash
    // is needed when there is no manifest digest to compare against.
    auto error = reader(write_image, &download, source);
    if (error == ESP_OK && (!download.started || download.received != size))
        error = ESP_ERR_INVALID_SIZE;
    if (error != ESP_OK) {
        if (download.started)
            esp_ota_abort(download.handle);
        return error;
    }
    if (progress)
        progress(UpdateStage::verifying, 100, context);
    error = esp_ota_end(download.handle);
    return error == ESP_OK ? esp_ota_set_boot_partition(download.partition) : error;
#else
    (void)size;
    (void)reader;
    (void)source;
    (void)progress;
    (void)context;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

} // namespace pace::services
