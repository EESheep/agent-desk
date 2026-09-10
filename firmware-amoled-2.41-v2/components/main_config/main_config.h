#pragma once

#include <driver/gpio.h>


#define LCD_SPI_NUM         SPI2_HOST
#define BSP_I2C_NUM         I2C_NUM_0

#define LCD_WIDTH           600
#define LCD_HEIGHT          450

#define I2C_SDA_PIN     GPIO_NUM_47
#define I2C_SCL_PIN     GPIO_NUM_48

#define LCD_CS_PIN      GPIO_NUM_9
#define LCD_CLK_PIN     GPIO_NUM_10
#define LCD_D0_PIN      GPIO_NUM_11
#define LCD_D1_PIN      GPIO_NUM_12
#define LCD_D2_PIN      GPIO_NUM_13
#define LCD_D3_PIN      GPIO_NUM_14
#define LCD_RST_PIN     GPIO_NUM_NC
#define LCD_TE_PIN      GPIO_NUM_21

#define TP_RST_PIN      GPIO_NUM_NC
#define TP_INT_PIN      GPIO_NUM_3
#define TP_SDA_PIN      I2C_SDA_PIN
#define TP_SCL_PIN      I2C_SCL_PIN
