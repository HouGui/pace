// SPDX-License-Identifier: MIT
#include "services_internal.hpp"

namespace pace::services {
esp_err_t load_brightness(Runtime &runtime) {
    char bytes[3]{};
    std::size_t size;
    const auto error = load_blob("nvs", "brightness", bytes, sizeof(bytes), size);
    if (error != ESP_OK)
        return error;
    const auto percent = static_cast<unsigned char>(bytes[2]);
    if (size != sizeof(bytes) || bytes[0] != 'B' || bytes[1] != '1' || percent < 20 ||
        percent > 100)
        return ESP_ERR_INVALID_ARG;
    runtime.brightness = percent;
    return ESP_OK;
}

void save_brightness(Runtime &runtime, Health &health) {
    const auto percent = runtime.brightness_pending.exchange(0);
    if (!percent)
        return;
    if (percent < 20 || percent > 100) {
        health.brightness_error = true;
        return;
    }
    if (percent == runtime.brightness) {
        health.brightness_error = false;
        return;
    }
    const char bytes[] = {'B', '1', static_cast<char>(percent)};
    health.brightness_error = save_blob("nvs", "brightness", bytes, sizeof(bytes)) != ESP_OK;
    if (!health.brightness_error)
        runtime.brightness = percent;
}
} // namespace pace::services
