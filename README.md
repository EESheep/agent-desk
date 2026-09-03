# Desk Panel / ESP32-S3-Touch-LCD-7

开发目录：`C:\Users\ZHUOZhuang\Documents\lvgl-dev`

用于 800×480 Waveshare ESP32-S3-Touch-LCD-7 的 LVGL 状态屏基础工程。
已适配 **ESP-IDF 6.1 + LVGL 9.5.0**，通过 UART 显示真实 Codex 会话、本地活动提醒和官方额度。验证记录见 `docs/validation.md`。

## 界面

- Sessions：会话标题和由本地 rollout 推导的 RUNNING / IDLE / COMPLETED 状态。
- Attention：显示最近完成、等待输入和失败的会话。
- Information：活动数量、Codex 剩余额度、重置倒计时和连接状态。
- 顶部显示 WAITING / SYNCED / STALE DATA。
- 初版使用英文字体；中文标题暂取其中可显示的 ASCII 部分并附任务 ID。

## 运行真实任务桥接

保持 UART1 Type-C 连接，关闭其他占用 COM3 的串口软件，然后运行：

```powershell
C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe .\tools\codex_status_probe.py --port COM3 --watch
```

脚本每 10 秒读取任务列表和官方额度，并按 `codex-monitor` 的思路检查本地 rollout：最近 15 分钟内存在尚未结束的 turn 显示 RUNNING，结束后 2 分钟显示 COMPLETED，其余显示 IDLE。额度口径沿用 `Waveshare codex-meter` 的 `account/rateLimits/read`。这是只读提醒屏，不读取或执行审批。

## 构建

本机已安装 **ESP-IDF v6.1**，位置为 `C:\esp\v6.1\esp-idf`；工具在 `C:\Espressif\tools`。
优先从工程根目录运行构建助手（仅编译与统计大小，不烧录）：

```powershell
.\tools\build.ps1
```

或使用安装后的 ESP-IDF PowerShell 环境：

```powershell
cd C:\Users\ZHUOZhuang\Documents\lvgl-dev\firmware
idf.py --version
idf.py build
```

构建会根据 `sdkconfig.defaults` 选择 ESP32-S3，不必每次执行 `set-target`。
LVGL 固定 9.5.0，adapter 固定 0.5.2；`dependencies.lock` 保存已解析的精确依赖，应提交到版本控制。
Windows 中文环境如有编码警告，只需在当前终端设置 `$env:PYTHONUTF8 = '1'`；构建助手已设置，无需更改系统语言。

## 烧录（会覆盖当前应用固件）

在烧录前关闭 SSCOM 的 COM3；确认板卡确实是 800×480 的 LCD-7 而非 7B。
本机已确认设备为 ESP32-S3、16 MB Flash、8 MB PSRAM。关闭任务桥接和其他串口软件后执行：

```powershell
idf.py -p COM3 flash monitor
```

`Ctrl+]` 退出串口监视器。调试 UART 为 115200 8N1。
`build/desk_panel.bin` 是应用镜像，地址为 `0x10000`，不是可烧到 `0x00` 的合并整包；首次烧录应使用 `idf.py flash` 同时写入配套 bootloader 和分区表，不要把应用 bin 单独写到 `0x00`。
项目采用 8 MB Flash 的保守配置和 3 MB 单应用分区，尚未启用 OTA；PSRAM 必须是 8 MB OPI。
已迁移的驱动启用 GT911；CH422G 和触控共用新版 I2C 总线。不要改 USB/CAN 复用引脚来排查 UART 问题。
迁移前的驱动/配置保存在 `migration-backup-idf5`，仅作参考备份，不参与构建。

## 验收清单

1. 编译成功、没有未识别 Kconfig 配置项；记录 IDF 和锁文件版本。
2. 启动日志中出现 `waiting for real Codex tasks on UART`。
3. 屏幕无闪烁/偏色/错位；触控三个页签位置正确。
4. 桥接脚本输出 `panel confirmed`，屏幕从 WAITING 切换为 SYNCED 并展示真实任务。
5. 断开桥接 15 秒后显示 STALE DATA；Information 显示额度和重置倒计时。

## 扩展入口

- `firmware/main/panel_ui.c`：颜色、位置和交互。
- `firmware/main/panel_model.h`：会话和信息卡片数据结构。
- `firmware/main/panel_model.c`：模拟数据。
- `docs/architecture.md`：分层、Codex 接口证据与未实现功能。
- `docs/ESP32-S3-Touch-LCD-7`：之前整理的设备资料（历史摘要，驱动以 board/UPSTREAM.md 为准）。

## 不依赖设备的模型测试

安装主机 C 编译器和 CMake 后，可以独立运行：

```powershell
cmake -S tests -B build-host
cmake --build build-host
ctest --test-dir build-host -C Debug --output-on-failure
```

交叉编译器不能直接运行 Windows 主机测试。独立测试的运行状态见 `docs/validation.md`；构建通过也不等同于实机验证。
