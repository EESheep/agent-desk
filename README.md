# Agent Desk

**放在桌面的 AI 编程助手状态屏。**

Agent Desk 将 Codex、Kimi Code 的会话活动和 Codex 账户额度显示在独立的触控屏上。多个会话同时进行时，抬眼就能查看哪些正在运行、哪些最近结束，以及额度何时重置，减少来回切换电脑窗口。

电脑端负责采集数据，ESP32 负责显示和触控。设备是一块只读提醒屏，不在板上运行 AI 编程助手，也不执行审批或控制会话。

## 能做什么

- **集中查看会话**：合并显示 Codex 与 Kimi Code 的本地会话，查看运行、最近结束和空闲等状态。
- **触控查看详情**：从会话列表进入详情；两款设备分别采用适合屏幕尺寸的布局。
- **关注账户额度**：显示 Codex 剩余额度、额度周期和重置时间。
- **识别数据过期**：电脑停止同步时保留上次数据，并提示数据已过期。

当前已接入 Codex 和 Kimi Code 会话；额度仅接入 Codex。DeepSeek Harness（DSH）保留了界面入口，数据适配器尚未实现。

## 支持的设备

| | LCD-7 | AMOLED 2.41 V2 |
| --- | --- | --- |
| Waveshare 型号 | ESP32-S3-Touch-LCD-7 | ESP32-S3-Touch-AMOLED-2.41 V2 |
| 屏幕 | 7 英寸，800×480 | 2.41 英寸，600×450 |
| 数据连接 | USB 串口（UART1） | Wi-Fi 局域网 |
| 界面 | 软件筛选、会话列表、关注页、信息卡 | 会话分页、可滚动详情、独立额度页 |
| 中文显示 | 尚无完整中文字库 | 基本汉字和常用标点 |
| 固件目录 | [`firmware/`](firmware/) | [`firmware-amoled-2.41-v2/`](firmware-amoled-2.41-v2/) |
| 上手文档 | [连接、构建与烧录](docs/operations.md) | [配置、构建与烧录](docs/ESP32-S3-Touch-AMOLED-2.41-V2/implementation.md) |

两款设备使用独立固件，共用电脑端采集逻辑。AMOLED 型号须为 **V2**；LCD-7 固件不适用于 7B/7C。

## 工作方式

```text
电脑上的 Codex / Kimi Code
            │
      Python 状态采集
            │
            ├── USB 串口 ────── LCD-7
            │
            └── 局域网 HTTP ─── AMOLED 2.41 V2
```

Codex 活动优先读取本地回合数据库，兼容旧日志；Kimi Code 活动从本机会话文件推导。服务商账户凭据留在电脑，屏幕接收用于显示的状态快照。

状态反映的是采集到的会话活动：“最近结束”不保证任务成功，“空闲”不代表项目完成。具体判定规则见[架构与数据协议](docs/architecture.md)，AMOLED 的网络行为见[实现说明](docs/ESP32-S3-Touch-AMOLED-2.41-V2/implementation.md)。

## 开始使用

电脑端目前以 **Windows / PowerShell** 为开发和验证环境。需要可运行且已登录的 Codex，以及安装了 `pyserial` 的 Python；Kimi Code 是可选数据源，自动读取本机 `~/.kimi-code` 中的会话文件。

首次使用请先按上表中对应设备的文档完成烧录和配置。**已烧录并配置好的设备**，日常只需在项目根目录运行对应命令。

### LCD-7：USB 同步

用数据线连接板载 USB 转串口接口，将选择开关置于 **UART1**，关闭占用串口的调试或烧录工具，然后运行：

```powershell
python tools/codex_status_probe.py --watch
```

程序在启动时自动查找 CH343 串口。连接多个同型号串口设备时，可用 `--port COMx` 指定端口。收到设备确认后，屏幕会显示采集到的会话与额度。

### AMOLED 2.41 V2：局域网同步

先按[配置与操作](docs/ESP32-S3-Touch-AMOLED-2.41-V2/implementation.md#配置与操作)设置设备 Wi-Fi、快照地址和访问令牌，并准备本机 `local/amoled-config.json`，然后启动电脑端服务：

```powershell
python tools/amoled_server.py
```

电脑和设备需要能在局域网内互相访问，防火墙需允许设备访问服务端口（默认 TCP 8765）。设备定期获取快照，断线后自动重连。当前 HTTP 服务适用于可信局域网，使用访问令牌但不提供传输加密。

两种方式都需要保持电脑端程序运行，`Ctrl+C` 停止；目前未提供开机自启。LCD-7 串口桥接在运行中断线后需要重新启动。

## 开发与文档

固件基于 **ESP-IDF 6.1、LVGL 9.5.0**，各设备的精确依赖记录在对应固件目录的 `dependencies.lock` 中。仅修改电脑端采集逻辑时，无需重新烧录设备。

| 入口 | 内容 |
| --- | --- |
| [LCD-7 运行与维护](docs/operations.md) | 环境准备、命令参数、构建、烧录与排障 |
| [AMOLED V2 实现说明](docs/ESP32-S3-Touch-AMOLED-2.41-V2/implementation.md) | 界面、网络配置、操作步骤与验证记录 |
| [架构与数据协议](docs/architecture.md) | 状态来源、LCD-7 串口协议与扩展设计 |
| [多设备维护约定](docs/devices.md) | 固件边界与共享代码规则 |
| [LCD-7 验证记录](docs/validation.md) | 已验证内容、已知问题与待验收项目 |
| [LCD-7 硬件资料](docs/ESP32-S3-Touch-LCD-7/README.md) / [AMOLED V2 硬件资料](docs/ESP32-S3-Touch-AMOLED-2.41-V2/README.md) | 官方文档、硬件和驱动参考 |
| [电脑端采集](tools/codex_status_probe.py) / [局域网服务](tools/amoled_server.py) | 数据采集与快照服务源码 |

## 致谢与许可证

活动采集思路参考 [codex-monitor](https://github.com/manuelsh/codex-monitor)，额度读取参考 [Waveshare codex-meter](https://github.com/waveshareteam/codex-meter)。

本项目原创代码采用 [Apache-2.0](LICENSE)。第三方代码和依赖保留各自的版权与许可，详见 [NOTICE](NOTICE) 和[驱动来源说明](firmware/board/UPSTREAM.md)。
