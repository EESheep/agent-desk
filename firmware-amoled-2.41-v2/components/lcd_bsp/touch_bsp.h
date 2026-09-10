#ifndef TOUCH_BSP_H
#define TOUCH_BSP_H

#include "driver/i2c_master.h"
#include "esp_lcd_touch_ft5x06.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_lcd_touch_handle_t bsp_touch_init(i2c_master_bus_handle_t bus_handle, uint16_t xmax, uint16_t ymax);

#ifdef __cplusplus
}
#endif


#endif