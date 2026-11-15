// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include "pace_http.hpp"
#include <cstdint>

namespace pace::services {
enum class UpdateStage {
    idle,
    checking,
    downloading,
    verifying,
    restarting,
    current,
    failed,
    unavailable,
    needs_network
};
using UpdateProgress = void (*)(UpdateStage stage, unsigned percent, void *context);
bool update_available();
bool update_source_configured(const char *manifest_url);
esp_err_t confirm_boot_health(bool healthy);
esp_err_t update_application(UpdateProgress progress = nullptr, void *context = nullptr,
                             const char *manifest_url = nullptr);
using UploadReader = esp_err_t (*)(BodySink sink, void *target, void *source);
esp_err_t install_uploaded_image(std::size_t size, UploadReader reader, void *source,
                                 UpdateProgress progress = nullptr, void *context = nullptr);
} // namespace pace::services
