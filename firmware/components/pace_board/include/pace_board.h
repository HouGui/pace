// SPDX-License-Identifier: MIT
#pragma once

// Reference module wiring. Keep hardware/wiring.md and hardware/wiring.svg in sync.
#define PACE_BOARD_PROFILE "waveshare-esp32s3-touch-lcd7-800x480"

enum {
    PACE_I2C_SDA = 8,
    PACE_I2C_SCL = 9,
    PACE_TOUCH_IRQ = 4,
    PACE_SHT40_ADDRESS = 0x44,
    PACE_DISPLAY_WIDTH = 800,
    PACE_DISPLAY_HEIGHT = 480,
    PACE_LCD_HSYNC = 46,
    PACE_LCD_VSYNC = 3,
    PACE_LCD_DE = 5,
    PACE_LCD_PCLK = 7
};

// V1.2 reference profile; match the received board before flashing.
#define PACE_LCD_DATA_PINS {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40}
// Touch reset and backlight enable use CH422G outputs, not ESP32 GPIO numbers.
// GPIO21 is factory-wired LCD data. Setup uses touch; BOOT/RESET are for recovery.
