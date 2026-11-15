// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include "esp_lcd_types.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// All operations belong to the UI task. I2C transactions have 40 ms deadlines.
esp_err_t pace_board_init(esp_lcd_panel_handle_t *panel);
esp_err_t pace_board_backlight(bool on);
esp_err_t pace_board_touch(uint16_t *x, uint16_t *y, bool *pressed);
esp_err_t pace_board_sensor(uint8_t bytes[6]);
#ifdef __cplusplus
}
#endif
