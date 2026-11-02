# VelaOps 局域网资源看板演示手册

> 验证日期：2026-08-31。本文是当前 Demo 的唯一启动口径。
>
> 传输说明：ESP32-S3 的 WiFi 在 NuttX 下不稳定，比赛演示默认走 **USB 串口隧道**
> （有线），设备侧业务 HTTP 全部经开发机 relay 转发。设计与开关见
> `docs/SERIAL_TRANSPORT.md`；`TRANSPORT=wifi` 可一键切回 WiFi 直连。

## 1. 当前闭环

ESP32-S3-EYE 通过 2.4 GHz Wi-Fi 访问开发机上的 HTTP Proxy，每 5 秒读取
`check_resources`，并在 ST7789 LCD 显示内存、磁盘、服务和端口状态。请求仍使用
HMAC v1、随机 nonce 和 request ID；Demo 阶段不启用 TLS。

AI Agent 演示在同一条只读通路上工作：自定义 Markdown Skill 约束推理顺序，
`velaops_check_resources` Tool Provider 取得白名单资源证据，MiMo 输出结构化诊断。
LLM 不能传入主机、Action 或 shell 参数，也不能直接执行变更。

首次成功的工具取证会在 agent 进程内拉起屏显管理器：LCD 每 5 秒刷新资源看板，
`velaops_show_message` 工具可弹出闪烁提示框（琥珀/红边框交替），短按 BOOT 消除
弹窗回到看板，看板态短按 BOOT 翻页。后台取证共享 4s 缓存且完全静默，不占串口。

当前开发机地址为 `192.168.71.90`，Proxy 端口为 `28790`，局域网 NTP 端口为
`40123`。若开发机 IP 改变，必须同步修改 Proxy/设备临时配置和板级 NTP 地址。

## 2. 开发机启动

演示栈（Proxy / 局域网 NTP / MiMo 转发器 / 演示目标）以 **user-systemd** 服务常驻，
并已开启 linger，开发机重启或注销后会自动拉起，密钥目录也改用持久化的
`$HOME/.local/state/velaops-live/`（不再依赖会被清空的 `/tmp`）。

若配置不存在，先恢复本地私密环境文件权限，再成对生成 Proxy/设备配置（脚本不输出
密钥）：

```bash
chmod 600 .velaops.local.env
python3 docs/tools/prepare_live_demo.py --host 192.168.71.90
```

脚本默认拒绝覆盖已有配置，避免设备与 Proxy 的 HMAC 密钥被意外轮换。只有明确需要
重新配对时才使用 `--force`；轮换后必须重新执行第 3 节的设备配置下发。

随后确认配置存在、权限为 `0600`，并确认四个服务在线：

```bash
test -f $HOME/.local/state/velaops-live/proxy.json
test -f $HOME/.local/state/velaops-live/device-config.json
stat -c '%a %n' $HOME/.local/state/velaops-live/{proxy.json,device-config.json}

systemctl --user enable --now \
  velaops-proxy.service velaops-lan-ntp.service \
  velaops-llm-forwarder.service velaops-demo-target.service
systemctl --user is-active \
  velaops-proxy velaops-lan-ntp velaops-llm-forwarder velaops-demo-target
```

演示目标只监听开发机回环地址 `28791`，与监听 `28790` 的 Proxy 相互独立，因此
后续可以安全演示“目标故障 → Proxy 仍在线”。需要制造故障时停止目标服务，不要
停止 Proxy：

```bash
systemctl --user stop velaops-demo-target.service   # 制造故障
systemctl --user start velaops-demo-target.service  # 恢复
curl --fail http://127.0.0.1:28791/
```

若 unit 由旧脚本手工创建、路径仍指向 `/tmp`，用 `systemctl --user cat <unit>` 核对
`ExecStart` 后改为 `$HOME/.local/state/velaops-live/proxy.json`；不要手工改一边配置
造成 HMAC 密钥不一致。

不使用 systemd 时也可在前台手动启动（终端 A 局域网 NTP，终端 B Proxy）：

```bash
cd /home/lu/桌面/openvela/contest2026_023_0xS3
PYTHONPATH=proxy/src python3 -m velaops_proxy.lan_ntp --port 40123

PYTHONPATH=proxy/src python3 -m velaops_proxy \
  --config $HOME/.local/state/velaops-live/proxy.json
```

另一个终端验证服务：

```bash
curl --fail http://192.168.71.90:28790/healthz
curl --fail http://127.0.0.1:28791/
ss -lun '( sport = :40123 )'
```

Proxy 配置监听局域网时必须显式包含：

```json
"allow_insecure_http": true
```

该开关只用于可信局域网 Demo；Guarded 生产模式仍应配置 TLS。

需要制造可恢复故障时，只停止独立目标，不要停止 Proxy：

```bash
systemctl --user stop velaops-demo-target.service
```

重新准备正常演示环境时再启动即可；该单元不需要 root 或 sudo。

## 3. 板端启动

烧录或断电后，按以下顺序执行。Wi-Fi 密码用逐字符脚本发送，禁止写入文档：

```text
wapi mode wlan0 2
wapi psk wlan0 <wifi_password> 3 2
wapi essid wlan0 TP-GM2.4 1
renew wlan0
ifconfig wlan0
```

下发唯一需要的设备文件并认证。`/data` 是易失 tmpfs，烧录或断电后必须先重建目录，
否则配置写入静默失败并报 `io_error`：

```bash
python3 docs/tools/send_raw.py 'mkdir /data/velaops'
python3 docs/tools/serial_push.py \
  $HOME/.local/state/velaops-live/device-config.json \
  /data/velaops/config.json
python3 docs/tools/send_raw.py 'velaops auth-check'
```

若局域网 NTP 在演示前未能及时校时，用开发机当前 Unix 时间完成一次性注入，
HMAC、时间窗和 nonce 防重放仍保持启用：

```bash
epoch_now=$(date +%s)
python3 docs/tools/send_raw.py "velaops set-time $epoch_now"
```

认证成功后启动看板：

```bash
python3 docs/tools/send_raw.py 'velaops monitor'
```

BOOT 短按依次切换三页：系统总览（健康、内存负载、磁盘和端口）、代理服务
（服务、端口和延迟）、内存详情（总量、已用和可用）。顶部圆点表示在线状态和
当前页，所有数据每 5 秒刷新。Proxy 暂时断开时看板标记 `OFFLINE`，服务恢复后
下一刷新周期自动恢复，无需重启板子。

资源看板同时执行本地事件去抖。连续两次 warning/critical（当前为 10 秒）后，
串口只输出一次 `proactive_event type=opened`；持续异常不会每 5 秒重复。连续两次
normal 后输出 `type=recovered`。这是看板自身的状态提示，不会直接注入 Agent；
Agent 主动巡检由下一节独立的生命周期适配器负责。

### 3.1 开机自启与连接状态页（串口/无线）

烧录后上电即自动运行（板级 bringup 会拉起 `velaops autoconfig`），无需手动敲
命令。上电后 LCD 依次显示：

1. `VELAOPS / BOOTING` 开机画面（约 4s）；
2. `CONNECTION / SERIAL WAIT RELAY`（无线模式为 `WIFI WAIT RELAY`）——等待与
   开发机建立连接，闪烁框 + 板载 LED 同步闪烁；此时开发机需已运行 relay（串口
   模式）或可达的转发器（无线模式）；
3. 首个巡检成功后自动切到资源看板；连接中断显示 `... LINK DOWN`，恢复后自动回
   看板。

传输模式由设备配置自动判定（host 为 `127.0.0.1` 即 SERIAL，否则 WIFI），无需
手动区分。串口模式下建议让 relay 常驻并自动重连：

```bash
systemd-run --user --unit=velaops-relay --collect -p Restart=always -p RestartSec=2 \
  -p Environment=PORT=/dev/ttyACM0 -p Environment=SET_TIME=1 \
  -p Environment=PROXY_HOST=127.0.0.1 -p Environment=PROXY_PORT=28790 \
  -p Environment=FORWARD_HOST=127.0.0.1 -p Environment=FORWARD_PORT=28792 \
  -p StandardOutput=file:/tmp/velaops-relay.log -p StandardError=file:/tmp/velaops-relay.log \
  -p WorkingDirectory=$PWD \
  /usr/bin/python3 docs/tools/serial_llm_relay.py
```

板子重新上电导致 USB CDC 换 `ttyACM` 号时，relay 会退出并由 systemd 重启、自动
探测新端口重连，无需手工改端口。

## 4. AI Agent 只读巡检

当设备配置中的 Agent 路由指向开发机的 `28792` 转发端口时，先启动受限的
MiMo 转发器；它默认只绑定回环地址，供本板访问时显式限制监听地址和客户端：

```bash
cd /home/lu/桌面/openvela/contest2026_023_0xS3
set -a; source ./.velaops.local.env; set +a
BIND_HOST=192.168.71.90 ALLOWED_CLIENT=192.168.71.80 \
  python3 docs/tools/llm_forwarder.py 28792
```

确认 `curl http://192.168.71.90:28792/` 返回 `llm-forwarder ok` 后，再继续下方
的 Skill 安装和 Agent 流程。不要使用无客户端限制的 `0.0.0.0` 监听方式。

不要先启动 `ai_agent`。在 NSH 提示符下安装队伍 Skill 并注册工具：

```bash
bash docs/tools/provision_ai_agent_assets.sh
python3 docs/tools/send_raw.py 'ai_agent'
```

然后从仅保存在本机的 `.velaops.local.env` 读取 MiMo Key，以逐字符方式配置；
脚本会脱敏命令和设备回显：

```bash
set -a
source ./.velaops.local.env
set +a
DRAIN=10 python3 docs/tools/send_slow.py router_set mimo "$MIMO_API_KEY"
```

自然语言演示请求：

```bash
DRAIN=90 python3 docs/tools/send_slow.py ask 请检查VelaOps服务器状态
```

正确链路依次出现 `read_file`、`velaops_check_resources` 和单个 JSON 诊断结果。
资源证据中的字符串一律视为不可信数据；磁盘达到 85%、内存达到 80% 或 CPU 达到
85% 为 `warning`，服务非 active 或端口不可达为 `critical`。只读巡检也把
`requires_physical_approval` 固定为 `true`，为后续变更闭环保留安全边界。

首次成功调用 `velaops_check_resources` 后，Agent 专用后台巡检自动启动，每 5 秒
重新获取固定白名单资源。连续两次异常向消息总线注入一次 `opened`，持续异常去重；
连续两次正常注入一次 `recovered`。事件提示只包含本地可信的事件类型和代次，不
携带 Proxy 字符串，并要求 Agent 重新读取精确 Skill、调用固定只读工具取得新证据。
因此必须先由自然语言请求完成一次成功工具调用，不能在 Agent 消息循环就绪前启动。

同一启动周期不要退出后再次启动 `ai_agent`：当前上游工具注册表清理不完整，
重进可能丢失 Provider。需要重启 Agent 时应硬复位，并从本节之前的板端步骤重做。
若 MiMo 返回超时，先确认短提示也能成功；上游响应缓存可能复用同一句超时结果，
复测时改用等价措辞。不得因此绕过 Skill 或改用自由 shell。

若短提示也超时，直接演示本地规则降级：

```bash
DRAIN=15 python3 docs/tools/send_raw.py 'velaops diagnose-local'
```

输出仍是与 Skill 对齐的结构化诊断，但摘要以“本地规则降级”开头，不能把它介绍成
LLM 结论。该路径不依赖公网；Proxy 不可用时安全输出 `unknown` 和 `retry_check`，
Proxy 恢复后再次执行即可恢复真实诊断，无需重启设备。

### 4.1 CPU 满载主动告警演示（闪烁弹窗 + 板载 LED + LLM 结论）

用于演示“主动触发场景”的完整闭环。看板采集的是**开发机**（relay/proxy/转发器
所在机）的 CPU，阈值 **85%**；超阈值经去抖开单后：LCD 弹出闪烁告警框、板载 LED
同节奏闪烁，Agent 单轮诊断的结论会替换弹窗文字，恢复正常后自动收起。

```bash
# 1) 确保 relay 在跑（串口模式）；已在其它终端运行可跳过
PORT=/dev/ttyACM1 SET_TIME=1 \
  PROXY_HOST=127.0.0.1 PROXY_PORT=28790 \
  FORWARD_HOST=127.0.0.1 FORWARD_PORT=28792 \
  python3 docs/tools/serial_llm_relay.py

# 2) 压满 CPU 触发告警。默认全核 60s；演示建议 180~200s 覆盖整轮 LLM 诊断
python3 docs/tools/stress_cpu.py --seconds 200 --workers "$(nproc)"
```

预期观测（已真机验收）：
- LCD 先出现**闪烁告警框**，本地摘要带**元凶进程**（Proxy 采集的 `top_process`，
  例如 `stress_cpu.py 96%`）；
- **板载 LED**（ESP32-S3-EYE，经 `/dev/userleds`）随告警框闪烁；
- Agent 诊断返回后弹窗更新为 LLM 的短英文结论（prompt 要求其输出 `display` 字段，
  例如 `CPU HIGH`）；完整中文分析见 relay 日志/GUI（LCD 字库仅 ASCII）；
- CPU 回落后触发 `recovered`，弹窗与 LED 自动关闭；
- 短按 BOOT 可先消除弹窗（不翻页）。

注意：压测与 relay/proxy 同机，会一起抢占 CPU；脚本到点自动停止，避免影响随后
的 LLM 诊断。

## 5. BOOT 实体批准修复

停止独立目标制造 critical，Proxy 必须保持在线：

```bash
systemctl --user stop velaops-demo-target.service
```

Agent 已经给出针对 `demo` 的 critical + `restart_service` 建议后，再明确请求修复。
Skill 要求当前会话已有该诊断才允许执行，合并成一句 ask 无效；两步 ask 也不能背靠背
连发（②会先于①完成被消费），必须等①的诊断 JSON 出现后再发②。已真机验证的有效措辞：

```bash
# ① 诊断（等回复出现 recommended_action 后再发②）
DRAIN=180 python3 docs/tools/send_slow.py ask 检查demo服务状态
# ② 修复（显式点名工具，避免模型重新诊断而非执行）
DRAIN=180 python3 docs/tools/send_slow.py \
  ask 我确认执行修复，请立即调用velaops_restart_service工具重启demo服务
```

两步法可直接用 `docs/tools/orchestrate_two_step.sh` 编排（内含串口监听与等待逻辑）。

只有出现板端提示后，才在 30 秒内连续长按 BOOT 2 秒。Tool 不接受服务、目标、命令、
批准 ID 或时间参数；设备生成 60 秒有效的硬件随机批准，执行固定 `demo` 服务重启，
然后用新的 request ID 最多 5 次重新取证。最终只有服务 active 且端口可达才输出
`execution_state=completed`、`verified=true` 和 `status=recovered`。

若 MiMo 当前不可用，可用完全相同的设备/Proxy/按键闭环做确定性验收：

```bash
DRAIN=45 python3 docs/tools/send_raw.py 'velaops repair-demo'
```

不按键时必须得到 `not_started + approval_timeout`，且 Proxy 不得出现
`restart_service` 审计。传输中断时返回 `execution_state=unknown`，不得自动重试。

## 6. 联调 Debug 控制台（LCD 弹窗闭环）

主机侧纯标准库浏览器 GUI，用于一键触发 Debug 事件联调：

```bash
PORT=/dev/ttyACM0 python3 docs/tools/debug_event_gui.py
# 浏览器打开 http://127.0.0.1:8765/
```

- 「🚨 触发 DEBUG 事件」：向设备发送联调 ask，模型直接调用 `velaops_show_message`
  （text=TEST-OK），LCD 弹出 AGENT MESSAGE 提示框并用中文回复测试正常；
  短按 BOOT 消除弹窗并恢复资源看板。已真机验收：端到端约 10s。
- 「📊 例行资源巡检」：只读巡检 ask，验证 Skill → 工具链路并拉起屏显看板。
- 页面实时滚动串口日志；检测到弹窗工具调用时顶部横幅变色提醒。
- 串口为独占资源：GUI 运行期间不要再跑其他串口脚本；烧录前必须先停止 GUI。
- 弹窗内容仅支持 ≤ 16 字符可打印 ASCII（字库限制），中文回复看 GUI 日志。
- 资源事件告警阈值为内存 80%、磁盘 90%（演示服务器磁盘基线 85.4%，
  85% 会让主动事件永久触发）；屏显配色仍用 85% 视觉提醒。

## 7. 已知限制

- `/data` 当前为 tmpfs，烧录或断电后设备配置会丢失；开机由 TF 卡自动配置恢复。
- Proxy/设备配对凭据位于持久的 `$HOME/.local/state/velaops-live/`，开发机重启不会
  丢失；只有显式 `--force` 才会轮换密钥。
- 演示目标是一次性 Python HTTP 服务，仅用于验证白名单重启和端口复核，不代表
  生产服务本身。
- HTTP 只适用于比赛 Demo 的可信局域网，不用于公网或生产部署。
- 当前固件的局域网 NTP 地址/端口来自 vendor 本地 `defconfig`：
  `192.168.71.90:40123`。
- MiMo 是当前唯一 LLM 后端；公网 TLS 或服务端不稳定时，Agent 安全返回超时，
  不影响本地看板和 Proxy，也不会执行变更。
- 曾出现“新鲜窗口”掉线：复位后看板巡检与 ai_agent 网络服务几乎同时发起首个请求，
  数秒内会打断 ESP32-S3 的关联（表现为持续 `transport_error`、收发计数全零）。现已
  在设备端修复：看板延迟 20s 再开始巡检，避开 Agent 建链窗口。看板侧只诊断、不主动
  改接口——实测运行期 `renew wlan0`、`ifdown/ifup`、`wapi disconnect + essid` 在
  ai_agent 同时运行时都可能连带打断正常链路甚至整机静默。真掉线时用
  `velaops net-check` 复核，仍不通则重新上电，由开机自动配置重建链路；开机凭据会
  缓存到 `/data/velaops/wifi.txt` 供诊断。
- 含中文的命令必须用 `send_slow.py` 逐字符发送，快发打散会导致 MiMo 报 400。
- SIMPLE 复杂度回复存在 LLM 缓存，复测同一请求必须换措辞，否则会回放旧结果。

## 8. 验收结果

- 演示配置生成器 3 项测试通过：成对配置与私密权限、拒绝隐式轮换、非法地址与
  符号链接拒绝；生成的真实配置已通过 Proxy 严格加载；
- 演示目标管理器 3 项测试和真实 user-systemd 启停通过；目标停止后，经 HMAC 认证、
  未过期实体批准声明的 `restart_service` 返回 `verified=true`，目标端口随后恢复；
- Proxy 93 项测试、设备端 10 组主机测试和 ESP32-S3 目标构建通过；
- 2026-08-31 设备端增至 16 组主机测试，串口推送 4 项、脱敏 5 项测试通过；
- 当前交付固件大小 `1,898,524` 字节（含屏显按键 read 死循环修复），SHA-256：
  `f8d0921e625068bc7361e003537334bbc8732a1c353497a795bcc9c8c9e97c45`；
- esptool 烧录后数据哈希校验通过；
- 真机首次 NTP 校时和 HTTP+HMAC 认证通过；
- 连续五轮真实资源刷新通过；
- Proxy 停止后板端保持运行并报告离线，Proxy 恢复后自动恢复连续刷新。
- AI Agent 已多次完成自然请求 → 精确 Skill → 只读工具 → 单个严格 JSON 的真机
  闭环，真实数据约为内存 25.12%、磁盘 84.65%、服务 active、端口可达。MiMo
  公网链路仍会间歇超时；超时不影响本地看板、Proxy 或规则降级路径。
- `diagnose-local` 已用真实 Proxy 输出磁盘 85.21% 的 `warning`；停止 Proxy 后输出
  `unknown + retry_check`，恢复 Proxy 后无需重启板卡即恢复真实诊断。
- `velaops monitor` 已真机连续取得五次真实数据：第二次 warning 后仅产生一次
  `opened generation=1`，后续三次持续 warning 未重复事件。
- Agent 专用主动巡检已真机验证：首次成功工具调用后自动采样；将监控端口临时改为
  不可达后，第二次异常只入队一次 `opened`，持续异常不重复；恢复端口后，第二次
  正常只入队一次 `recovered`。测试后 Proxy 监听和监控端口均已恢复为 `28790`。
- BOOT 批准闭环已真机验证：30 秒不按键返回 `approval_timeout`，目标保持 inactive
  且没有变更审计；连续长按 2 秒后，Proxy 仅记录一次 `restart_service`，板端使用
  新 request ID 的第一次 `check_resources` 复核即确认服务 active、端口可达。
- **LLM 执行闭环已真机验收（2026-08-31）**：两步法措辞下模型真实调用
  `velaops_restart_service`（诊断 159s、critical + restart_service），批准后服务恢复，
  复核 `active/running`、端口可达，审计新增唯一 `restart_service` 开始/成功对，目标返回 200。
  联调时批准环节由临时文件钩子（`/data/velaops/auto_approval`）自动化，验收后已删除，
  交付固件恢复真实按键路径。
- 5 秒资源巡检与 LLM 调用并发时保持精确 5s 审计间隔，无失败记录。
- **DEBUG 弹窗联调闭环已真机验收（2026-08-31）**：联调控制台一键触发后，模型 10s 内
  直接调用 `velaops_show_message`（未绕道巡检），LCD 弹出提示框，中文回复测试正常，
  短按 BOOT 消除后看板继续 5s 刷新；屏显/监控双线程后台取证零日志刷屏。
- **Wi-Fi 稳定性修复已真机验收（2026-09-15）**：定位到看板首个巡检请求与 ai_agent
  网络服务建链竞态会让 ESP32-S3 数秒内掉线。加入 20s 巡检启动延迟、并移除运行期
  主动改接口的自愈后，同一条只读链路上 `ai_agent + 屏显看板` 连续运行 12 分钟、
  139 次 5 秒巡检全部成功，Proxy 审计 140 条请求平均间隔 5.30s、零错误、零自愈
  （`transport_error=0`）；此前多轮 5~8 分钟测试同样零错误。

## 9. 英文 LLM 结论与看板验收（真机已跑通）

编译前执行 `bash docs/tools/apply_nuttx_patches.sh`。LLM 诊断会把英文 summary、根因和建议
分别写入三页 LCD 内容（页 3~5：`LLM SUMMARY` / `LLM ROOT CAUSE` / `LLM ACTION`）；诊断
结论写出后看板会**自动跳到 LLM SUMMARY 页**，告警弹窗仍会同时弹出，短按 `BOOT` 消除弹窗
后即见该页。

**按键（当前保底方案）**：`BOOT` 短按翻页（6 页循环），`BOOT` 长按 2 秒批准修复。
ESP32-S3-EYE 的 MENU/PLAY/UP+/DOWN 接 ADC1_CH0（GPIO1）电阻梯，原理图与官方 BSP 均已
确认，但 NuttX `esp32s3_adc.c` 读出恒为 ~1334mV、按任何键不变（驱动层问题），故暂用
BOOT 单键保底；`patches/vendor/0002` 保留 ADC 映射以便后续修复。

**LLM 提示约束（踩坑）**：ai_agent 的文件 ask 通道只读第一行，故诊断 prompt 必须单行；
且提示里 `display` 后不能跟空格（`play ` 会被 NL 快速通道误判成播放音乐）。
