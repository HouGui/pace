// SPDX-License-Identifier: MIT
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "mbedtls/sha256.h"
#include "pace/protocol.hpp"
#include "pace_board.h"
#include "pace_http.hpp"
#include "pace_ota.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
unsigned checks{};
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                                        \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
esp_partition_t running{3 * 1024 * 1024}, inactive{3 * 1024 * 1024};
esp_app_desc_t installed;
esp_ota_img_states_t boot_state;
esp_err_t state_error{}, begin_error{}, write_error{}, end_error{}, select_error{}, valid_error{},
    fetch_error{}, transfer_error{};
unsigned begins{}, aborts{}, ends{}, selections{}, validations{}, rollbacks{}, downloads{};
bool partition_missing{};
std::string image, manifest, written;
std::size_t chunk_size = 13, cut_at;
std::vector<pace::services::UpdateStage> stages;

std::string hash(const std::string &input) {
    unsigned char bytes[32];
    unsigned length;
    CHECK(EVP_Digest(input.data(), input.size(), bytes, &length, EVP_sha256(), nullptr) == 1);
    std::string result;
    for (auto byte : bytes) {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 15];
    }
    return result;
}

void reset() {
    begins = aborts = ends = selections = validations = rollbacks = downloads = 0;
    state_error = begin_error = write_error = end_error = select_error = valid_error = fetch_error =
        transfer_error = ESP_OK;
    partition_missing = false;
    boot_state = ESP_OTA_IMG_VALID;
    chunk_size = 13;
    stages.clear();
    written.clear();
    installed = {};
    installed.magic_word = ESP_APP_DESC_MAGIC_WORD;
    std::strcpy(installed.version, "1.0");
    std::strcpy(installed.project_name, "pace");
    auto candidate = installed;
    std::strcpy(candidate.version, "1.1");
    image.assign(1024, '\x5a');
    std::memcpy(image.data() + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
                &candidate, sizeof(candidate));
    cut_at = image.size();
    manifest = "{\"schema_version\":1,\"board_profile\":\"" PACE_BOARD_PROFILE
               "\",\"version\":\"1.1\",\"image_url\":\"https://updates.example.invalid/"
               "image.bin\",\"size\":1024,\"sha256\":\"" +
               hash(image) + "\"}";
}

[[maybe_unused]] void field(const char *key, cJSON *value) {
    auto *j = cJSON_Parse(manifest.c_str());
    CHECK(j);
    cJSON_ReplaceItemInObjectCaseSensitive(j, key, value);
    char *text = cJSON_PrintUnformatted(j);
    CHECK(text);
    manifest = text;
    cJSON_free(text);
    cJSON_Delete(j);
}

esp_err_t upload_reader(pace::services::BodySink sink, void *target, void *) {
    std::size_t received = 0;
    while (received < std::min(cut_at, image.size())) {
        const auto n = std::min(chunk_size, std::min(cut_at, image.size()) - received);
        const auto error = sink(image.data() + received, n, target);
        if (error != ESP_OK)
            return error;
        received += n;
    }
    return transfer_error;
}

esp_err_t run() {
    return pace::services::update_application([](auto stage, unsigned percent, void *) {
        CHECK(percent <= 100);
        stages.push_back(stage);
    });
}
} // namespace

std::int64_t esp_timer_get_time() { return 1000000; }

const esp_app_desc_t *esp_app_get_description() { return &installed; }

const esp_partition_t *esp_ota_get_running_partition() { return &running; }

const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *) {
    return partition_missing ? nullptr : &inactive;
}

esp_err_t esp_ota_get_state_partition(const esp_partition_t *, esp_ota_img_states_t *state) {
    *state = boot_state;
    return state_error;
}

esp_err_t esp_ota_begin(const esp_partition_t *p, std::size_t, esp_ota_handle_t *handle) {
    CHECK(p == &inactive);
    ++begins;
    *handle = 1;
    return begin_error;
}

esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, std::size_t size) {
    CHECK(handle == 1);
    written.append(static_cast<const char *>(data), size);
    return write_error;
}

esp_err_t esp_ota_abort(esp_ota_handle_t) {
    ++aborts;
    return ESP_OK;
}

esp_err_t esp_ota_end(esp_ota_handle_t) {
    ++ends;
    return end_error;
}

esp_err_t esp_ota_set_boot_partition(const esp_partition_t *) {
    ++selections;
    return select_error;
}

esp_err_t esp_ota_mark_app_valid_cancel_rollback() {
    ++validations;
    return valid_error;
}

esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot() {
    ++rollbacks;
    return ESP_OK;
}

namespace pace::services {
esp_err_t fetch(const char *, const char *post, bool redirect, char *out, std::size_t capacity,
                std::int64_t deadline, std::size_t &size, int &status,
                const RequestHeaders *headers) {
    CHECK(!headers);
    CHECK(!post && !redirect && deadline == 16000);
    size = manifest.size();
    status = 200;
    if (fetch_error != ESP_OK)
        return fetch_error;
    if (size > capacity)
        return ESP_ERR_INVALID_SIZE;
    std::memcpy(out, manifest.data(), size);
    return ESP_OK;
}

esp_err_t transfer(const char *, const char *post, bool redirect, std::size_t limit,
                   std::int64_t deadline, BodySink sink, void *context, std::size_t &received,
                   int &status, const RequestHeaders *headers) {
    CHECK(!headers);
    ++downloads;
    CHECK(!post && !redirect && deadline == 301000);
    received = 0;
    status = 200;
    while (received < std::min(cut_at, image.size())) {
        const auto n = std::min(chunk_size, std::min(cut_at, image.size()) - received);
        if (received + n > limit)
            return ESP_ERR_INVALID_SIZE;
        const auto error = sink(image.data() + received, n, context);
        if (error != ESP_OK)
            return error;
        received += n;
    }
    return transfer_error;
}
} // namespace pace::services

int main() {
    using namespace pace::services;
    reset();
#ifdef PACE_TEST_UNSIGNED
    CHECK(!update_available());
    CHECK(run() == ESP_ERR_NOT_SUPPORTED);
    CHECK(!begins && !selections && !downloads);
    CHECK(install_uploaded_image(1024, upload_reader, nullptr) == ESP_ERR_NOT_SUPPORTED);
#else
    CHECK(update_available());
    for (auto chunk : {1u, 13u, 288u, 1024u}) {
        reset();
        chunk_size = chunk;
        CHECK(run() == ESP_OK);
        CHECK(written == image && begins == 1 && ends == 1 && selections == 1 && !aborts);
        CHECK(stages.front() == UpdateStage::checking && stages.back() == UpdateStage::verifying);
    }
    for (const char *url :
         {"http://updates.example.invalid/image.bin", "https://other.example.invalid/image.bin",
          "https://updates.example.invalid:443/image.bin",
          "https://updates.example.invalid.evil/image.bin"}) {
        reset();
        field("image_url", cJSON_CreateString(url));
        CHECK(run() != ESP_OK && !begins && !selections && !downloads);
    }
    for (auto size : {0.0, 287.0, 1024.5, 3145729.0}) {
        reset();
        field("size", cJSON_CreateNumber(size));
        CHECK(run() != ESP_OK && !begins && !selections);
    }
    for (const char *key : {"board_profile", "version", "sha256"}) {
        reset();
        field(key, cJSON_CreateString("invalid"));
        CHECK(run() != ESP_OK && !begins && !selections);
    }
    reset();
    field("version", cJSON_CreateString("1.0"));
    CHECK(run() == ESP_ERR_INVALID_VERSION && !downloads);
    reset();
    field("schema_version", cJSON_CreateNumber(2));
    CHECK(run() != ESP_OK && !downloads);
    reset();
    manifest = "{\"schema_version\":1,\"schema_version\":1}";
    CHECK(run() != ESP_OK && !downloads);
    reset();
    partition_missing = true;
    CHECK(run() != ESP_OK && !downloads);
    reset();
    boot_state = ESP_OTA_IMG_PENDING_VERIFY;
    CHECK(run() == ESP_ERR_INVALID_STATE && !downloads);
    reset();
    fetch_error = ESP_ERR_TIMEOUT;
    CHECK(run() == ESP_ERR_TIMEOUT && !begins);
    reset();
    image.back() ^= 1;
    CHECK(run() == ESP_ERR_INVALID_CRC && aborts == 1 && !ends && !selections);
    reset();
    image[32] ^= 1;
    CHECK(run() == ESP_ERR_INVALID_ARG && !begins && !selections);
    reset();
    image[48] ^= 1;
    CHECK(run() == ESP_ERR_INVALID_ARG && !begins && !selections);
    reset();
    image[80] ^= 1;
    CHECK(run() == ESP_ERR_INVALID_ARG && !begins && !selections);
    for (auto cut : {12u, 300u, 1023u}) {
        reset();
        cut_at = cut;
        CHECK(run() == ESP_ERR_INVALID_CRC && !selections && !ends);
        CHECK(aborts == (cut >= 288 ? 1u : 0u));
    }
    reset();
    cut_at = 400;
    transfer_error = ESP_ERR_TIMEOUT;
    CHECK(run() == ESP_ERR_TIMEOUT && aborts == 1 && !selections);
    reset();
    begin_error = ESP_FAIL;
    CHECK(run() == ESP_FAIL && !aborts && !selections);
    reset();
    write_error = ESP_FAIL;
    CHECK(run() == ESP_FAIL && aborts == 1 && !ends && !selections);
    // Model SDK signature/chip validation rejecting the candidate. SDK crypto is not simulated.
    reset();
    end_error = ESP_FAIL;
    CHECK(run() == ESP_FAIL && ends == 1 && !selections && !aborts);
    reset();
    select_error = ESP_FAIL;
    CHECK(run() == ESP_FAIL && ends == 1 && selections == 1);
    reset();
    CHECK(install_uploaded_image(image.size(), upload_reader, nullptr) == ESP_OK);
    CHECK(written == image && selections == 1 && !downloads);
    reset();
    cut_at = 400;
    CHECK(install_uploaded_image(image.size(), upload_reader, nullptr) == ESP_ERR_INVALID_SIZE &&
          aborts == 1 && !selections);
    reset();
    cut_at = 400;
    transfer_error = ESP_ERR_TIMEOUT;
    CHECK(install_uploaded_image(image.size(), upload_reader, nullptr) == ESP_ERR_TIMEOUT &&
          aborts == 1 && !selections);
    reset();
    CHECK(install_uploaded_image(3145729, upload_reader, nullptr) == ESP_ERR_INVALID_SIZE &&
          !begins);
    reset();
    CHECK(install_uploaded_image(300, upload_reader, nullptr) == ESP_ERR_INVALID_SIZE &&
          !selections);
    reset();
    end_error = ESP_FAIL;
    CHECK(install_uploaded_image(image.size(), upload_reader, nullptr) == ESP_FAIL && !selections);
    reset();
    std::memcpy(image.data() + 32, &installed, sizeof(installed));
    CHECK(install_uploaded_image(image.size(), upload_reader, nullptr) == ESP_ERR_INVALID_VERSION &&
          !begins);
    reset();
    image[80] ^= 1;
    CHECK(install_uploaded_image(image.size(), upload_reader, nullptr) == ESP_ERR_INVALID_ARG &&
          !begins);
#endif
    reset();
    boot_state = ESP_OTA_IMG_PENDING_VERIFY;
    CHECK(confirm_boot_health(true) == ESP_OK && validations == 1 && !rollbacks);
    reset();
    boot_state = ESP_OTA_IMG_PENDING_VERIFY;
    CHECK(confirm_boot_health(false) == ESP_OK && rollbacks == 1 && !validations);
    reset();
    state_error = ESP_ERR_NOT_FOUND;
    CHECK(confirm_boot_health(true) == ESP_OK && selections == 1 && validations == 1);
    reset();
    boot_state = ESP_OTA_IMG_UNDEFINED;
    CHECK(confirm_boot_health(true) == ESP_OK && selections == 1 && validations == 1);
    reset();
    state_error = ESP_ERR_NOT_FOUND;
    select_error = ESP_FAIL;
    CHECK(confirm_boot_health(true) == ESP_FAIL && !validations);
    reset();
    boot_state = ESP_OTA_IMG_PENDING_VERIFY;
    valid_error = ESP_FAIL;
    CHECK(confirm_boot_health(true) == ESP_FAIL);
    reset();
    CHECK(confirm_boot_health(true) == ESP_OK && !selections && !validations && !rollbacks);
    std::printf("PASS: OTA transaction / boot health (%u assertions)\n", checks);
}
