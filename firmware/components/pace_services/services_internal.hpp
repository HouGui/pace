// SPDX-License-Identifier: MIT
#pragma once
#include "pace_services.hpp"

namespace pace::services {
// Single network worker; reuses its existing response buffer.
void poll_traffic(Runtime &runtime, Health &health, TrafficSchedule &schedule, char *bytes,
                  std::int64_t mono);

struct UploadChunk {
    int size{}; // Positive payload, zero end, negative cancelled.
    char bytes[2048]{};
};

esp_err_t receive_upload(BodySink sink, void *target, void *source);
esp_err_t storage_init();
esp_err_t load_blob(const char *partition, const char *key, char *out, std::size_t capacity,
                    std::size_t &size);
esp_err_t save_blob(const char *partition, const char *key, const char *bytes, std::size_t size);
esp_err_t erase_user_data();
inline constexpr std::size_t kCachePrefix = 36; // PC02 + SHA-256 of source identity.
bool cache_header(const Settings &settings, char *header);
esp_err_t load_brightness(Runtime &runtime);
void save_brightness(Runtime &runtime, Health &health);
void init_maps(Runtime &runtime);
void poll_maps(Runtime &runtime, const Health &health, std::int64_t mono);
esp_err_t portal_start(Runtime &runtime, Health &health);
void portal_stop();
bool portal_expired();

} // namespace pace::services
