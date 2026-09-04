# 运行、开发与维护

核对日期：2026-09-03。快速入口见 [README](../README.md)，数据语义见[架构说明](architecture.md)。所有相对命令均注明工作目录；COM3 只是本机当前示例。

## 1. 环境与连接

当前开发机：Windows / PowerShell，项目在 `C:\Users\ZHUOZhuang\Documents\lvgl-dev`。

| 项目 | 本机配置 |
| --- | --- |
| ESP-IDF | `C:\esp\v6.1\esp-idf` |
| EIM 注册表 | `C:\Espressif\tools\eim_idf.json` |
| Python | `C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe` |
| 屏幕 | ESP32-S3-Touch-LCD-7，800×480，GT911；非 7B/7C |
| 数据接口 | 板载 USB 转串口，选择 UART1；115200 8N1 |
| 自动枚举 | CH343，VID:PID `1A86:55D3` |

设备固件已烧录时，日常显示只需 Python、pyserial、本机可运行且已登录的 Codex，不需要打开 VS Code 或重新编译。Kimi Code 为可选数据源：无需额外配置，桥接自动读取本机 `~/.kimi-code` 的会话文件（Web 与 CLI 会话都会落盘）；不需要时可用 `--no-kimi` 跳过。

换电脑时不要直接复制上述绝对路径。先找实际 Python 和 Codex，再用 `--codex` 指定。pyserial 在本机 ESP-IDF 环境中已安装；其他 Python 可检查 `python -m pip show pyserial`，缺少时才安装 `python -m pip install pyserial`。

桥接查找 Codex 的顺序为 `--codex` → PATH 中的 `codex` → `%LOCALAPPDATA%\OpenAI\Codex\bin\*\codex.exe` 中最近修改的候选。

## 2. 启动、停止和电脑重启

普通 PowerShell：

```powershell
cd C:\Users\ZHUOZhuang\Documents\lvgl-dev
& 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe' .\tools\codex_status_probe.py --watch
```

启动前关闭占用设备串口的 SSCOM、其他调试助手、`idf.py monitor` 和旧桥接窗口。同一端口不能由多个桥接同时打开。`Ctrl+C` 停止；退出时可能显示 KeyboardInterrupt，不代表固件故障。

重启电脑、拔插设备或程序退出后重新运行。当前只在启动时选端口，不支持运行中重连；没有开机启动项。不要在旧桥接仍运行时反复启动新的桥接。

### 参数

以下命令在正确 Python 环境、项目根目录执行：

```powershell
python tools/codex_status_probe.py --help
python tools/codex_status_probe.py --watch --interval 10 --limit 8
python tools/codex_status_probe.py --port COM3 --watch
python tools/codex_status_probe.py --port auto
python tools/codex_status_probe.py --codex 'C:\实际安装目录\codex.exe' --watch
```

- `--watch`：持续同步，省略 `--port` 时自动枚举。
- `--port auto`：自动选择；不带 `--watch` 时只推送一次。
- `--port COMx`：显式指定，跳过自动枚举，不保证该设备就是本屏幕。
- `--limit`：默认 8；串口模式限制 1–8，仅 JSON 模式限制 1–100；限制两来源合并后的总数，不是每来源各一份。
- `--interval`：默认 10 秒，最小 2 秒，为每轮采集与发送之后的等待时间。
- `--no-kimi`：跳过 Kimi Code 采集，仅同步 Codex 数据。
- 无 `--watch` 且无 `--port`：只打印 JSON `[tasks, usage]`，不打开串口；标题可能含私人信息。

## 3. 串口与状态排障

先枚举串口，不向设备写入：

```powershell
& 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe' -m serial.tools.list_ports -v
```

| 现象 | 检查顺序 |
| --- | --- |
| 重启电脑后画面不更新 | 启动桥接；设备已保存的固件不会自动启动电脑上的 Python |
| `No matching CH343 adapter found` | 检查数据线、UART 接口、UART1 开关与设备管理器；原生 USB 不匹配此规则 |
| `Multiple CH343 adapters found` | 根据枚举和实际连线确定设备，再用 `--port COMx`；程序不会随意选一个 |
| COM 号变化 | 重启桥接，使用 `--watch` 自动枚举，不固定旧 COM 号 |
| `PermissionError(13)` / 拒绝访问 / 无法打开端口 | 先关闭旧桥接、调试助手、烧录监视器；再检查端口是否仍存在，不要先以管理员身份掩盖占用问题 |
| `ModuleNotFoundError: serial` | 使用 ESP-IDF Python，或给当前 Python 安装 pyserial |
| `codex executable not found` | 检查 Codex 安装和 PATH，或传入 `--codex` 的实际路径 |
| App Server 超时 / `thread/list failed` | 先运行不带串口参数的 JSON 模式，核查登录和 Codex 版本；串口可能完全正常 |
| 屏幕 WAITING | 尚未收到首个有效快照；检查桥接输出、线缆和 UART1 选择 |
| 屏幕 STALE DATA | 超过约 15 秒未收到快照；检查桥接是否退出、电脑休眠或采集变慢 |
| CODEX LEFT / RESET IN 为 UNKNOWN | 上游额度缺字段或接口失败；SYNCED 仅代表串口收到数据，不保证额度可用 |
| `0h` | 不到一小时会被向下取整为 0h，也可能已到期；不能只据这个值判断已重置 |
| 会话只剩英文片段 | 固件尚无完整中文字库，目前是 ASCII 标题加短 ID |
| 明明结束却显示 IDLE | 完成提示通常仅约两分钟；日志来源与文件修改时间也影响判断 |
| Kimi 显示 NOT CONNECTED | 未找到索引、使用了 `--no-kimi`，或索引读取/UTF-8 解码失败、坏元数据导致无可显示会话；检查 `KIMI_CODE_HOME`、文件权限和 JSON 格式。成功读取空索引不等于读取失败 |

不要对未知串口自动发送测试字符串或烧录命令。看到 `panel confirmed` 只说明固件接收快照，不能证明当前状态推导或触控已验收。

## 4. 构建固件

普通 PowerShell，从项目根目录运行：

```powershell
.\tools\build.ps1
```

脚本从 EIM 注册表查找恰好一个名称为 `v6.1`、状态为 `finished` 的安装，加载其激活脚本，用该安装的 Python 构建并输出 ASCII CSV 内存报告。自定义 EIM 注册表：

```powershell
.\tools\build.ps1 -IdfRegistry 'D:\Espressif\tools\eim_idf.json'
```

找不到安装时用 EIM 完成配置，或在已经激活的 ESP-IDF PowerShell 中执行 `idf.py -C firmware build`。不要依赖普通终端直接运行 `export.ps1` 一定成功；本机此前遇到过未正确导出环境的问题。

`firmware/sdkconfig.defaults` 指定目标和默认配置；生成的 `sdkconfig` 可能保留旧设置，更新 defaults 后应核查最终配置，不能假定自动覆盖。直接依赖约束在 `main/idf_component.yml`，精确解析结果在 `dependencies.lock`。

目前保守配置使用 8 MB Flash、3 MB 单应用分区、8 MB OPI PSRAM；本机实测 Flash 为 16 MB，但没有启用完整容量或 OTA。静态内存报告不包括所有动态帧缓冲和运行时峰值。

## 5. 烧录与恢复显示

先停止桥接并关闭所有串口软件。在 EIM 提供的 ESP-IDF PowerShell 中：

```powershell
cd C:\Users\ZHUOZhuang\Documents\lvgl-dev\firmware
idf.py -p COM3 flash monitor
```

烧录覆盖相应区域，不需要全片擦除或修改 eFuse。典型地址是 bootloader `0x0`、partition-table `0x8000`、应用 `0x10000`；以本次构建生成的参数为准，不混用不同构建的镜像。

如果固件已构建而 IDF 环境启动失败，可从同一构建目录使用 esptool 和生成的参数文件（先确认文件存在，COM3 换成实际端口）：

```powershell
cd C:\Users\ZHUOZhuang\Documents\lvgl-dev\firmware\build
& 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe' -m esptool --chip esp32s3 -p COM3 -b 460800 --before default-reset --after hard-reset write-flash '@flash_args'
```

不要将单独的 `desk_panel.bin` 当合并固件写到 `0x0`。完成后 `Ctrl+]` 退出 monitor，再运行桥接。只有修改固件才需构建/烧录；修改 Python、README 不需烧录。

## 6. 验证

不占用设备的桥接自检：

```powershell
cd C:\Users\ZHUOZhuang\Documents\lvgl-dev
& 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe' .\tools\codex_status_probe.py --self-test
```

`tests/test_model.c` 的旧 Attention 断言已修正，并增加来源解析、筛选、同名 ID、完成提醒与来源可用性测试。本机尚未运行独立主机 C 测试；固件启动自检已在设备验证关键来源/解析逻辑。详见[验证记录](validation.md)。安装可运行 Windows 程序的主机 C 编译器与 CMake 后，可执行：

```powershell
cmake -S tests -B build-host
cmake --build build-host --config Debug
ctest --test-dir build-host -C Debug --output-on-failure
```

使用 Debug 保留 assert；ESP32 交叉编译器生成的程序不能直接在 Windows 执行。编译通过、串口确认、屏幕目视和长期稳定性是不同层次的验证。

## 7. 本地 Git

实际项目目录是唯一维护基准；此前 `ChatGPT\LVGL develop\lvgl-dev-staging` 和重复 docs 已移到 `C:\Users\ZHUOZhuang\Documents\lvgl-dev-cleanup-20260903` 归档，不再维护。当前直接在实际项目构建，不混用归档产物。

```powershell
cd C:\Users\ZHUOZhuang\Documents\lvgl-dev
git status
git diff
git log --oneline -5
```

确认修改后再提交，示例（按实际改动选择文件）：

```powershell
git add README.md docs tools/codex_status_probe.py
git diff --cached --stat
git commit -m 'Document setup and serial auto-detection'
```

首次基线为 `ae4385d`。远程仓库为 [EESheep/agent-desk](https://github.com/EESheep/agent-desk)，SSH 地址为 `git@github.com:EESheep/agent-desk.git`。没有自动提交或自动推送；提交后使用 `git push` 同步远程。不要提交账户密钥、原始会话日志或带私密内容的 JSON 输出；`.gitignore` 只是第一道过滤，不替代提交前检查。构建产物和迁移备份仍保留在磁盘，只是不纳入 Git。
