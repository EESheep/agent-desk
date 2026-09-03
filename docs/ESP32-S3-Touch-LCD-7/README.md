# ESP32-S3-Touch-LCD-7 开发资料

> 历史资料（2026-09-02 调研）：以下 Wi-Fi、模拟 API、审批提示和里程碑不代表当前实现。项目现已采用 USB UART + 本地日志活动提醒 + Codex 额度，不提供审批操作。当前使用方法见[项目 README](../../README.md)，实际数据协议见[架构说明](../architecture.md)。


更新时间：2026-09-02

这份目录面向当前项目的目标：在 Waveshare ESP32-S3-Touch-LCD-7 上显示 Codex 任务状态，并醒目标识“等待审批”。内容依据 Waveshare 新文档站、旧 Wiki 和官方示例仓库整理。

## 先读结论

- 目标硬件是 **800×480 RGB565 IPS + GT911 五点电容触控 + ESP32-S3**，适合运行 LVGL 状态面板。
- 官方同时支持 Arduino 与 ESP-IDF；此项目建议以 **ESP-IDF + LVGL 9** 示例为基线。ESP-IDF 更适合持续联网、任务调度、证书存储、断线重连和后续 OTA。
- ESP32-S3 不应直接登录或持有 Codex/ChatGPT 账户凭据。推荐由电脑上的本地桥接服务读取 Codex 任务状态，转换成一个小型、只读、设备专用的 HTTP/WebSocket API；设备只持有局域网访问令牌。
- “是否需要审批”不是 Waveshare 硬件能力，而是 Codex 侧状态数据。开发前必须先验证当前 Codex App/CLI 是否存在受支持的本地事件或任务状态接口；如果没有，就需要在电脑端实现适配层，不能在设备端猜测状态。

## 文档导航

- [01-hardware.md](01-hardware.md)：硬件规格、引脚、共享资源与注意事项
- [02-toolchains-and-examples.md](02-toolchains-and-examples.md)：ESP-IDF、Arduino、LVGL 示例及烧录流程
- [03-codex-status-display-plan.md](03-codex-status-display-plan.md)：本项目建议架构、数据模型和实施顺序
- [sources.md](sources.md)：侧栏子页面、原理图、芯片资料及官方仓库索引

## 推荐的第一阶段验收

1. 烧录官方 `09_lvgl_v9_demo`，确认屏幕、背光与触控正常。
2. 连接 Wi-Fi，显示设备 IP、连接状态和最后更新时间。
3. 用电脑上的模拟 API 返回 3–10 个任务，设备显示任务列表。
4. 将 `needs_approval` 显示为高优先级颜色，并支持触控筛选。
5. 接入真实 Codex 状态源；设备仍只读，不在首版中批准或拒绝请求。
