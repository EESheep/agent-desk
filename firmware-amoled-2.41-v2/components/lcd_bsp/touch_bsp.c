#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include "touch_bsp.h"
#include "main_config.h"

static esp_lcd_touch_handle_t touch_handle;

esp_lcd_touch_handle_t bsp_touch_init(i2c_master_bus_handle_t bus_handle, uint16_t xmax, uint16_t ymax)
{
    esp_lcd_panel_io_i2c_config_t touch_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = ymax - 1,
        .y_max = xmax - 1,
        .rst_gpio_num = TP_RST_PIN,
        .int_gpio_num = TP_INT_PIN,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 1,
            .mirror_x = 0,
            .mirror_y = 1,
        },
    };
    esp_lcd_panel_io_handle_t touch_io_handle = NULL;
    touch_io_config.scl_speed_hz = 400 * 1000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus_handle, &touch_io_config, &touch_io_handle));
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(touch_io_handle, &tp_cfg, &touch_handle));
    return touch_handle;
}
