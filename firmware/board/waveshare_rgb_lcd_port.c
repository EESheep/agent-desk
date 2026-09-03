/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_check.h"

static const char *TAG = "lcd_port";
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_ch422g_config;
static i2c_master_dev_handle_t s_ch422g_output;

static esp_err_t i2c_master_init_if_needed(void)
{
    if (s_i2c_bus) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t i2c_conf = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_dev_handle_t config = NULL;
    i2c_master_dev_handle_t output = NULL;
    esp_err_t ret = i2c_new_master_bus(&i2c_conf, &bus);
    if (ret != ESP_OK) return ret;
    /* CH422G exposes separate 7-bit addresses for configuration and output. */
    i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x24,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(bus, &device, &config);
    if (ret != ESP_OK) goto fail;
    device.device_address = 0x38;
    ret = i2c_master_bus_add_device(bus, &device, &output);
    if (ret != ESP_OK) goto fail;
    s_i2c_bus = bus;
    s_ch422g_config = config;
    s_ch422g_output = output;
    return ESP_OK;
fail:
    if (config) i2c_master_bus_rm_device(config);
    i2c_del_master_bus(bus);
    return ret;
}

static esp_err_t ch422g_init_for_output(void)
{
    const uint8_t value = 0x01;
    /* New I2C API timeout is milliseconds, not FreeRTOS ticks. */
    return i2c_master_transmit(s_ch422g_config, &value, 1, I2C_MASTER_TIMEOUT_MS);
}

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911
static esp_err_t touch_reset_gpio_init(void)
{
    const gpio_config_t io_conf = {
        .pin_bit_mask = GPIO_INPUT_PIN_SEL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io_conf);
}

static esp_err_t waveshare_esp32_s3_touch_reset(void)
{
    uint8_t write_buf = 0x2C;

    ESP_ERROR_CHECK(ch422g_init_for_output());
    ESP_ERROR_CHECK(touch_reset_gpio_init());
    ESP_ERROR_CHECK(i2c_master_transmit(s_ch422g_output, &write_buf, 1, I2C_MASTER_TIMEOUT_MS));
    esp_rom_delay_us(100 * 1000);
    gpio_set_level(EXAMPLE_TOUCH_RESET_GPIO, 0);
    esp_rom_delay_us(100 * 1000);
    write_buf = 0x2E;
    ESP_ERROR_CHECK(i2c_master_transmit(s_ch422g_output, &write_buf, 1, I2C_MASTER_TIMEOUT_MS));
    esp_rom_delay_us(200 * 1000);
    return ESP_OK;
}

#endif

esp_err_t waveshare_rgb_lcd_backlight_on(void)
{
    uint8_t write_buf = 0x1E;

    ESP_ERROR_CHECK(i2c_master_init_if_needed());
    ESP_ERROR_CHECK(ch422g_init_for_output());
    return i2c_master_transmit(s_ch422g_output, &write_buf, 1, I2C_MASTER_TIMEOUT_MS);
}

esp_err_t waveshare_esp32_s3_rgb_lcd_init(esp_lv_adapter_tear_avoid_mode_t tear_mode,
                                          esp_lv_adapter_rotation_t rotation,
                                          esp_lcd_panel_handle_t *panel_handle,
                                          esp_lcd_touch_handle_t *touch_handle)
{
    if ((panel_handle == NULL) || (touch_handle == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    *panel_handle = NULL;
    *touch_handle = NULL;

    const uint8_t num_fbs = esp_lv_adapter_get_required_frame_buffer_count(tear_mode, rotation);

    ESP_LOGI(TAG, "Install RGB LCD panel driver");
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings =  {
            .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
            .h_res = EXAMPLE_LCD_H_RES,
            .v_res = EXAMPLE_LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags = {
                .pclk_active_neg = 1,
            },
        },
        .data_width = EXAMPLE_RGB_DATA_WIDTH,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = num_fbs,
        .bounce_buffer_size_px = EXAMPLE_RGB_BOUNCE_BUFFER_PIXELS,
        .dma_burst_size = 64,
        .hsync_gpio_num = EXAMPLE_LCD_IO_RGB_HSYNC,
        .vsync_gpio_num = EXAMPLE_LCD_IO_RGB_VSYNC,
        .de_gpio_num = EXAMPLE_LCD_IO_RGB_DE,
        .pclk_gpio_num = EXAMPLE_LCD_IO_RGB_PCLK,
        .disp_gpio_num = EXAMPLE_LCD_IO_RGB_DISP,
        .data_gpio_nums = {
            EXAMPLE_LCD_IO_RGB_DATA0,
            EXAMPLE_LCD_IO_RGB_DATA1,
            EXAMPLE_LCD_IO_RGB_DATA2,
            EXAMPLE_LCD_IO_RGB_DATA3,
            EXAMPLE_LCD_IO_RGB_DATA4,
            EXAMPLE_LCD_IO_RGB_DATA5,
            EXAMPLE_LCD_IO_RGB_DATA6,
            EXAMPLE_LCD_IO_RGB_DATA7,
            EXAMPLE_LCD_IO_RGB_DATA8,
            EXAMPLE_LCD_IO_RGB_DATA9,
            EXAMPLE_LCD_IO_RGB_DATA10,
            EXAMPLE_LCD_IO_RGB_DATA11,
            EXAMPLE_LCD_IO_RGB_DATA12,
            EXAMPLE_LCD_IO_RGB_DATA13,
            EXAMPLE_LCD_IO_RGB_DATA14,
            EXAMPLE_LCD_IO_RGB_DATA15,
        },
        .flags = {
            .fb_in_psram = 1,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, panel_handle));
    ESP_LOGI(TAG, "Initialize RGB LCD panel");
    ESP_ERROR_CHECK(esp_lcd_panel_init(*panel_handle));

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911
    ESP_ERROR_CHECK(i2c_master_init_if_needed());
    ESP_ERROR_CHECK(waveshare_esp32_s3_touch_reset());

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;
    ESP_LOGI(TAG, "Initialize I2C panel IO");
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_config, &tp_io_handle));

    ESP_LOGI(TAG, "Initialize touch controller GT911");
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = EXAMPLE_PIN_NUM_TOUCH_RST,
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, touch_handle));
#endif

    return ESP_OK;
}
