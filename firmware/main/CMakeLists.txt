idf_component_register(
    SRCS "main.c" "panel_model.c" "panel_ui.c" "../board/waveshare_rgb_lcd_port.c"
    INCLUDE_DIRS "." "../board"
    REQUIRES esp_timer esp_lcd esp_driver_gpio esp_driver_i2c esp_driver_uart)
