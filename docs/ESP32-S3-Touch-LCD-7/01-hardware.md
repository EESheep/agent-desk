# 硬件速查

## 核心规格

| 项目 | 参数 |
|---|---|
| MCU | ESP32-S3，双核 Xtensa LX7，最高 240 MHz |
| 模组 | 板上标注 ESP32-S3-N16R8；16 MB Flash、8 MB PSRAM（官方规格表部分位置写 8 MB Flash，见“资料矛盾”） |
| 无线 | 2.4 GHz Wi-Fi 802.11 b/g/n、Bluetooth 5 LE |
| 屏幕 | 7 英寸 IPS，800×480，RGB 接口，RGB565/65K 色，约 345 cd/m² |
| 触控 | GT911，I²C，五点电容触控，带中断 |
| 外设 | TF、CAN、RS485、I²C、USB、UART、ADC/Sensor |
| 供电 | Type-C 5 V；典型标称功耗 5 V / 450 mA |
| 环境 | 0–65 ℃ |
| 触控版尺寸 | 192.96 × 110.76 mm |

## 与 GUI 最相关的引脚

### RGB LCD

| GPIO | 信号 | GPIO | 信号 |
|---|---|---|---|
| 0 | G3 | 1 | R3 |
| 2 | R4 | 3 | VSYNC |
| 5 | DE | 7 | PCLK |
| 10 | B7 | 14 | B3 |
| 17 | B6 | 18 | B5 |
| 21 | G7 | 38 | B4 |
| 39 | G2 | 40 | R7 |
| 41 | R6 | 42 | R5 |
| 45 | G4 | 46 | HSYNC |
| 47 | G6 | 48 | G5 |

背光使能由 CH422G 的 `EXIO2 / DISP` 控制。

### GT911 触控与 I²C

| 信号 | 引脚 |
|---|---|
| TP_IRQ | GPIO4 |
| TP_SDA / I²C SDA | GPIO8 |
| TP_SCL / I²C SCL | GPIO9 |
| TP_RST | CH422G EXIO1 |

触控、CH422G IO 扩展器和外接 I²C 接口共享 GPIO8/9。外接器件地址必须避开已有地址，电平选择也必须正确。

## 其他接口

- TF（SPI）：MOSI GPIO11、SCK GPIO12、MISO GPIO13；片选由 CH422G `EXIO4` 拉低。
- RS485：RX GPIO16、TX GPIO15，板载自动收发切换。
- CAN/TWAI：TX GPIO20、RX GPIO19；终端电阻可选。
- USB：D- GPIO19、D+ GPIO20。
- UART1 Type-C 与 UART2 排针共用 ESP32-S3 UART0（IO43/44），通过板上开关选择；UART1 经 CH343 转 USB-TTL。
- 电池：仅支持单节 3.7 V 锂电，PH2.0 2P；其他外设接口使用 HY2.0，勿混插。

## 必须注意的资源冲突

1. **USB 与 CAN 共用 GPIO19/20。** CH422G `EXIO5` 拉低进入 USB，拉高进入 CAN；默认高电平时电脑可能识别不到 USB COM。
2. **触控与外接 I²C 共用 GPIO8/9。** 总线初始化、复位顺序和锁必须统一管理。
3. **TF 卡片选不是普通 GPIO。** 必须经 CH422G `EXIO4` 控制。
4. **LVGL 非线程安全。** 官方示例在调用 LVGL API 前后使用互斥锁；联网任务不可直接跨线程改 UI。

## 资料矛盾与板卡识别

- 新文档特性和板载资源写 N16R8/16 MB Flash，但规格表仍写 8 MB；旧页面也出现 N8R8。烧录前应读取芯片信息或查看模组丝印，以实物为准。
- 不要混淆 `ESP32-S3-Touch-LCD-7`（800×480）与 `7B`（1024×600）。示例、时序和分辨率不可直接互换。
- Arduino 无显示时，官方 FAQ 首先要求检查 Flash 容量和 8 MB OPI PSRAM 是否正确启用。

