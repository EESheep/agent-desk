#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include "display_bsp.h"
#include "main_config.h"
#include "esp_lcd_sh8601.h"
#include "esp_io_expander_tca9554.h"
#include "esp_lv_adapter.h"

static const char *TAG = "Display";
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static uint8_t brightness;
static esp_io_expander_handle_t io_expander = NULL;
static lv_display_t *disp;

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t []){0x20}, 1, 0},
    {0x26, (uint8_t []){0x0A}, 1, 0},
    {0x24, (uint8_t []){0x80}, 1, 0},
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0xC2, (uint8_t []){0x00}, 1, 10},
    {0x35, (uint8_t []){0x00}, 0, 0},
    {0x51, (uint8_t []){0x00}, 1, 10},
    {0x11, (uint8_t []){0x00}, 0, 80},
    {0x2A, (uint8_t []){0x00,0x10,0x00,0xD1}, 4, 0},
    {0x2B, (uint8_t []){0x00,0x00,0x00,0x57}, 4, 0},
    {0x29, (uint8_t []){0x00}, 0, 10},
    {0x36, (uint8_t []){0x30}, 1, 0},   // V2 official landscape initialization
    {0x51, (uint8_t []){0xFF}, 1, 0},
};

esp_err_t bsp_display_brightness_init(void)
{
    bsp_display_brightness_set(100);
    return ESP_OK;
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (panel_handle == NULL)
    {
        ESP_LOGE(TAG, "Panel handle is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (brightness_percent < 0 || brightness_percent > 100)
    {
        ESP_LOGE(TAG, "Invalid brightness percentage. Should be between 0 and 100.");
        return ESP_ERR_INVALID_ARG;
    }

    brightness = (uint8_t)(brightness_percent * 255 / 100);

    uint32_t lcd_cmd = 0x51;
    lcd_cmd &= 0xff;
    lcd_cmd <<= 8;
    lcd_cmd |= 0x02 << 24;
    uint8_t param = brightness;
    esp_lcd_panel_io_tx_param(io_handle, lcd_cmd, &param, 1);

    return ESP_OK;
}

int bsp_display_brightness_get(void)
{
    if (panel_handle == NULL)
    {
        ESP_LOGE(TAG, "Panel handle is not initialized");
        return -1;
    }

    return brightness * 100 / 255;
}

static esp_err_t bsp_display_exio_init(i2c_master_bus_handle_t i2c_handle) {
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(i2c_handle, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_0, IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_1, IO_EXPANDER_OUTPUT));
    return ESP_OK;
}

static esp_err_t bsp_display_exio_reset(uint32_t pin_num) {
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, pin_num, 1));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, pin_num, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, pin_num, 1));
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_OK;
}

static esp_err_t bsp_display_panel_reset(void) {
    return bsp_display_exio_reset(IO_EXPANDER_PIN_NUM_0);
}

esp_err_t bsp_display_touch_reset(void) {
    return bsp_display_exio_reset(IO_EXPANDER_PIN_NUM_1);
}

void bsp_display_init(i2c_master_bus_handle_t i2c_handle,size_t max_transfer_sz)
{
    ESP_LOGI(TAG, "SPI BUS init");
    bsp_display_exio_init(i2c_handle);
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = LCD_CLK_PIN;
    buscfg.data0_io_num = LCD_D0_PIN;
    buscfg.data1_io_num = LCD_D1_PIN;
    buscfg.data2_io_num = LCD_D2_PIN;
    buscfg.data3_io_num = LCD_D3_PIN;
    buscfg.max_transfer_sz = max_transfer_sz;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = LCD_CS_PIN;
    io_config.dc_gpio_num = -1;
    io_config.spi_mode = 0;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 2;
    io_config.on_color_trans_done = NULL;
    io_config.user_ctx = NULL;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_NUM, &io_config, &io_handle));

    sh8601_vendor_config_t vendor_config = {};
    vendor_config.init_cmds = lcd_init_cmds;
    vendor_config.init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]);
    vendor_config.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = LCD_RST_PIN;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config;

	ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
	ESP_ERROR_CHECK(bsp_display_panel_reset());
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    //ESP_ERROR_CHECK(esp_lcd_panel_set_gap(*panel_handle, 0, 0x14));
}

#if LVGL_VERSION_MAJOR >= 9
static void rounder_event_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;

    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // round the start of coordinate down to the nearest 2M number
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    // round the end of coordinate up to the nearest 2N+1 number
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}
#else
static void bsp_lvgl_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;

    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // round the start of coordinate down to the nearest 2M number
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    // round the end of coordinate up to the nearest 2N+1 number
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}
#endif

lv_display_t * bsp_display_lcd_init(void) {
    esp_lv_adapter_config_t cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(esp_lv_adapter_init(&cfg));
    esp_lv_adapter_display_config_t disp_cfg = ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(
        panel_handle,           	// LCD 面板句柄
        io_handle,        			// LCD 面板 IO 句柄（某些接口可为 NULL）
        LCD_WIDTH,             		// 水平分辨率
        LCD_HEIGHT,             	// 垂直分辨率
        ESP_LV_ADAPTER_ROTATE_0 	// 旋转角度
    );
    // 20 rows = 24 KB: leave internal DMA memory for concurrent Wi-Fi operation.
    disp_cfg.profile.buffer_height = 20;
    disp = esp_lv_adapter_register_display(&disp_cfg);
    assert(disp != NULL);

#if LVGL_VERSION_MAJOR >= 9
    lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
#else
    lv_disp_t *disp_v8 = (lv_disp_t *)disp;
    if (disp_v8 && disp_v8->driver) {
        disp_v8->driver->rounder_cb = bsp_lvgl_rounder_cb;
    }
#endif

    return disp;
}

lv_indev_t * bsp_display_indev_init(esp_lcd_touch_handle_t tp) {
    const esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, tp);
    return esp_lv_adapter_register_touch(&touch_cfg);
}
