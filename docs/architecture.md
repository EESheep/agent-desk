# 架构与数据协议

核对日期：2026-09-10。以下固件和串口协议针对 LCD-7；AMOLED V2 的 Wi-Fi 通路见[独立实现说明](ESP32-S3-Touch-AMOLED-2.41-V2/implementation.md)。本文描述当前源码：多软件显示与筛选已实现，真实数据已有 Codex 与 Kimi Code（Kimi 为本机文件推导，无额度接口）；早期模拟审批、Wi-Fi 和 DeepSeek Harness 适配器不代表已实现能力。使用入口见 [README](../README.md)。

## 数据流

```text
电脑：独立 Codex App Server ── 会话列表 / 额度 / 套餐 ─┐
电脑：Codex 回合 SQLite / 旧 JSONL ── 活动状态 ───────┤
电脑：.kimi-code 会话索引与 wire.jsonl ── 活动推导 ───┤
                                                    ↓
                               tools/codex_status_probe.py
                                                    ↓ USB 转串口 / 115200 8N1
设备：main.c 接收完整快照 → panel_model → panel_ui / LVGL
```

- 桥接每轮启动 `codex app-server --stdio`，读取数据后结束该子进程；不是订阅现有桌面 App 的连接。
- `firmware/board` 负责 RGB LCD、GT911、CH422G 和背光，不采集 Codex 数据。
- `main.c` 在有效 END 后持 adapter 锁替换快照，LVGL 每秒定时维护数据年龄。
- `panel_model.{h,c}` 为固定大小的数据模型；保留模拟函数和审批字段，但当前 `app_main` 不启动模拟源。
- `panel_ui.{h,c}` 渲染来源筛选、全局提醒、三页及详情。按来源 + 会话 ID 绑定详情，防止不同软件的同名 ID 串页；按来源和页面分别保留滚动位置。会话从最新列表消失时提示不可用，不判定为完成。仅年龄变化时只更新顶栏和提醒，不销毁列表触控目标。

板卡开关标签为 UART1，源码外设为 `UART_NUM_0`；两者命名不是同一层级，不应因标签而改成 `UART_NUM_1`。

## 列表范围

`thread/list` 按 `updated_at` 降序，传入 `sourceKinds=[]`，不按项目筛选。只读一次返回页，不跟随分页游标，因此不是全部历史会话。

串口模式默认 8 个，`--limit` 限制为 1–8；仅 JSON 输出模式可请求 1–100 个。需关注项排在前面，组内保留上游顺序。活动统计仅针对本次列表；屏幕能滚动不代表加载了更多任务。

两来源归并：Codex 任务保持上游返回顺序，Kimi 任务按 `updatedAt` 降序，两路按更新时间交错合并后统一截取 `--limit` 条；8 条串口上限为两来源共用，不是每个来源各 8 条。发送 TASK 和统计 ACTIVITY 使用同一份最终截断列表，不统计未发送的会话。

## 状态判定

独立 App Server 中的 `notLoaded` 不代表桌面任务未运行。判定顺序为：

1. 接口返回 `active`、`idle`、`systemError` 时直接采用，不用日志覆盖。
2. 其他状态优先只读查询 `CODEX_HOME/thread_history_1.sqlite`（默认用户目录 `.codex`），按 `rollout_ordinal` 取最新回合。`inProgress` 为 active；completed/interrupted/failed 在完成后的两分钟内分别映射 completed/completed/systemError，之后为 idle。未知状态为 unknown。
3. 数据库缺失、查询失败或没有对应回合时回退旧 JSONL；无有效证据时为 unknown。AMOLED 显示“未知”，LCD-7 旧串口协议兼容映射为 IDLE。
4. `activeFlags` 存在 `waitingOnUserInput` 时覆盖为 `waiting`。

日志根目录为 `CODEX_HOME/sessions`，未设置环境变量时用用户目录 `.codex/sessions`。递归扫描 JSONL，以文件名末尾会话 ID 匹配：

| 条件 | 推导结果 |
| --- | --- |
| 最近 `task_started` 的 turn ID 没有终止事件，文件距今不超过 15 分钟更新 | RUNNING |
| 最近 turn ID 有 `task_complete` 或 `turn_aborted`，文件距今不超过 2 分钟更新 | COMPLETED |
| 已有终止事件且超过 2 分钟 | IDLE |
| 未结束但已过期、文件缺失或无法读取 | UNKNOWN |

旧日志回退的时间依据是**日志文件修改时间**而非事件时间。后续写入可能延长完成提示；长时间没写日志的活动任务会变为 UNKNOWN（旧串口映射 IDLE）；中止回合也显示 COMPLETED。该提示不证明任务成功，不保证桌面实时状态正确。

SQLite 属于 Codex 内部格式，升级可能改变结构；程序异常退出后 inProgress 也可能残留，不能当作进程存活证明。

FAILED 对应接口 `systemError` 或数据库近期 failed 回合，不从工具失败日志推导；等待输入的可见性取决于上游是否给出标志。`waitingOnApproval` 不参与显示或操作审批。

### Kimi Code 推导

Kimi Code 会话从 `KIMI_CODE_HOME`（默认 `~/.kimi-code`）读取：`session_index.jsonl` 提供会话索引，各会话 `state.json` 的 `updatedAt` 用于排序（单位为毫秒，Codex 日志时间为秒）；Web 端与 CLI 会话都会在此落盘，`archived: true` 的会话跳过。活动推导解析 `agents/*/wire.jsonl`，覆盖主代理与子代理；每个文件独立配对事件、按自身修改时间判断新鲜度：

| 条件 | 推导结果 |
| --- | --- |
| 存在未配对 `interaction.request`（无对应 `interaction.resolved`），文件距今不超过 15 分钟更新 | WAITING INPUT |
| 存在未配对 `turn.prompt`（按 promptId 配对，无对应 `prompt.completed` / `prompt.aborted`），文件距今不超过 15 分钟更新 | RUNNING |
| 最近回合已结束，文件距今不超过 2 分钟更新 | COMPLETED |
| 其余或文件缺失 | IDLE |

会话汇总优先级为 WAITING INPUT > RUNNING > COMPLETED > IDLE；主代理已完成但子代理仍等待或运行时，不显示完成。不同代理的同名 promptId / interaction id 不交叉配对；旧子代理也不会被其他代理的新日志重新激活。单个日志在扫描中消失或不可读取时跳过，不终止整条桥接。

元数据容错：索引不存在、不可读或无法按 UTF-8 解码时，采集返回不可用，不生成 Kimi ACTIVITY 卡，也不影响 Codex 采集。索引条目与 state.json 必须是对象；会话 ID、目录与标题经过类型校验，坏记录跳过，其他有效会话继续显示。若没有可显示会话且跳过了坏记录，也返回不可用；成功读取的空索引或仅含有效归档会话则返回空列表，仍表示采集成功。非法更新时间回退到日志修改时间。现有固件把来源不可用显示为 NOT CONNECTED，不能区分未安装与读取失败；这里没有新增错误页面。

仍是日志启发式：某代理超过 15 分钟不写日志，即使仍在运行也可能被判为 IDLE；完成窗口使用文件修改时间而非严格完成事件时间，中止也作为回合结束。跳过不可读日志可能少报状态，不等于确认代理空闲。Kimi 没有本地额度/套餐接口，因此没有 CODEX LEFT 类卡片。

## 计数与视觉口径

- Attention 包含 WAITING INPUT、COMPLETED、FAILED。
- All / Codex / Kimi / DSH 筛选同时作用于会话、顶部计数和 Information；顶部 `running` 只计 RUNNING，`completed` 单独计数，底部 Attention 显示当前来源的关注数量。
- Information 的 ACTIVITY 卡按来源各一张（Codex / Kimi 分别统计本来源）；它把 `active` 和 `waiting` 都算作 RUNNING，有等待输入时可能与顶部计数不同，这是当前已知口径差异。
- 完成项绿色，等待项黄色，失败项红色，运行项青色；完成标记还有勾选符号，不只靠颜色。
- 全局提醒条不受来源或页面影响，优先选取快照中第一个 COMPLETED（沿用上游更新排序），无完成时选首个需关注项；无数据或过期警告优先于旧任务提醒。不存储“已读”状态，不自动跳页，也不伪造完成时间。两分钟规则仍来自主机。
- Information 的 All 概览逐一显示 Codex / Kimi / DeepSeek Harness；只有快照中出现该来源任务或卡片时才标为 DATA RECEIVED，否则 NOT CONNECTED。选中来源后显示其卡片。该标记表示收到过当前快照数据，不是独立软件进程探活；新鲜度仍以顶部全局状态为准。
- 标题仅保留 ASCII 可打印部分并附 ID 前 8 位，无 ASCII 时显示 `Codex task`。详情是通用状态说明，不传聊天正文或真实回复摘要。

## 额度

读取 `account/rateLimits/read` 的 `result.rateLimits.primary`，套餐取 `account/read` 的 `account.planType`（`refreshToken=false`）。

- 剩余比例为 `round(100 - usedPercent)`，限制到 0–100。
- RESET IN 为 `resetsAt` 减电脑当前时间，最小为 0，按整天/整小时显示；不足一小时显示 `0h`，不表示已重置。
- `10080 min` 为窗口总长 7 天，与剩余重置时间不同。
- 仅显示 `primary`，不汇总 `secondary` 或多个 limit ID，不跨供应商合并额度。
- 缺字段或读取错误显示 UNKNOWN / `?`，不是 0%；套餐标识原样呈现，不映射营销名称。
- CODEX LEFT 与 RESET IN 对应同一所选窗口。电脑时间错误会影响倒计时和日志新鲜度。

## 串口发现与生命周期

`--watch` 未指定端口或使用 `--port auto` 时枚举 CH343 `VID:PID=1A86:55D3`；唯一匹配才连接，否则列出端口并退出。可用 `--port COMx` 手动指定。

枚举阶段不打开或试写其他端口，但型号匹配不是固件认证：唯一候选也可能是别的 CH343 设备。目前不保存 USB 序列号绑定。串口为 115200、8N1、无硬件流控，打开前设置 DTR/RTS=False。

没有自动重连、Windows 服务或开机自启；同一端口只运行一个桥接。轮询或串口错误可能使程序退出。

## 快照协议

ASCII 行协议，文本字段先编码 UTF-8 再转十六进制。以下 `<TAB>` 表示真实制表符：

```text
BEGIN
TASK<TAB>id_hex<TAB>title_hex<TAB>state<TAB>legacy_approval[<TAB>source]
CARD<TAB>title_hex<TAB>value_hex<TAB>detail_hex[<TAB>source]
END
```

TASK、CARD 可重复。BEGIN 清空待提交快照，END 发布；无效记录或超长行丢弃本次接收，保留旧快照，下一次 BEGIN 重新同步。没有协议版本、校验和、请求 ID 或可靠重传。

方括号表示可省略字段，不是字面字符。source 为未编码的 ASCII `codex`、`kimi` 或 `dsh`；未知来源拒绝接收。省略时默认 Codex，但当前 Python 桥接已始终显式发送来源字段。来源存在任务/卡片便可参与筛选；Kimi 采集器已接入，DSH 采集器仍未实现。

状态编码：`1=IDLE`、`2=RUNNING`、`3=WAITING INPUT`、`4=COMPLETED`、`5=FAILED`，其他值显示 UNKNOWN。

`legacy_approval` 为兼容字段，解析器接受 `-1/0/1`，当前主机固定发 `0`，不表示真实审批结论。UI 没有审批按钮。

设备回传 `REAL snapshot tasks=N cards=M` 日志。主机只在发送后短暂读取并查找该字符串，不等待带请求 ID 的 ACK；缺少单次 `panel confirmed` 不能独自证明失败，应同时看屏幕新鲜度。确认日志不证明状态推导或视觉布局正确。

### 容量

| 字段 | 主机最大有效 UTF-8 字节 / 固件数组大小（含 NUL） |
| --- | --- |
| TASK ID | 63 / 64 |
| TASK 标题 | 95 / 96 |
| CARD 标题 | 31 / 32 |
| CARD 值 | 47 / 48 |
| CARD 说明 | 95 / 96 |

最多 8 个 TASK、4 个 CARD，是工程数组限制，不是芯片限制。主机限制整个快照小于 4096 字节；固件 `BRIDGE_LINE_MAX=4096` 约束的是单行缓冲（有效最多 4095 字节），并没有整帧累计长度校验。扩大容量需同时检查桥接限制、固件数组、缓冲区、任务栈和 UI 性能。

## 数据过期与安全

有效快照重置 `age_seconds=0`，LVGL 每秒累加，超过 15 秒显示 STALE DATA；首次快照前显示 WAITING。旧数据不会因断线清空或自动变成完成。该计时受调度影响，不是精确墙上时钟。LINK 的 USB UART 只表示传输类型。

登录凭据不发往设备；传输内容为 ID、显示标题、状态、额度与说明。电脑端仍读取本地会话日志；直接 JSON 输出包含原始标题，分享前需脱敏。USB 协议没有鉴权/加密，只适用于可信本地连接，不能直接暴露到网络。

## 多软件扩展边界

延续电脑采集、统一快照、ESP32 显示。Kimi Code 采集器已接入（本机 `~/.kimi-code` 文件推导）；DeepSeek Harness 适配器仍未实现，接入前先核对实际版本和可用事件/日志。Kimi 的 `wire.jsonl` 事件格式为实测核对所得，不是官方承诺，Kimi Code 版本变化后需重新核对。

已增加来源字段和来源内 ID 区分、统一列表筛选、全局提醒条及按来源展示的信息页；颜色统一表示状态，来源用文字区分。全局容量仍是 8 个任务 / 4 张卡片，不是每个软件各有一份。尚未实现 DSH 适配器、各来源独立心跳、通知已读或中文字体；无接口的数据不能伪造。
