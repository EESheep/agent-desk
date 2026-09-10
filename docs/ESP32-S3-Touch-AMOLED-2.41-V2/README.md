# ESP32-S3-Touch-AMOLED-2.41 V2 开发资料

记录日期：2026-09-10。用户已确认实物为 **V2**。与 ESP32-S3-Touch-LCD-7 分开维护。独立固件已基于 V2 驱动完成构建和烧录，已通过局域网接收真实快照，详见[实现、操作与验证](implementation.md)。下文保留官方基线与待核对事项。

## 硬件基线

| 项目 | 配置 |
| --- | --- |
| 主控 | ESP32-S3R8，双核 LX7，240 MHz |
| 存储 | 16 MB Flash、8 MB PSRAM |
| 屏幕 | 2.41 英寸 AMOLED，600×450，RM690B0，QSPI |
| 触摸 | FT6336，I²C |
| 外设 | PCF85063 RTC、QMI8658 六轴 IMU、Micro SD 卡槽、锂电池充放电 |
| USB | ESP32-S3 原生 USB，D− GPIO19、D+ GPIO20 |

V2 专属映射：TP_INT=GPIO3、TP_RESET=EXIO1、OLED_RESET=EXIO0、OLED_TE=GPIO21、RTC_INT=EXIO2、EXIO_INT=GPIO18。EXIO 是扩展 IO，不能作为 ESP32 GPIO 编号使用。完整引脚及电源初始化以 V2 原理图和示例为准，不能套用 V1 或 LCD-7 驱动。

## 开发环境与待验证项

- 官方 Arduino 文档列出 ESP32 支持包 ≥3.0.7、配套 LVGL 8.4.0；V2 选 ESP32S3 Dev Module，Flash、PSRAM、USB 参数按 V2 仓库的 Tools Configuration.png 核对。
- 官方 ESP-IDF 文档建议 V2 使用 5.5.x；其 `09_LVGL_V9_Test` 配置为 5.5.2，依赖锁定文件记录 5.5.4。具体版本需下载示例后复核。
- 既有 LCD-7 工程采用 IDF 6.1 / LVGL 9.5.0，不视为这块板已验证的组合。
- 原生 USB 与 LCD-7 的 CH343 链路不同；现有桥接的自动识别条件不能直接用于本设备，需核对实际枚举和固件接收方式。
- 首次开发先验证显示、触摸、方向、亮度与 USB，再接入业务界面。横竖屏坐标及触摸映射以实测为准。
- RGB565 全屏缓冲为 540,000 字节，双缓冲为 1,080,000 字节（计算值）。缓冲位置、DMA 限制和刷新策略需按驱动验证。

## 官方资料

- [硬件及 V1/V2 差异](https://docs.waveshare.net/ESP32-S3-Touch-AMOLED-2.41/)
- [原理图、芯片手册及分版本示例](https://docs.waveshare.net/ESP32-S3-Touch-AMOLED-2.41/Resources-And-Documents/)
- [Arduino 开发](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.41/Arduino)
- [ESP-IDF 开发](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.41/ESP-IDF)
- [V2 官方源码仓库](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.41-V2)

资料中的操作步骤是参考资料，不代表用户已要求执行安装、烧录或接线操作。
代码组织遵循[多设备维护约定](../devices.md)。
