#ifndef DISPLAY_BSP_H
#define DISPLAY_BSP_H

#include <driver/i2c_master.h>
#include "lvgl.h"
#include "esp_lcd_touch_ft5x06.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_display_brightness_init(void);
esp_err_t bsp_display_brightness_set(int brightness_percent);
int bsp_display_brightness_get(void);
void bsp_display_init(i2c_master_bus_handle_t i2c_handle,size_t max_transfer_sz);
esp_err_t bsp_display_touch_reset(void);
lv_display_t * bsp_display_lcd_init(void);
lv_indev_t * bsp_display_indev_init(esp_lcd_touch_handle_t tp);

#ifdef __cplusplus
}
#endif


#endif
