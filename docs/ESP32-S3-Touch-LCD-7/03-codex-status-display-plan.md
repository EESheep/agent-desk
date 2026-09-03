# Codex 任务状态显示方案

## 建议架构

```text
Codex App / CLI
      │ 本机可用的状态源（待验证）
      ▼
电脑端 bridge service
  - 读取/归一化任务状态
  - 判断 needs_approval
  - 隐去提示词、代码、路径、凭据
  - HTTP snapshot + WebSocket/SSE 增量
      │ 局域网 TLS 或短期 bearer token
      ▼
ESP32-S3 + Wi-Fi + LVGL
  - 只读任务列表
  - 审批醒目标识
  - 断线/数据过期提示
```

首版建议只读。把“批准/拒绝”做成设备端操作会扩大安全边界，还需要防误触、身份确认、重放保护和审计，不应与状态屏 MVP 混在一起。

## 设备 API 的最小数据模型

```json
{
  "generated_at": "2026-09-02T12:34:56+08:00",
  "bridge": { "connected": true },
  "tasks": [
    {
      "id": "opaque-id",
      "title": "LVGL develop",
      "state": "running",
      "needs_approval": false,
      "approval_summary": null,
      "updated_at": "2026-09-02T12:34:50+08:00"
    }
  ]
}
```

建议状态枚举：`running`、`waiting_input`、`needs_approval`、`completed`、`failed`、`unknown`。审批状态应独立成布尔值，避免服务端出现新增状态时设备漏报警。

## 800×480 UI 草案

- 顶栏：Codex、Wi-Fi/bridge 状态、当前时间、数据新鲜度。
- 主区：每行一个任务；标题、状态色条、最近更新时间。
- 审批：红/橙色整行高亮，固定“需要审批”文字和审批类型摘要。
- 底栏：筛选按钮（全部 / 运行中 / 待审批 / 异常）及刷新按钮。
- 断线：保留最后一次快照，同时显示“离线”和快照年龄；绝不把旧状态伪装成实时状态。

## 安全边界

- ESP32 不保存 OpenAI/Codex 主账户 token、浏览器 cookie 或会话数据。
- bridge 只输出显示必需字段，不输出任务正文、命令内容、源代码、绝对路径或工具参数。
- 设备 token 可单独撤销，并限制为只读、局域网、单设备。
- 审批提示要区分“真实待审批”和“bridge 断线/状态未知”。
- 若以后允许触控审批，至少加入二次确认、短期 challenge、签名请求和电脑端审计日志。

## 尚需验证的关键点

1. 当前 Codex 桌面版或 CLI 是否有受支持的本地任务列表/事件接口。
2. 该接口是否明确暴露待审批状态、审批类型和生命周期事件。
3. 是否能稳定关联任务 ID 与标题，以及已归档/已完成任务的保留策略。
4. bridge 应运行在 Windows 服务、当前用户进程还是项目内开发进程。

这些问题不在 Waveshare 文档范围内。硬件侧可以先用模拟 API 并行开发，但真实接入前必须用 Codex 官方/本机能力验证，不能依赖抓取 UI 文本作为长期协议。

## 实施里程碑

1. **硬件基线**：官方 LVGL 9 示例跑通，记录实物 Flash/PSRAM 和板卡版本。
2. **设备壳层**：Wi-Fi 配网、时间、断线重连、设置持久化。
3. **模拟状态源**：电脑端固定 JSON/WebSocket，完成 UI、列表和待审批视觉告警。
4. **真实 bridge**：接入经验证的 Codex 状态源，做字段脱敏和鉴权。
5. **可靠性**：心跳、指数退避、快照年龄、看门狗、异常恢复。

