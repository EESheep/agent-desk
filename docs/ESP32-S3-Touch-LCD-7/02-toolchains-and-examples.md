# 工具链、示例与烧录

## 推荐路径：ESP-IDF

官方新文档以 Windows、VS Code 和 ESP-IDF 扩展为例，截图版本为 ESP-IDF 5.5.2，并明确要求选择与示例匹配的 IDF 版本。示例依赖通过 `idf_component.yml` 自动下载。

官方 ESP-IDF 示例：

| 示例 | 用途 |
|---|---|
| `01_I2C_Test` | I²C 扫描与寄存器读写 |
| `02_RS485_Test` | RS485 回环 |
| `03_SD_Test` | TF 卡初始化与文件操作 |
| `04_Sensor_AD` | ADC |
| `05_UART_Test` | UART 回环 |
| `06_TWAItransmit` | CAN/TWAI 发送 |
| `07_TWAIreceive` | CAN/TWAI 接收 |
| `08_lvgl_v8_demo` | LVGL 8.3 GUI |
| `09_lvgl_v9_demo` | LVGL 9.x GUI，推荐基线 |

屏幕示例的 `waveshare_rgb_lcd_port.h` 中，`CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911` 为 1 时启用触控，为 0 时关闭。官方页面的代码片段默认展示 0，触控型号要主动改为 1。

## Arduino 路径

- ESP32 board package：3.0.6 或更高可直接选择 `Waveshare ESP32-S3-Touch-LCD-7`。
- `ESP32_Display_Panel`：v0.1.4 或更高。
- `ESP32_IO_Expander`：v0.0.4 或更高。
- LVGL：v8 示例用 8.4.0；v9 示例用 9.5.0，不能混用配套压缩包和 `lv_conf.h`。
- Arduino 库路径不要含中文字符；官方 FAQ 指出这会造成 `lv_conf.h` 查找失败。

本项目涉及长期联网和并发状态更新，优先 ESP-IDF。Arduino 可用于快速确认硬件或做最小原型。

## 第一次上电/烧录

1. 建议先用标有 UART 的 Type-C 口做下载和日志；官方说明该口带自动下载电路。
2. 选择 ESP32-S3 和正确 COM 口，构建、烧录并打开串口监视。
3. 若自动下载卡在同步阶段，按住 BOOT，断电重连，进入下载模式后再放开。
4. 官方整包测试固件位于仓库 `firmware`，烧录地址 `0x00`；可先用它验证板载设备。
5. 烧录后按 RESET 运行。

若使用 ESP32 原生 USB 口，注意它与 CAN 模式复用，`EXIO5` 默认高电平可能使电脑无法识别端口。

## 建议的验证顺序

1. 官方整包固件：排除硬件损坏。
2. `01_I2C_Test`：确认 CH422G 与 GT911 可见。
3. `09_lvgl_v9_demo`：确认 RGB 时序、PSRAM、背光与触控。
4. 在示例上增加 Wi-Fi 和 SNTP，先不要改显示驱动。
5. 将网络数据通过队列传给单一 UI/LVGL 任务。

