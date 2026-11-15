// SPDX-License-Identifier: MIT
#include "pace_board.h"
#include "pace_services.hpp"
#include "pace_ui.hpp"

#include "esp_log.h"

namespace {
constexpr char kTag[] = "pace";
}

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "PACE; board: %s", PACE_BOARD_PROFILE);
    ESP_LOGI(kTag, "Panel: %d x %d; sensor SDA=%d SCL=%d", PACE_DISPLAY_WIDTH, PACE_DISPLAY_HEIGHT,
             PACE_I2C_SDA, PACE_I2C_SCL);
    ESP_LOGI(kTag, "Feed contract: %u", static_cast<unsigned>(pace::kFeedSchemaVersion));
    static pace::services::Runtime runtime;
    ESP_ERROR_CHECK(pace::services::start(runtime));
    pace::run_ui(runtime);
}
