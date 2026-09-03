# Board support provenance

Source: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7

Pinned commit: `bc51a84b30c0cea018fee6e0d7427ec10102060e` (retrieved 2026-09-02).

Based on `examples/ESP-IDF/09_lvgl_v9_demo/components/waveshare_rgb_lcd_port.{c,h}`.
Keep `LICENSE.waveshare` and source SPDX notices. Do not substitute the 7B or 7C driver.

GT911 is enabled in this exact upstream header. The old Wiki excerpt showing default 0 is not the current source default.
Driver keeps the upstream RGB timings, GPIO mapping and CH422G reset sequence. Basic display and touch initialization have since been tested on the device; see [validation records](../../docs/validation.md) for the tested scope and remaining interaction checks.

## ESP-IDF 6.1 migration (2026-09-02)

- Replaced legacy I2C with a shared `i2c_master_bus_handle_t`; CH422G addresses 0x24/0x38 use separate device handles, GT911 uses the same bus.
- New transmit timeout is milliseconds. Bus/device setup cleans up a partial initialization on failure.
- RGB565 input/output formats replace bits_per_pixel; dma_burst_size replaces the old alignment fields.
- Bounce buffer length is now expressed as pixels: 800 × 10, not bytes.
- Backlight I2C initialization remains available even when touch is disabled.
- CMake explicitly depends on esp_driver_i2c and esp_driver_gpio, not the legacy driver component.
- Original first-stage files are retained in `migration-backup-idf5/`; they are not compiled.

Migration reference: https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/migration-guides/release-6.x/6.0/peripherals.html
