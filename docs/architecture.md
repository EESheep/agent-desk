# 分层设计与真实数据接入

## 已实现的第一阶段

- ESP-IDF 6.1 工程，以固定版本 Waveshare RGB/GT911 驱动为基础迁移到新 API。
- LVGL 9 横屏布局：会话列表、需要关注筛选、其他信息卡片。
- 最多 8 个会话、4 个信息卡片；列表可纵向滚动，审批优先显示。
- 8 秒切换一次模拟审批状态，串口打印模拟状态心跳。
- 明确显示 DEMO DATA，未接入 Codex、Wi-Fi 或真实审批。
- 当前使用内置英文字体，避免中文方框；下一阶段需要按中文字符集生成/选择中文字库。字符数组长度按 UTF-8 字节计，不按汉字个数计。

## 模块职责

1. `firmware/board`：硬件驱动，不放业务数据。
2. `panel_model.{h,c}`：固定大小的数据模型、状态文案、模拟数据，无 LVGL 依赖。
3. `panel_ui.{h,c}`：只渲染快照；所有操作在 LVGL 回调或 adapter 锁内执行。
4. `main.c`：设备启动和模拟源。接入网络后，用队列传入快照替代 `demo_tick`。
5. 后续 `bridge`：在电脑上获取状态，只输出脱敏后的只读信息。

## Codex 接入证据与限制

已核对 [官方 Codex App Server 文档](https://learn.chatgpt.com/docs/app-server)（2026-09-02）：

- `thread/status/changed` 会给出已加载会话的运行时状态。
- `activeFlags` 包含 `waitingOnApproval` 时可标记需要审批。
- 审批请求以及 `serverRequest/resolved` 可用于关联等待/解除生命周期。

重要：启动一个独立 app-server 并不等于自动订阅桌面 App 中现有会话的实时状态。必须验证当前桌面版连接方式、会话归属和订阅范围，不能把历史文件里出现过的审批当成当前待审批。现阶段只确认协议能力，没有实现或验证桌面 App 连接。

已加入最小只读探针 `tools/codex_status_probe.py`。它只使用 Python 标准库和官方 App Server JSON-RPC：可以读取桌面任务持久化的 ID、名称和更新时间；实测当前桌面任务在独立服务中为 `notLoaded`，因此将 `needsApproval` 输出为 `null`（未知），绝不误报为“不需要审批”。运行：

```powershell
python tools/codex_status_probe.py
python tools/codex_status_probe.py --self-test
```

若要在屏幕上安全提交真实审批，桥接程序必须成为产生该审批请求的 App Server 客户端，或者 OpenAI 提供可连接桌面宿主的公开接口。轮询历史文件只能做只读展示，不能用于批准操作。

当前最小数据链路已实现：电脑端探针 → UART1/COM3 → 固件固定上限快照 → LVGL UI。只有需要脱离 USB 运行时才增加 Wi-Fi、鉴权和网络任务。

设备端保留 `needs_approval` 独立标志，不用一个状态枚举覆盖所有状态。
断线/数据过期和任务状态分开显示；超过 15 秒的实时快照标为 STALE DATA。`age_seconds` 必须由未来的接收任务根据单调时钟持续维护。
空闲不代表完成；状态未知不代表正常；等待用户输入与等待工具审批分开处理。

## 其他内容如何扩展

向 `panel_snapshot_t.cards` 增加标题、值、说明即可展示简单的时间/服务/传感器信息。
复杂页面（图表、播放器、天气）应新增 UI 模块，不要把设备驱动或联网逻辑写到标签更新函数里。

## 尚未实现

- 硬件验收（目标构建状态见 validation.md）。
- Wi-Fi 配网、NVS、时间同步、重连、TLS/鉴权。
- 真实 Codex 状态桥接、中文字体、OTA。
- 触控批准操作：首版不做，仍在电脑上审批。
