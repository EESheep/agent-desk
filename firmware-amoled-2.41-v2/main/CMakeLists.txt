idf_component_register(SRCS "main.c" "panel.c" "font_cjk_28.c"
    INCLUDE_DIRS "."
    REQUIRES lcd_bsp main_config esp_wifi esp_event esp_netif nvs_flash
    esp_http_client esp_driver_usb_serial_jtag cjson esp_timer esp_lvgl_adapter lvgl)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
idf_component_get_property(cjson_lib espressif__cjson COMPONENT_LIB)
# Network snapshots have shallow schemas; bound parser recursion within the task stack.
target_compile_options(${cjson_lib} PRIVATE -UCJSON_NESTING_LIMIT -DCJSON_NESTING_LIMIT=16)
