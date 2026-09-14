# 2.41 V2 小屏与局域网实现

## 交互

- 横屏 600×450，底部两个主入口：会话、额度。
- 顶栏高 64 px，返回与服务商按钮距顶边 12 px；额度入口显示“服务商 · 切换”，使用亮色描边、填充背景及按下反馈。
- 每页两条会话，整卡可点；标题 28 px，最多两行，超出省略。详情完整换行并纵向滚动，返回保留页码。
- 按运行中、等待输入、失败、回合结束、空闲排序，同状态按更新时间倒序，不区分项目。设备每次采用服务端最新顺序；手指按住屏幕时暂不提交新数据，避免点到替换后的对象。详情仍按来源与会话 ID 跟踪，不随列表重排切换目标。会话被上游移除时，详情提示不可用。
- 会话按来源标签合并。额度独立选择 Codex、Kimi、DSH，选择在运行期保留；每个服务商的额度窗口单独翻页，显示名称、周期、剩余比例及重置时间。
- 当前只接入 Codex 额度；Kimi/DSH 显示“额度暂未接入”，不伪造零余额。
- 按用户当前使用习惯，AMOLED 服务仅下发通用 Codex 的周额度（10080 分钟），不展示 GPT-5.3-Codex-Spark 的两个额度窗口；旧 LCD-7 的采集与显示策略不变。筛选在 `tools/amoled_server.py`，调整后重启电脑端服务即可，无需烧录。
- 本版每次最多八条会话；标题网络上限为 4096 UTF-8 字节，超限拒绝本次快照，不静默截断。中文基本汉字及常用标点可显示，字体不覆盖 emoji 和扩展汉字区。

## 数据与代码分隔

Codex 实时状态优先只读查询 `thread_history_1.sqlite` 的最新回合，按 `rollout_ordinal` 排序：运行中映射 active，近期完成映射 completed，近期失败映射 systemError，已结束超过两分钟映射 idle。数据库缺失、结构不兼容或没有该会话时兼容旧 JSONL；过期且未结束的日志显示 unknown，不再冒充空闲。数据库属于 Codex 内部格式，升级后需要核对兼容性；持久化 inProgress 在程序异常退出时仍可能残留，不能等同于进程存活检测。LCD-7 复用正常状态修复，但旧串口协议的未知状态仍兼容映射为空闲。

桌面恢复会话后，公开会话 ID 与当前回合记录 ID 可能不同。采集器只读 `state_5.sqlite` 中的当前 `rollout_path`，识别文件名追加的历史 UUID，再查询对应回合；旧 ID 的结束记录不能覆盖当前记录。列表按来源与公开 ID 去重。“已同步”表示电脑快照未过期，不是对上游会话状态准确性的独立验证。

共用 `tools/codex_status_probe.py` 的 `collect()`，保持 LCD-7 的串口接口和既有字段。额度归一化新增原始 `buckets`，供网络模式读取各额度窗口；旧串口仍取原有单窗口。

新增 `tools/amoled_server.py` 使用 Python 标准库，定期采集，提供唯一只读接口 `GET /snapshot`。数据包括版本、会话、按来源的额度窗口、成功采集 revision、age_seconds 和 server_time。需要 Bearer 访问令牌，不返回原始日志或服务商凭据。

新增 `firmware-amoled-2.41-v2/` 独立构建；驱动、触摸坐标、亮度、Wi-Fi、网络接收和布局均在该目录。未复制旧设备的固定卡片模型或 ASCII 串口编码。实际共用部分先复用现有入口，未创建空泛的 shared 框架。

设备正常情况下每两秒取快照，网络请求放在独立 FreeRTOS 任务，UI 更新持 LVGL 锁。服务器默认每轮采集后等待十秒。服务端用单调时钟计算数据年龄；HTTP 成功不会把旧缓存变成新数据。超过三十秒显示过期。Wi-Fi、电脑可达性和数据新鲜度分别处理；断线自动重连。右上角只显示状态：正常为“已同步”，短暂失败为“同步中”，连续三次失败为“离线”，成功后清零恢复；旧快照超过三十秒仍显示“数据已过期”。单次 HTTP 操作超时为 1500 ms，慢请求可能使实际轮询周期延长。

普通 HTTP 仅面向可信局域网，不提供传输加密；服务绑定指定局域网地址，访问令牌限定读取。服务商登录凭据保留在电脑。电脑休眠或服务停止后设备数据会过期。

## 配置与操作

本机配置在被 Git 忽略的 `local/amoled-config.json`，含 ssid、password、host、port、url、token。不要提交或分享此文件。通过原生 USB 写入独立 NVS 命名空间，不把 Wi-Fi 密码编进固件。重新配置无需重编译。

以下命令从实际项目根目录执行，`python` 使用装有 pyserial 且能访问本人 Codex 数据的 Python 环境：

```powershell
# 本机已安装 IDF 6.1；与 LCD-7 各自构建
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build-amoled.ps1
# 在 ESP-IDF 环境内，将 COM4 替换为已核对的设备端口
idf.py -C firmware-amoled-2.41-v2 -p COM4 flash
python tools/configure-amoled.py --port COM4
# 保持电脑端服务运行；同一端口只启动一份
python tools/amoled_server.py
# 离线测试
python tests/test_amoled_server.py
python tools/codex_status_probe.py --self-test
```

防火墙需允许目标设备访问该电脑的 TCP 8765。建议路由器给电脑保留地址；更换地址后更新 url 并再次通过 USB 配置。固件只接受 10/8、172.16/12、192.168/16 地址的 HTTP snapshot URL，避免误填 VPN 虚拟接口。

USB 调试支持 `IDENTIFY`、`PAGE 0..3`、`SCREEN`，页面依次为会话、额度、当前页首条详情、Kimi 额度；不修改上游会话。可运行 `python tools/capture-amoled.py --page 0 --output local/sessions.png` 读取 LVGL 渲染缓冲；这能检查布局，不等于物理屏幕和触摸实测。

## 验证记录（2026-09-10）

- 实物：COM4，ESP32-S3 rev 0.2，8 MB PSRAM，16 MB Flash，USB Serial/JTAG。SSID 使用用户指定的 <本机 Wi-Fi SSID>。
- 工具链：IDF 6.1、LVGL 9.5.0、esp_lvgl_adapter 0.5.2；精确组件见独立 dependencies.lock。最终镜像 5,502,480 字节，构建成功，烧录后的数据哈希校验通过。
- 烧录前完整备份 16,777,216 字节，位于 `local/amoled-v2-original.bin`；SHA256 为 `6BA92826D78A68D2630E71F9E3CD671E71260B06137B935C8C1A28FDE5B7B365`。快速读取在固定区段失败，改用 ROM 读取该块后完成备份。
- 设备日志持续出现 SNAPSHOT_OK，已收到真实会话与三个 Codex 额度窗口；本机 HTTP 鉴权接口返回 200，服务在后台运行。
- 已修复 Wi-Fi 开启后 SPI DMA 临时缓冲不足：刷新块从 100 行缩至 20 行；修复后监视日志未再出现该错误。
- 已修复中文字体不显示：使用未压缩的字库，与当前 LVGL 配置匹配。会话、详情、Codex 额度、Kimi 暂未接入四页的实际 LVGL 渲染截图已检查，图片位于 `local/amoled-{sessions,detail,quota,kimi}.png`。
- `tests/test_amoled_server.py` 通过，覆盖长 Unicode 标题、关注优先顺序、独立额度窗口、未接入额度、缓存新鲜度、报文上限和 HTTP 鉴权；原 `codex_status_probe.py --self-test` 通过，保留 LCD-7 串口格式。
- 实机服务中断测试通过：停止本次新建的 HTTP 服务，设备保留旧会话，显示“电脑不可达”与“上次”状态；`local/amoled-offline.png` 记录数据年龄 81 秒。恢复服务后，无需重启设备便重新收到连续 SNAPSHOT_OK；恢复监视中无 DMA 错误，可用堆约 2.99 MB。
- 本次后台服务 PID 记录在 `local/server.pid`，日志在 `local/server.log`；未安装开机自启。当前主机地址为 <电脑局域网 IP>:8765，密码及令牌不写入文档。设备已恢复正常会话页。
- 渲染截图不等于实物光学或触摸测试。实物显示方向、触摸命中和阅读字号仍需用户操作确认；未声称这些已自动验收。
- 用户实测后反馈顶边裁切及服务商按钮不明显，已调整顶栏安全边距与按钮样式；重新编译、COM4 烧录及哈希校验通过，镜像 5,502,704 字节。额度、会话及详情的更新截图保存在 `local/amoled-{quota,sessions,detail}-header.png`；检查顶部轮廓完整，设备恢复联网同步，停留在额度页供实测。
- 周额度筛选测试通过，覆盖通用额度、Spark 双窗口排除及无周额度的情况；重启服务后，实际 HTTP 接口仅返回通用 Codex 周额度。设备截图 `local/amoled-weekly-only.png` 用于核对显示。
- 修复始终空闲：当前桌面应用实时回合在 SQLite 中更新，旧 JSONL 未及时更新。新增 `tests/test_codex_activity.py` 验证最新回合优先、完成/失败、未知及旧日志回退；原有两组测试通过。重启服务后实际接口返回 active，设备截图 `local/amoled-status-fixed.png` 确认“运行中”，无需烧录。

## 2026-09-14 排序修复验证

- 使用本机 `C:\esp\v6.1\esp-idf`（6.1.0）及项目锁定的 18 个外部组件构建成功；依赖锁文件未变。镜像为 5,501,952 字节。
- 烧录前完整 Flash 备份：`local/amoled-before-sort-20260914.bin`，16,777,216 字节，SHA256 `342A9441D0E841CD5F46854ED2E0DBA29EC9D3597D9618CE18C6AF9D233C9C37`。
- COM4 烧录及写入哈希校验通过，保留 NVS 配置；重启后连续收到 `SNAPSHOT_OK`。电脑端服务已重启加载排序规则。
- `tests/test_amoled_server.py` 验证运行中优先、同状态更新时间倒序、另一项目转为运行中后排到最前；状态回归测试和原串口自检均通过。
- 设备渲染截图 `local/amoled-sorted-20260914.png` 的前两条与真实 HTTP 快照顺序一致，首条显示本次运行中的会话。自动验证未新建其他项目任务来触发真实跨项目状态切换；该切换由回归测试覆盖。


## 2026-09-14 两秒同步与离线状态验证

- 修改轮询周期为 2000 ms，右上角不再显示快照秒数；连续三次同步失败显示“离线”，成功清零。触摸期间有效快照的暂缓提交不计为网络失败。
- 固件 5,502,336 字节，构建成功，COM4 应用分区烧录及哈希校验通过，保留 Wi-Fi 配置。
- 实机正常同步日志间隔约 2 秒。停止已核对的电脑服务后，连续记录 `SYNC_FAILED count=1/2/3`；截图 `local/amoled-offline-3-failures.png` 确认右上角“离线”。
- 恢复服务后连续 `SNAPSHOT_OK`，截图 `local/amoled-sync-recovered.png` 确认“已同步”且不显示秒数。测试结束已恢复正常服务。

## 会话状态配色

列表和详情页统一使用：运行中青蓝色（`#81D7EE`）、空闲灰色（`#9AA7B2`）、等待输入黄色（`#FFD478`）、回合结束绿色（`#9FDDA9`）、失败红色（`#FF9999`）、未知紫色（`#C6A8EF`）。保留状态文字，避免仅凭颜色识别。
