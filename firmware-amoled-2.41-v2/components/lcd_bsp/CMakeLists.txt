idf_component_register(
    SRCS
    "display_bsp.c"
    "touch_bsp.c"
    PRIV_REQUIRES
    esp_lcd
    main_config
    driver
    espressif__esp_lcd_sh8601
    espressif__esp_lcd_touch_ft5x06
    espressif__esp_lvgl_adapter
    REQUIRES
    lvgl
    INCLUDE_DIRS ".")
