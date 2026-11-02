# 串口隧道传输（WiFi 不可用时的有线方案）

> 用途：ESP32-S3 的 WiFi 在 NuttX 下会随机掉线（详情见本文末“背景”），
> 比赛演示改用 USB 串口承载设备侧全部 HTTP 业务流量。本文是这套方案的设计、
> 使用与交接说明。

## 1. 设计（分层解耦）

```text
设备侧                                  开发机侧
┌─────────────────────────┐             ┌──────────────────────────────┐
│ ai_agent / VelaOps 看板  │             │ serial_llm_relay.py          │
│   └─ HTTP(127.0.0.1:18080)│             │   ├─ 解帧 → 原始 HTTP        │
│        └─ velaops tunnel │  USB CDC    │   ├─ /v1/chat/completions →  │
│             └─ 帧化 ─────┼────────────►│   │   llm_forwarder:28792    │
│                 ◄────────┼─────────────┤   └─ 其它 → proxy:28790      │
└─────────────────────────┘  @@VOPREQ   └──────────────────────────────┘
```

- 设备侧所有业务请求都指向 `127.0.0.1:18080`；`velaops tunnel` 把它读到的
  HTTP 请求按 4/3 膨胀成单行 base64 帧 `@@VOPREQ <b64>` 写到控制台 stdout。
- 开发机 relay 读串口、解帧，按路径转发到本机 Proxy(28790) 或 LLM 转发器
  (28792)，再把响应 base64 写回设备 `/tmp/vop-in.b64` 并置位 `/tmp/vop-in.ready`。
- 设备 tunnel 轮询到 ready 后解码并回写 socket，交给 ai_agent/看板。
- **WiFi 与传输层解耦**：切换只改 SD 凭据里的 `TRANSPORT`，代码不动。

## 2. 开关：wifi / serial

凭据文件 `/mnt/sd/velaops-credentials.txt` 增加一项：

```text
TRANSPORT=serial     # 走串口隧道（推荐，WiFi 修复前）
TRANSPORT=wifi       # 设备 WiFi 直连（默认；openvela 修好 WiFi 后切回）
```

`velaops_autoconfig_run` 按该字段决定：

| 行为 | serial | wifi |
| --- | --- | --- |
| 隧道任务 | 启动 | 不启动 |
| WiFi 连接 | 尽力而为，失败继续 | 必须成功，失败退出 |
| 设备配置 host/port | `127.0.0.1:18080` | `DEMO_HOST:DEMO_PORT` |
| Agent LLM 路由 | `127.0.0.1:18080` | `DEMO_HOST:28792` |
| 看板首次巡检延迟 | 0s | 20s（避开建链竞态） |
| NTP | 由 relay 注入时间；WiFi NTP 仅尽力 | WiFi NTP |
| 日志级别 | ERROR（保证帧不被日志插断） | 默认 |

**切回 WiFi**：把 SD 凭据改回 `TRANSPORT=wifi`（或删除该行），复位即可。上游
`nuttx/vendor/ai_agent` 不需要任何改动。

## 3. 开发机准备

```bash
# 演示服务（Proxy/NTP/转发器）默认绑回环即可，relay 在本机访问
HOST_IP=127.0.0.1 BOARD_IP=127.0.0.1 \
  bash docs/tools/install_demo_services.sh

# 启动 relay（保持前台或放后台）
PORT=/dev/ttyACM0 SET_TIME=1 \
  PROXY_HOST=127.0.0.1 PROXY_PORT=28790 \
  FORWARD_HOST=127.0.0.1 FORWARD_PORT=28792 \
  python3 docs/tools/serial_llm_relay.py
```

- `SET_TIME=1`：启动 6s 后向板端注入当前 Unix 时间（HMAC 需要，隧道不搬 NTP）。
- 串口端口以实际枚举为准（重上电后可能是 `/dev/ttyACM1`）。

## 4. 板端准备

1. 把含 `TRANSPORT=serial` 的凭据写到 TF 卡根目录。
2. 复位后串口应出现：`传输模式=serial` → `串口隧道已启动` → `listening 127.0.0.1:18080`
   → `认证成功` → `全部完成` → `屏显看板已启动`。

## 5. 已知问题（交接重点）

1. **控制台是单串口复用**：日志/回显与 base64 帧共用一路 CDC，偶发日志插进帧
   导致 relay 丢帧（表现为看板 `transport_error`）。已做：串口模式收敛到 ERROR、
   帧前后加换行、relay 只在帧内提取 base64。**仍未完全杜绝**，是当前主要遗留。
2. 因此**没有做请求重发**：重发会被 Proxy 判为 `replay_detected`（已实测）。
   如需重发，应先在 Proxy 侧放宽同一 request_id 的重放窗口或改为幂等确认。
3. relay 回写响应改为 `echo '<48 字符>' >> /tmp/vop-in.b64`（48 是 4 的倍数，
   否则板端 base64 解码跨行错位会提前截断），逐行等回显再发；单个响应约 2-7s。
4. **LLM 诊断与看板巡检并发会让隧道卡死**：隧道同一时刻只服务一个请求。已做：
   入队 LLM 诊断后看板巡检退避 90s（`VELAOPS_LLM_PAUSE`，`velaops_main.c`），
   让出隧道给 Agent。验证后隧道不再永久卡死；诊断窗口内仍可能有个别
   `transport_error`，窗口结束自动恢复。
5. 若后续要彻底解决复用问题，建议方向：把隧道改到独立 UART（SLIP/PPP），或
   让隧道支持多连接独立状态。

## 6. 背景：为什么放弃 WiFi

- WiFi 链路对“看板巡检 + ai_agent 建链”并发敏感，复位后数秒内可掉线；
  20s 启动延迟可缓解但无法根治。
- LLM 请求（~10-20KB）会打断关联（排除 AP 因素：家用路由器同样复现）。
- 断开后 `reason=8`，驱动重连不回来，需重新上电。
- 结论：这是 NuttX + esp32s3 WiFi 驱动层问题，非本项目应用层可完全规避，
  故比赛期间用有线传输保证演示稳定。

## 7. 上游（ai_agent）改动：编译前打补丁，上游 0 改动

Agent 相关能力不直接改上游源码，全部放在 `patches/ai_agent/`，由
`docs/tools/apply_nuttx_patches.sh` 在编译前打入 `packages/ai_agent`：

- `0001-ask-queue-file-channel.patch`：`nsh_commands.c` 增加 `/tmp/vela-ask.txt`
  文件队列通道（绕开串口下 nsh 与 agent 抢输入）。
- `0002-outbound-reply-and-llm-io-lock-hooks.patch`：
  - `agent_main.c`：outbound 回发处提供 `agent_set_reply_hook`（函数指针，未注册
    空操作），设备侧据此把诊断结论写到 LCD 告警框并标记诊断完成；
  - `llm_proxy.c`：LLM HTTP 调用前后提供 `llm_set_io_lock_hook`，设备侧注册
    隧道全局锁，让 **LLM 大请求与看板/工具/后台采样共用同一把锁**，避免单条
    半双工隧道抢道（这是"诊断后卡死"的根因修复）。

两处均为"函数指针 + 未注册即空操作"，不改变上游默认行为，可独立编译。

## 8. 相关文件

- `app/hello_app/src/velaops_serial_tunnel.c` / `include/velaops_serial_tunnel.h`
- `app/hello_app/src/velaops_autoconfig.c`（TRANSPORT 分支）
- `app/hello_app/src/velaops_proxy_http_transport.c`（隧道请求全局互斥 + 有界锁）
- `app/hello_app/src/velaops_agent_display.c`（Agent 回发 → LCD 告警框桥接）
- `docs/tools/serial_llm_relay.py`（开发机 relay）
- `docs/tools/install_demo_services.sh`（服务栈）
- `patches/ai_agent/`（上游 ai_agent 补丁，编译前由 apply 脚本打入）
