# ESP-IDF 6.1 迁移验证记录

日期：2026-09-02

- 工具链：ESP-IDF v6.1，Xtensa GCC 15.2.0，Python 3.13.9，esptool 5.3.1。
- 直接依赖：LVGL 9.5.0、esp_lvgl_adapter 0.5.2、esp_lcd_touch_gt911 1.2.1。完整依赖已锁定于 `firmware/dependencies.lock`。
- `idf.py -C firmware build`：完整构建通过（1980 个应用构建步骤，加引导程序）。
- `tools/build.ps1`：增量构建通过；生成 bootloader、partition-table、desk_panel.bin 和 ELF。
- 配置核对：ESP32-S3、8 MB Flash、OPI PSRAM 80 MHz、UART0/115200、LVGL FreeRTOS 后端。
- GPIO 核对：迁移前后 RGB 引脚映射完全一致。触控复位字节序列保持不变。
- `esptool image-info build/desk_panel.bin`：芯片 ESP32-S3、ESP-IDF v6.1，镜像 checksum 和 validation hash 均 valid。
- 应用镜像：644032 字节（0x9d3c0），3 MiB 分区仍有约 80% 空余。
- 引导程序：0x5820 字节，未超过 0x8000 分区表地址之前的空间。
- `desk_panel.bin` SHA256：`ad0dc00a207c01cf062b97f41eb18e651ddff577a4ed3903cd0e34bfd756ec38`。
- 链接报告静态 DIRAM 使用 89842 字节；不包含运行时显示帧缓冲、任务栈等动态分配，不能据此判断运行时峰值内存。

## 构建警告

完整配置时 ESP-IDF 自身的 BT/FATFS Kconfig 布尔默认值、重命名映射，以及 Wi-Fi/wpa_supplicant 私有头文件依赖产生通知/警告；它们未阻止构建，没有修改 SDK 源码或全局屏蔽警告。
UART 默认选项下显式波特率设置有不可见配置提示，但生成配置已核实为 UART0/115200。
Windows Unicode 提示通过构建助手的进程级 PYTHONUTF8 处理；大小报告使用 ASCII CSV 输出以避免嵌套 CMake 输出乱码。

## 首次硬件烧录与启动验证

- UART1/UART2 选择开关切换到 UART1 后，COM3 通信成功。此前端口可打开但没有芯片回应。
- 实测芯片：ESP32-S3 revision v0.2，Flash 16 MB，PSRAM 8 MB。
- 按用户要求不备份原设备固件，直接烧录；未修改 eFuse，未执行全片擦除。
- 460800 baud 写入 bootloader（0x0）、partition-table（0x8000）、desk_panel.bin（0x10000），三个文件均通过写入哈希校验，随后复位。
- 启动日志确认 ESP-IDF v6.1、PSRAM 80 MHz 及内存测试 OK，GT911 ID 0x39/0x31/0x31、配置版本 88，LVGL adapter 和触控输入注册成功。
- 约 18 秒串口观察窗口内，应用在 uptime=0/8/16 秒报告 tasks=4、approvals=1/0/1，未观察到崩溃或重启。
- 当前镜像仍按兼容性配置使用 8 MB Flash；硬件实际 16 MB，启动时会提示容量差异并使用镜像配置的 8 MB，不影响当前应用。后续扩容/OTA 时再调整配置和分区。
- LVGL 提示未启用手势识别，当前仅支持单点指针事件。

## 会话详情页更新

- 用户已确认上一版画面正常，三个标签可正常切换。
- 新增 Sessions / Attention 会话卡片点击入口、固定 Back 按钮、可滚动的完整标题/摘要、会话 ID、状态和审批提示。
- 使用会话 ID 跟踪选择；快照刷新仅更新现有详情控件，保持详情页与滚动位置。会话从快照消失时显示不可用提示，不误判为完成。
- 修正装饰子控件的点击拦截，滚动容器保留触摸命中；返回列表恢复原标签和滚动位置。
- ESP-IDF 6.1 增量构建通过。新应用镜像 645872 字节（0x9daf0），3 MiB 应用分区剩余 79%。
- 新应用 SHA256：`6e6d99bd152880953c485551b34c8d8e4e2cfbd937a18001bdfddbe7ec186b2e`；以上首次烧录段落之前的镜像大小/哈希为旧版历史记录。
- COM3 上三个固件文件均写入成功并通过哈希校验；启动日志确认新 ELF 哈希前缀 `3060fbcfa`，PSRAM 测试、GT911 与 LVGL 初始化正常。

### 详情页实机验收（待用户确认）

1. 在 Sessions 点击任意卡片的标题、状态或空白处，进入对应会话；Back 返回 Sessions。
2. 在 Attention 打开 Codex status bridge，等待至少两个 8 秒刷新周期；仍停留在该会话，审批提示和状态更新；Back 返回 Attention。
3. 详情页直接切换 Information 或 Sessions，能够退出详情页并正常展示目标标签。
4. 对超出可视区域的详情上下滑动，Back 始终可见；刷新后仍在当前详情页。当前摘要只是模型已有摘要，不是聊天记录。

## 尚未验证

- 新增详情页的实际布局、点击/滚动/返回仍需用户目视及操作确认；编译和启动日志不等于交互已验证。
- 未验证长期稳定性、运行时峰值内存。
- 独立 C 模型测试源码仍在 `tests/`，本轮没有在主机上执行；固件编译通过不等于这些单元测试通过。
- 未接入真实 Codex、Wi-Fi、中文字体或 OTA。

`desk_panel.bin` 是 0x10000 的应用镜像，不是可写到 0x0 的合并镜像；完整烧录应使用配套 bootloader 和分区表。

## 真实任务列表 UART 接入

- 电脑端探针自检通过，使用官方 App Server `thread/list` 读取到真实任务。
- 固件改用无额外依赖的 BEGIN/TASK/END 行协议；文本十六进制编码，限制 4095 字节、8 个任务。
- 首次实机启动发现解析器自检的局部快照导致 main 栈溢出；改为静态测试缓冲区后重新编译、烧录，启动稳定并返回 `app_main`。
- 桥接以 COM3/115200 每 10 秒同步；连续三次收到设备确认 `REAL snapshot tasks=1 approvals=0`。
- 当前真实任务来自独立 App Server，状态为 `notLoaded`，固件显示 UNKNOWN / APPROVAL UNKNOWN，不声称获得桌面实时审批。
- 当前未嵌入中文字库；屏幕使用标题中的 ASCII 部分加任务 ID，避免中文字形显示为方框。
