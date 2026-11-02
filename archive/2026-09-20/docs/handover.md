# AI Agent 交接文档（给新接手的 Agent）

> **接手时按顺序阅读：**
> 1. `PROJECT_OVERVIEW.md` —— 当前 Plan C 产品方案、架构与 P0 验收范围
> 2. `PROJECT_MEMORY.md` —— 已验证事实、架构决策与当前工作区迁移状态
> 3. `DEVELOPMENT_RULES.md` —— 分层、测试、烧录和“一功能一提交”规约
> 4. 本文件 `handover.md` —— 环境、工具链、编译、调试与 ai_agent 当前状态
> 5. `ai_agent_bringup.md` —— ai_agent 实机跑通过程、根因与修复清单
> 6. `esp32s3-eye-wifi-setup.md` —— Wi-Fi、wapi 命令和串口交互注意事项
> 7. `DEMO_RUNBOOK.md` —— 当前唯一可照抄的资源看板启动与验收步骤
> 8. `AI_AGENT_TRACK_CHECKLIST.md` —— 官方赛道硬要求与当前证据/缺口

> **方案口径：** P0 当前采用“ESP32-S3-EYE + HMAC API + VelaOps Proxy”的
> Plan C。板端直连 SSH 是已放弃的历史原型，不得从远端分支覆盖本地重构基线。

---

## 一、当前进展状态（2026-08）

### 已完成 ✅
- **ai_agent 已在 ESP32-S3-EYE 实机跑通**：WiFi 连接 → DNS → TLS → LLM 对话全部正常（连续 3 次 `llm=ok`）。
- 板级配置 `vendor/espressif/boards/esp32s3/esp32s3-eye/configs/openvela/defconfig` 已启用 ai_agent 并修好所有关键问题。
- 当前可用固件：工作区根目录 `nuttx/nuttx.bin`，大小 1,898,524 字节，SHA-256 为 `f8d0921e625068bc7361e003537334bbc8732a1c353497a795bcc9c8c9e97c45`（含屏显按键 read 死循环修复）。2026-08-31 已完成 16+ 组设备主机测试、目标构建、烧录写入校验、HTTP+HMAC、BOOT 批准修复闭环、**自然语言调用变更 Tool 的 LLM 执行闭环真机验收**，以及**屏显管理器与 DEBUG 弹窗联调闭环真机验收**（联调控制台一键触发 → 模型直调 `velaops_show_message` → LCD 弹窗 → BOOT 消除回看板）。

### 关键结论（详见 ai_agent_bringup.md）
- **必须关闭 BLE**：`CONFIG_ESP32S3_BLE` 会导致 `ESP32S3_WIFI_BT_COEXIST` 自动开启，WiFi 数据通路不稳定（丢包/断连）。已从 defconfig 移除全部蓝牙项。
- **必须关闭 SMP**：双核下 ai_agent 工具注册后崩溃（EXCCAUSE=001d）。
- **固定 DNS**：`CONFIG_NETDB_DNSSERVER_IPv4ADDR=0x72727272`（114.114.114.114）。
- **TCP 接收缓冲**：`CONFIG_NET_RECV_BUFSIZE=16384`（TLS 握手需大缓冲）。
- **普通 N16R8 开发板无法引导**（EFUSE quad flash，ROM 加载失败）——**硬件问题，与 vela 无关**，别在这块板上浪费时间。

### 已修 Bug：首次 LLM watchdog 误判
- 根因是系统时钟从 1970 跳到 2026，墙上时钟被误用于计算 LLM 调用耗时。
- 已改用 `CLOCK_MONOTONIC`，见 `packages/ai_agent` 本地提交 `09642b3`。
- 已完成主机边界测试、目标编译、烧录和重启后首次 MiMo 调用真机回归。

### Proxy P0 本机服务已完成

- 代码位于 `contest2026_023_0xS3/proxy/`，启动入口为 `python3 -m velaops_proxy --config <path>`。
- Demo 当前采用局域网 HTTP + HMAC v1；TLS 代码保留，但不作为比赛演示前置条件。
- 已实现 nonce 防重放、Action/资源白名单、无 shell 执行、批准、SQLite 幂等和 JSONL 审计。
- Action：`check_service`、`check_port`、`check_disk`、`check_memory`、`read_service_log`、`restart_service`。
- `make -C proxy check` 通过 93 项测试；systemd unit 和 sudoers 模板已分别通过 `systemd-analyze verify` 和 `visudo -cf`。
- 真实 HTTPS + HMAC + user-systemd 联调已验证重启 PID 变化、request ID 去重、错误签名和 nonce 重放拒绝。联调临时资产已清理。
- 队伍仓使用本地分支 `local-planc-rebuild`；不得从远程问题分支覆盖。

### 设备端 Proxy 通路已验收

- `app/hello_app/` 已实现 HMAC v1、Proxy API Client、严格响应包络解析、局域网 HTTP 传输和私密设备配置加载。
- `velaops auth-check` 已在 ESP32-S3-EYE 真机通过：局域网 NTP 校时 → 硬件 TRNG 生成 request ID/nonce → HTTP → HMAC 认证。
- `velaops check-memory` 已真机访问本机 Proxy 的白名单 `check_memory` Action，返回结构化内存快照；Proxy 生成 `action_started` 和 `action_succeeded` 审计。
- NTP 采用最多 3 轮、每轮 20 秒的有界 daemon 重启，规避首包 ARP 丢失和 daemon 退出后空等。未校时时安全失败。
- 主机回归为 4 组 C 测试，均以 `-Wall -Wextra -Werror -pedantic` 通过；目标固件已完整构建。
- 已新增通用 HealthSnapshot、内存阈值规则、严格内存结果 JSON 适配器和 Incident 去抖状态机；持续异常不重复开单，恢复也需连续健康快照。
- 当前主机回归为 7 组 C 测试，严格警告门禁和 ASan/UBSan 均通过；目标固件已完整构建。
- 已新增 `velaops monitor`：使用 `/dev/lcd0` 显示服务器内存/健康状态，BOOT 短按切换三页；`/dev/video0` 是摄像头节点，不能作为显示设备。
- 2026-08-26 已连续完成五轮真实资源刷新；Proxy 停止时看板保持运行并离线，恢复后下一周期自动恢复。
- 下一开发阶段进入 AI Agent 的“资源证据 → 结构化诊断建议”闭环。设备配置和 Wi-Fi 的断电持久化仍是独立待办，不得误记为完成。
- 队伍目录已实现 `server-incident-response` Skill 和只读
  `velaops_check_resources` Provider，不修改 `packages/ai_agent`。已多次真机完成自然
  请求 → 精确 Skill → 只读工具 → 单个严格 JSON；MiMo 公网仍有间歇超时，本地
  看板、Proxy 和规则降级路径不受影响。
- 每次硬复位后必须先运行 `docs/tools/provision_ai_agent_assets.sh`，再启动一次
  `ai_agent`；同一启动周期不要退出重进，否则上游注册表清理问题可能丢 Provider。
- MiMo 不可用时运行 `velaops diagnose-local`。它输出与 Skill 对齐但明确标记为
  “本地规则降级”的 JSON；已真机验证 Proxy 停止时安全输出 unknown，恢复后自动
  恢复真实 warning，不依赖重启板卡。
- `velaops monitor` 复用同一次 5 秒采样维护本地 Incident；连续两次异常开单、
  持续异常去重、连续两次正常恢复。Agent 专用适配器在首次成功工具调用后另启单个
  5 秒巡检线程，并将去抖后的 `opened`/`recovered` 安全注入 Agent 消息总线；真机
  已通过端口异常、持续异常去重和恢复回归。事件提示不携带 Proxy 证据，Agent 会
  重新读取 Skill 并调用固定只读工具取证。
- 开发机重启会清空 `/tmp/opencode/velaops-live/`。缺少临时配置时，在队伍目录运行
  `chmod 600 .velaops.local.env`，再运行
  `python3 docs/tools/prepare_live_demo.py --host 192.168.31.139`。脚本不会输出密钥，
  默认拒绝覆盖；显式 `--force` 轮换后必须重新下发板端配置。
- 新生成的配置使用独立 user-systemd 演示目标：运行
  `python3 docs/tools/manage_demo_target.py start` 启动回环端口 `28791`；用 `stop`
  制造故障。Proxy 仍监听 `28790`。真实 HMAC 批准重启已通过，但端口就绪可能稍晚于
  systemd active，板端处置后必须独立重试 `check_resources`，不能只信任 Action 返回。
- Agent Provider 已增加固定无参数 `velaops_restart_service`，同一设备实现可用
  `velaops repair-demo` 独立验收。30 秒内连续长按 BOOT 2 秒才会生成 60 秒批准；
  超时不执行，网络结果不确定不重试。真机已验证超时无变更、长按唯一重启和新
  request ID 独立复核恢复。
- **自然语言路由回归已完成（2026-08-31）**：两步法措辞（①`ask 检查demo服务状态`；
  ②`ask 我确认执行修复，请立即调用velaops_restart_service工具重启demo服务`）下模型真实
  调用变更 Tool，服务恢复且审计唯一；编排可用 `docs/tools/orchestrate_two_step.sh`，
  后台串口监听可用 `docs/tools/boot_watch.py`。联调时批准环节曾用 `/data/velaops/auto_approval`
  文件钩子自动化，验收后已从固件删除，交付固件是真实按键路径。
- 板端联调陷阱：`/data` 是 21K tmpfs，复位后必须 `mkdir -p /data/velaops` 再推配置，
  否则报 `io_error`；`quit` 退出 Agent 后同周期重进会丢 velaops Provider，异常一律硬复位重做；
  LLM 缓存会回放相同措辞的旧结果，复测换措辞；含中文命令必须 `send_slow.py` 逐字符发送；
  WiFi 有“新鲜窗口”现象（复位后前 1~3 次 LLM 调用健康，之后劣化），只能硬复位恢复。
- `packages/ai_agent` 本地分支已拆成 7 个功能提交（传输错误重试、vela_tls 读写 deadline、
  LLM 超时 180s/60s、WiFi 健壮性、mimo-v2.5 预置、两处构建修复），上游 PR 待大赛规则确认。
- **屏显与 DEBUG 弹窗（2026-08-31，纯队伍仓）**：`velaops_screen` 屏显线程（首次成功取证/弹窗时
  幂等拉起，5s 看板 + 弹窗闪烁 + BOOT 短按消除/翻页）；新工具 `velaops_show_message`（text ≤ 16
  ASCII，只屏显不触服务端）；主机联调控制台 `docs/tools/debug_event_gui.py`（127.0.0.1:8765，
  串口独占，烧录前先停）。教训：后台取证必须静默+共享缓存，否则刷屏压死控制台输入；
  磁盘事件阈值已调 90%（演示服务器基线 85.4%，85% 会永久触发主动会话挤占消息队列）。

---

## 二、硬件与环境

| 项 | 值 |
|----|----|
| 开发板 | **ESP32-S3-EYE**（比赛板，原生 USB 通常为 `/dev/ttyACM0`） |
| Wi-Fi | WPA2 网络；SSID 和密码通过本地私密配置提供，不写入仓库 |
| LLM | MiMo：host `api.xiaomimimo.com`，模型 `mimo-v2.5`，API Key 仅放本地私密配置，不写入仓库 |
| 工作区 | `/home/lu/桌面/openvela/` |
| 队伍仓库 | `/home/lu/桌面/openvela/contest2026_023_0xS3/`（作品代码 + 文档 + logs 提交这里） |

### 工具链路径
```bash
# 编译器
export PATH="$PWD/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH"
# esptool（烧录）+ ccache 禁用（避免报错）
export PATH="$PWD/.buildlog/esptool-venv/bin:$PATH"
export CCACHE_DISABLE=1
```

---

## 三、编译

### 首次编译先恢复 LCD 驱动补丁

ESP32-S3-EYE 的 ST7789 `RAMCTRL` 参数必须按 `00 F0` 两个有序字节发送，
否则 RGB565 颜色会异常。新工作区或 `repo sync` 后先执行一次：

```bash
bash contest2026_023_0xS3/docs/tools/apply_nuttx_patches.sh
```

脚本可重复执行，已应用时不会再次修改源码。

### 板级配置路径（重要，不要写错）
```bash
# 正确：vendor 自定义板全路径（不能用 esp32s3-eye:openvela 简写，会失败）
./build.sh vendor/espressif/boards/esp32s3/esp32s3-eye/configs/openvela
```

### 首次 / 改 defconfig 后（会触发 distclean + 重新 clone HAL，慢）
```bash
# 1. distclean
./build.sh vendor/espressif/boards/esp32s3/esp32s3-eye/configs/openvela distclean

# 2. 后台运行 fix 脚本（HAL clone 完成后打补丁；超时已改为 1200s）
bash packages/ai_agent/fix_esp32s3.sh &

# 3. 前台编译
./build.sh vendor/espressif/boards/esp32s3/esp32s3-eye/configs/openvela
```

> ⚠️ **HAL 大坑**：每次 `distclean` 会删除并重新 clone
> `nuttx/arch/xtensa/src/esp32s3/esp-hal-3rdparty`（commit `9fc713a9`，含子模块），
> 网络慢/易失败。若 clone 失败，手动重建：
> ```bash
> cd nuttx/arch/xtensa/src/esp32s3
> git clone --depth=1 https://github.com/espressif/esp-hal-3rdparty.git esp-hal-3rdparty
> git -C esp-hal-3rdparty fetch --depth=1 origin 9fc713a95b1ff150dd0b0647e465d3c624056bb1
> git -C esp-hal-3rdparty checkout --quiet 9fc713a95b1ff150dd0b0647e465d3c624056bb1
> git -C esp-hal-3rdparty submodule update --init --depth=1 \
>   components/mbedtls/mbedtls components/esp_phy/lib components/esp_wifi/lib \
>   components/bt/controller/lib_esp32c3_family components/esp_coex/lib
> git -C esp-hal-3rdparty/components/mbedtls/mbedtls reset --quiet --hard
> cd esp-hal-3rdparty/components/mbedtls/mbedtls && git apply ../../../nuttx/patches/components/mbedtls/mbedtls/*.patch
> # 然后重新执行 fix_esp32s3.sh 打 4 处补丁
> ```

### 只改 ai_agent 源码（defconfig 未变）→ 增量编译，很快
```bash
./build.sh vendor/espressif/boards/esp32s3/esp32s3-eye/configs/openvela
```
> 前提：`nuttx/defconfig` 与 vendor defconfig 一致（configure.sh 检测到不一致会 distclean）。

---

## 四、烧录

```bash
# eye 板原生 USB（默认 /dev/ttyACM0，按 ls /dev/ttyACM* 确认）
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 460800 \
  --before default-reset --after hard-reset write-flash 0x0 nuttx/nuttx.bin

# 烧完复位运行
esptool.py --chip esp32s3 --port /dev/ttyACM0 --before default-reset --after hard-reset run
```

---

## 五、串口交互（NSH + ai_agent）

> **注意**：`/dev/ttyACM0` 是 eye 板原生 USB（USB-Serial/JTAG）。**用 raw tty 方式交互，不要碰 DTR/RTS**（会复位板子）。
> 串口工具脚本在本仓库 `docs/tools/` 下：
> - `send_raw.py` —— 整串发送（仅短命令）
> - `send_slow.py` —— 逐字符发送（**发长 Key 必须用**，如 `router_set`）
> - `serial_push.py` —— 按 NSH 79 字符命令上限分块推送文件
> - `serial_read.py` —— 只读（抓启动日志/panic）
> - 两个发送脚本会默认识别凭据类命令，同时脱敏本地提示和设备回显；禁止为调试重新打印明文凭据。
>
> 用法示例（可设 `PORT`/`DRAIN` 环境变量）：
> ```bash
> python3 docs/tools/send_raw.py 'wapi sense wlan0'
> python3 docs/tools/send_slow.py router_set mimo <api_key>   # 长命令逐字符
> python3 docs/tools/serial_read.py 15                         # 读 15 秒
> ```
> 若需以非 root 访问串口：`sg dialout -c "python3 docs/tools/send_raw.py ..."`

### WiFi 连接（NSH 下）
```bash
wapi mode wlan0 2
wapi psk wlan0 <wifi_password> 3 2 # 末尾 2 = WPA2（缺这个连不上）
wapi essid wlan0 <wifi_ssid> 1
renew wlan0
ifconfig wlan0                    # 应看到路由器分配的 IPv4 地址
```

### ai_agent 对话
```bash
ai_agent                           # 进入 vela> CLI
router_set mimo <api_key>          # 长 Key，逐字符发送！
ask 你好，介绍一下你自己
```

### 常用 vela 命令
- `net_test` 测 HTTPS/TLS、`net_status` 看网络
- `router_status` 看 LLM 后端配置、`router_clear` 清空
- `config_show` 看配置（Key 脱敏）

### VelaOps 真机命令与私密文件

```text
/data/velaops/config.json   # schema_version/host/port/device_id/secret
```

```bash
velaops auth-check
velaops check-memory
```

`config.json`、Wi-Fi 密码和设备密钥都是本地私密资产，不得提交。当前手工真机联调将 `/data` 挂为 tmpfs，重启或烧录后需重新下发；持久化配置是后续独立任务。当前完整步骤以 `DEMO_RUNBOOK.md` 为准。

---

## 六、目录速查

- `packages/ai_agent/` —— **ai_agent 源码**（作品核心，未来开发主战场）
  - `src/infra/network_manager.c`（WiFi 修复）、`src/infra/vela_tls.c`（TLS）、`src/channels/cmd_llm.c`（模型配置）、`src/agent_main.c`（主入口）
- `vendor/espressif/boards/esp32s3/esp32s3-eye/configs/openvela/defconfig` —— 板级配置（关键修复所在地）
- `packages/ai_agent/fix_esp32s3.sh` —— 构建期补丁脚本（4 处，不可提交）
- `contest2026_023_0xS3/` —— 队伍仓库：`app/`、`board/`、`quickapp/`（作品骨架）、`docs/`（文档）、`docs/tools/`（串口调试脚本）、`logs/`（AI Coding 日志）
  - `proxy/` —— 已验收的 P0 Linux Proxy，部署说明见 `proxy/README.md`

---

## 七、给新 Agent 的提醒

1. **遇到"网络差/TLS 失败/丢包"先别怀疑环境** —— 很可能是 BLE 或配置问题，对照 `ai_agent_bringup.md` 的根因表排查。
2. **改 defconfig = 重新 clone HAL（慢）**，尽量一次性改对，少改 defconfig。
3. **改 ai_agent 源码前先 `git diff` 看当前改动**，别覆盖已有修复。
4. 正式开发时，作品代码放 `contest2026_023_0xS3/`（会软链进编译树），公共仓（packages/nuttx/vendor）的改动按大赛规则走 fork+PR。
5. 演示用 eye 板即可；普通 N16R8 板引导不了，别在它上面排查。
6. 设备端 NTP 依赖 `CONFIG_NETUTILS_NTPCLIENT` 及完整的 server/port/stack/priority/signal/timeout 参数；该板必须使用 `GRND_RANDOM` 访问已启用的硬件 `/dev/random`。
