# Agent Desk：AI 编程助手桌面状态屏

把电脑上的 Codex 与 Kimi Code 会话活动、以及 Codex 账户额度显示到 **Waveshare ESP32-S3-Touch-LCD-7（800×480）**。电脑读取数据，ESP32 负责 LVGL 界面与触控；这是只读提醒屏，不执行审批，也不在设备上运行 Codex 或 Kimi Code。

核对日期：2026-09-03。实际开发目录：`C:\Users\ZHUOZhuang\Documents\lvgl-dev`。

## 当前功能

- 合并显示 Codex 与 Kimi Code 的本地会话，两来源按更新时间交错排序、共用最多 8 条上限，支持列表滚动、点击详情和返回。
- Kimi Code 会话由本机 `~/.kimi-code` 的会话索引与日志文件推导，包含 Web 端与 CLI 会话；Kimi 无本地额度接口，不提供额度卡片。
- 根据本地日志提示运行中、最近结束和空闲；完成项使用绿色卡片和 `✓ COMPLETED` 标记。
- 显示 Codex 剩余额度、额度周期、重置倒计时和连接信息。
- 通过 UART1 USB 同步；启动时自动查找 CH343 串口，不依赖固定 COM 号。
- 超过约 15 秒未收到快照时显示 `STALE DATA`，保留旧数据供参考。
- 顶部 All / Codex / Kimi / DSH 软件筛选，底部 Sessions / Attention / Information 导航；列表、计数、详情与信息卡按来源区分。
- 全局提醒条跨软件筛选、页面和详情保持可见，优先显示最近完成；没有完成时显示需关注项，数据过期时显示过期警告。

**尚未实现**：开机自启、运行中断线重连、Wi-Fi、OTA、完整中文字体、DeepSeek Harness 的电脑端数据适配器。多软件界面和来源协议已实现；Codex 与 Kimi Code 已有真实数据，DSH 显示 NOT CONNECTED，不填充示例会话。Kimi 数据来自本机会话文件推导，不是官方状态接口。等待输入和失败的显示分支已存在，但不保证能读取桌面 App 中对应的实时状态。

## 快速开始：设备已经烧录好

1. 用数据线连接板载 USB 转串口接口，UART1/UART2 选择开关置于 **UART1**，不要接成 ESP32 原生 USB 接口。
2. 确保本机 Codex 可运行且已登录。关闭串口调试助手、烧录监视器和重复的桥接窗口，同一串口只运行一个桥接。
3. 打开普通 PowerShell，执行本机已验证的命令：

```powershell
cd C:\Users\ZHUOZhuang\Documents\lvgl-dev
& 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe' .\tools\codex_status_probe.py --watch
```

预期输出（端口号和任务数以实际为准）：

```text
Auto-selected CH343 serial port: COM3
sent 1 codex + 1 kimi task(s) to COM3
panel confirmed: I (...) desk_panel: REAL snapshot tasks=2 cards=4
```

保持窗口运行；`Ctrl+C` 停止。默认每轮等待 10 秒，另加 Codex 读取与串口处理耗时，并非严格每 10 秒一次。重启电脑或关闭桥接后需要重新执行；**自动找串口不等于开机自启**。

上面的 Python 路径属于本机 ESP-IDF 安装。其他电脑需使用安装了 `pyserial` 的 Python，并提供可运行的 Codex，详见[运行与维护](docs/operations.md)。

## 页面与状态

| 页面 | 内容 |
| --- | --- |
| Sessions | 本次收到的会话，需关注项排在前面；点击进入详情 |
| Attention | 最近结束、等待输入或失败的会话，不是审批列表 |
| Information | All 显示各软件概览；选中 Codex 后显示 ACTIVITY、CODEX LEFT、RESET IN、LINK 四张卡片；本机有 Kimi 数据时 LINK 卡让位给 Kimi 的 ACTIVITY 卡（卡片上限 4）；选中 Kimi 后显示其 ACTIVITY 卡 |

顶部 `sessions`、`running`、`completed` 按当前软件筛选计数；底部 Attention 数量包含完成、等待输入和失败。全局提醒条不受当前筛选影响，不会自动跳页；通知随原会话退出 COMPLETED 状态而消失，没有独立已读存储。

- `RUNNING`：日志推导的活动提醒，不是桌面 App 的权威实时状态。
- `COMPLETED`：最近回合结束提示，通常约两分钟后回到 `IDLE`。实现使用日志文件修改时间，且中止也归入结束提示，**不能据此认定任务成功**。
- `IDLE`：未检测到活动回合，不代表整个项目已完成。
- `SYNCED`：刚收到串口快照，不代表上游状态一定完整；`STALE DATA` 表示快照已过期。

`CODEX LEFT` 例如 `78%` 表示所选额度窗口约剩 78%；`prolite / 10080 min window` 中前者是接口返回的套餐标识，后者为 7 天窗口。`RESET IN 3d 13h` 是该窗口距离重置的剩余时间，按整小时向下取整，不是任务剩余时间。当前仅显示 `primary` 窗口。`LINK: USB UART` 是链路类型说明，不是独立连接探测；本机有 Kimi 数据时 LINK 卡被 Kimi 的 ACTIVITY 卡取代，不在 Information 页显示。

状态判定边界、计数口径和额度来源详见[架构说明](docs/architecture.md)。

## 常用命令

在项目根目录执行；以下 `python` 请替换为快速开始中的 Python 路径，或在对应虚拟环境内运行。

```powershell
python tools/codex_status_probe.py --watch                  # 自动识别，持续同步
python tools/codex_status_probe.py --port COM3 --watch      # 手动指定串口
python tools/codex_status_probe.py --port auto              # 自动识别，仅推送一次
python tools/codex_status_probe.py --watch --limit 4        # 最近更新的 4 个会话
python tools/codex_status_probe.py --watch --no-kimi        # 跳过 Kimi Code 采集
python tools/codex_status_probe.py                         # 只输出 JSON，不连接串口
python tools/codex_status_probe.py --self-test             # 无需设备/登录的自检
python -m serial.tools.list_ports -v                      # 枚举串口，不打开串口
```

自动选择条件：USB VID:PID 为 `1A86:55D3`，并且只有一个匹配端口。无匹配或多个同型号设备时会列出端口并退出，可用 `--port COMx` 指定。匹配的是串口型号，不是固件身份，不会自动试写其他串口。

## 开发与烧录

本机工具链：**ESP-IDF 6.1、LVGL 9.5.0、esp_lvgl_adapter 0.5.2**。精确依赖保存在 [dependencies.lock](firmware/dependencies.lock)。目标为 LCD-7，不要使用 7B/7C 驱动。

普通 PowerShell 中从项目根目录构建：

```powershell
.\tools\build.ps1
```

脚本通过 EIM 注册表加载已安装的 v6.1 环境。若执行策略阻止脚本，可仅对此次进程放行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1
```

烧录前停止桥接和其他串口软件。打开 **EIM 提供的 ESP-IDF PowerShell**，执行（COM3 替换成当前端口）：

```powershell
cd C:\Users\ZHUOZhuang\Documents\lvgl-dev\firmware
idf.py -p COM3 flash monitor
```

`Ctrl+]` 退出 monitor，再运行桥接。烧录会覆盖对应 Flash 分区；不要把 `desk_panel.bin` 单独写到 `0x0`，它是 `0x10000` 的应用镜像。完整步骤与排障见[运行与维护](docs/operations.md)。仅修改 Python 桥接不需要重新烧录。

## 文档与源码入口

| 入口 | 用途 |
| --- | --- |
| [运行与维护](docs/operations.md) | 参数、启动、构建、烧录、故障排查、本地 Git |
| [架构与数据协议](docs/architecture.md) | 状态推导、串口格式、容量、安全边界、扩展方案 |
| [验证记录](docs/validation.md) | 已验证内容、历史版本、待验收项目与已知问题 |
| [设备资料](docs/ESP32-S3-Touch-LCD-7/README.md) | 硬件与官方文档索引；早期设计明确标为历史资料 |
| [驱动来源](firmware/board/UPSTREAM.md) | 上游提交、许可证与 IDF 6.1 迁移说明 |
| [桥接脚本](tools/codex_status_probe.py) | 状态采集、自动串口识别、快照输出 |
| [界面](firmware/main/panel_ui.c) | 页面、颜色、计数和触控交互 |
| [模型](firmware/main/panel_model.h) | 字段长度、状态枚举、8 个会话与 4 张卡片的上限 |
| [固件入口](firmware/main/main.c) | 初始化、串口解析、快照发布与过期计时 |

## 版本管理与来源

项目仓库：[EESheep/agent-desk](https://github.com/EESheep/agent-desk)，分支 `main`，首次基线提交 `ae4385d`。`.gitignore` 排除构建产物、下载依赖、迁移备份、原始会话日志和常见凭据文件。保留 `sdkconfig.defaults` 和 `dependencies.lock`，本机生成的 `sdkconfig` 不提交。

活动采集思路参考 [codex-monitor](https://github.com/manuelsh/codex-monitor)，额度读取参考 [Waveshare codex-meter](https://github.com/waveshareteam/codex-meter)。并非把两个完整项目运行在 ESP32 上，也无需启动它们的 Web 服务。保留硬件驱动原有版权声明。账户凭据留在电脑，不发送给 ESP32。

## 许可证

本项目原创代码采用 [Apache-2.0](LICENSE)。第三方代码和依赖保留各自的版权与许可，包括驱动中的 CC0-1.0 声明；详见 [NOTICE](NOTICE) 和[驱动来源说明](firmware/board/UPSTREAM.md)。
