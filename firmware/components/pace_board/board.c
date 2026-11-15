// SPDX-License-Identifier: MIT
// Hardware constants checked against Waveshare's V1.2 schematic and example
// bc51a84b30c0cea018fee6e0d7427ec10102060e. Original driver using IDF's new I2C API.
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pace_board.h"
#include "pace_board_driver.h"

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t expander_config, expander_output, touch, sensor;
static uint8_t outputs = 0x2e;
static bool touch_down;
static uint16_t touch_x, touch_y;
static const char *TAG = "board";

static esp_err_t add_device(uint8_t address, i2c_master_dev_handle_t *out) {
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = address, .scl_speed_hz = 100000};
    return i2c_master_bus_add_device(bus, &config, out);
}

esp_err_t pace_board_backlight(bool on) {
    const uint8_t next = on ? outputs | 4 : outputs & (uint8_t)~4;
    esp_err_t error = i2c_master_transmit(expander_output, &next, 1, 40);
    if (error == ESP_OK)
        outputs = next;
    return error;
}

esp_err_t pace_board_init(esp_lcd_panel_handle_t *panel) {
    *panel = NULL;
    const i2c_master_bus_config_t bus_config = {.i2c_port = I2C_NUM_0,
                                                .sda_io_num = PACE_I2C_SDA,
                                                .scl_io_num = PACE_I2C_SCL,
                                                .clk_source = I2C_CLK_SRC_DEFAULT,
                                                .glitch_ignore_cnt = 7,
                                                .flags.enable_internal_pullup = true};
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &bus), TAG, "I2C initialization");
    ESP_RETURN_ON_ERROR(add_device(0x24, &expander_config), TAG, "Expander configuration");
    ESP_RETURN_ON_ERROR(add_device(0x38, &expander_output), TAG, "Expander outputs");
    ESP_RETURN_ON_ERROR(add_device(0x5d, &touch), TAG, "Touch handle");
    ESP_RETURN_ON_ERROR(add_device(PACE_SHT40_ADDRESS, &sensor), TAG, "Sensor handle");
    uint8_t command = 1;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(expander_config, &command, 1, 40), TAG,
                        "Expander mode");
    // Hold GT911 reset low; IRQ low selects 0x5d. EXIO3/5 remain at vendor levels.
    const gpio_config_t irq_config = {.pin_bit_mask = 1ULL << PACE_TOUCH_IRQ,
                                      .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&irq_config), TAG, "Touch reset selection");
    ESP_RETURN_ON_ERROR(gpio_set_level(PACE_TOUCH_IRQ, 0), TAG, "Touch address");
    command = 0x28; // Reset and backlight low.
    ESP_RETURN_ON_ERROR(i2c_master_transmit(expander_output, &command, 1, 40), TAG, "Touch reset");
    vTaskDelay(pdMS_TO_TICKS(100));
    command = 0x2a;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(expander_output, &command, 1, 40), TAG,
                        "Touch release");
    outputs = command;
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_RETURN_ON_ERROR(gpio_set_direction(PACE_TOUCH_IRQ, GPIO_MODE_INPUT), TAG, "Touch input");
    const esp_lcd_rgb_panel_config_t display = {.clk_src = LCD_CLK_SRC_DEFAULT,
                                                .timings = {.pclk_hz = 16000000,
                                                            .h_res = PACE_DISPLAY_WIDTH,
                                                            .v_res = PACE_DISPLAY_HEIGHT,
                                                            .hsync_pulse_width = 4,
                                                            .hsync_back_porch = 8,
                                                            .hsync_front_porch = 8,
                                                            .vsync_pulse_width = 4,
                                                            .vsync_back_porch = 8,
                                                            .vsync_front_porch = 8,
                                                            .flags.pclk_active_neg = true},
                                                .data_width = 16,
                                                .bits_per_pixel = 16,
                                                .num_fbs = 1,
                                                .bounce_buffer_size_px = PACE_DISPLAY_WIDTH * 10,
                                                .dma_burst_size = 64,
                                                .hsync_gpio_num = PACE_LCD_HSYNC,
                                                .vsync_gpio_num = PACE_LCD_VSYNC,
                                                .de_gpio_num = PACE_LCD_DE,
                                                .pclk_gpio_num = PACE_LCD_PCLK,
                                                .disp_gpio_num = -1,
                                                .data_gpio_nums = PACE_LCD_DATA_PINS,
                                                .flags.fb_in_psram = true};
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&display, panel), TAG, "RGB allocation");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel), TAG, "RGB reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel), TAG, "RGB start");
    return pace_board_backlight(true);
}

esp_err_t pace_board_touch(uint16_t *x, uint16_t *y, bool *pressed) {
    const uint8_t status_register[] = {0x81, 0x4e};
    uint8_t status;
    *pressed = false;
    esp_err_t error = i2c_master_transmit_receive(touch, status_register, 2, &status, 1, 40);
    if (error != ESP_OK) {
        touch_down = false;
        return error;
    }
    if (status & 0x80) {
        const unsigned count = status & 0xf;
        uint8_t point[8];
        const uint8_t point_register[] = {0x81, 0x4f};
        if (count && count <= 5) {
            error = i2c_master_transmit_receive(touch, point_register, 2, point, 8, 40);
            if (error == ESP_OK) {
                touch_x = point[1] | ((uint16_t)point[2] << 8);
                touch_y = point[3] | ((uint16_t)point[4] << 8);
            }
        }
        const uint8_t clear[] = {0x81, 0x4e, 0};
        esp_err_t cleared = i2c_master_transmit(touch, clear, 3, 40);
        touch_down = error == ESP_OK && cleared == ESP_OK && count > 0 && count <= 5 &&
                     touch_x < PACE_DISPLAY_WIDTH && touch_y < PACE_DISPLAY_HEIGHT;
        if (error == ESP_OK)
            error = cleared;
    }
    *x = touch_x;
    *y = touch_y;
    *pressed = touch_down;
    return error;
}

esp_err_t pace_board_sensor(uint8_t bytes[6]) {
    const uint8_t measure = 0xfd; // High precision, heater off.
    esp_err_t error = i2c_master_transmit(sensor, &measure, 1, 40);
    if (error != ESP_OK)
        return error;
    vTaskDelay(pdMS_TO_TICKS(10));
    return i2c_master_receive(sensor, bytes, 6, 40);
}
