# VelaOps Sentinel 项目持续记忆

> 保存团队与 AI 协作中经确认的稳定事实、架构决策和当前工作区状态。
> 未经实测的设想不得写成“已完成”；旧环境记录与当前工作区必须分开。
>
> 最后整理：2026-08-31

## 1. 项目身份

- 项目名称：**VelaOps Sentinel（维拉哨兵）**。
- 参赛方向：openvela AI 硬件产品创新赛道。
- 设备：ESP32-S3-EYE。
- 软件基础：openvela / NuttX + ai_agent。
- 产品目标：主动发现服务器异常，分析原因，经安全授权后执行受控修复，再独立复核并审计。
- 队伍仓：`contest2026_023_0xS3/`。

## 2. 当前架构决策

P0 主路径采用 **Plan C**：

```text
ESP32-S3-EYE
  │ HMAC 认证的结构化 HTTP API
  ▼
VelaOps Proxy（局域网可信主机）
  │ 本地执行或 SSH
  ▼
Linux Server / NAS / Dev Machine
```

设备端负责交互、调度、本地规则、Incident 状态机、LLM 分析和物理授权。Proxy 负责身份验证、防重放、Action 白名单、参数校验、受控执行和结果脱敏。

板端直连 SSH 原型仅作历史实验参考，不是 P0 主架构。切换原因是 ESP32-S3 Wi-Fi 发送链路在 SSH 控制流量下有间歇性失速和死锁，未达到稳定性门槛。

## 3. 已验证的实机基线

以 `handover.md` 和 `ai_agent_bringup.md` 为准：

- ESP32-S3-EYE 已跑通 Wi-Fi → DNS → TLS → MiMo LLM 对话。
- BLE 必须关闭，否则 Wi-Fi 数据通路不稳定。
- SMP 必须关闭，否则 ai_agent 工具注册后可能崩溃。
- DNS 固定为 `114.114.114.114`，TCP 接收缓冲为 16384。
- LCD ST7789 RAMCTRL 与 RGB565 端序问题已恢复：参数按 `00 F0` 两个有序
  8 位值发送；`velaops display-test` 实机确认红绿蓝顺序和黑白灰阶正确。
- LLM watchdog 已改用 `CLOCK_MONOTONIC`，避免系统从 1970 校时到 2026 时误判超时；见 `packages/ai_agent` 本地提交 `09642b3`。
- 修复已通过主机边界测试、ESP32-S3-EYE 增量编译与烧录；重启后的首次 MiMo 调用实机返回“首次调用测试通过”。
- 串口使用 raw tty，不得操作 DTR/RTS。

## 4. 旧环境与当前工作区

### 4.1 旧环境曾记录

- Plan C C01–C05 首版曾完成。
- SD 卡持久化、固件烧录和 LCD 数据显示曾通过测试。
- 原计划继续执行 `TASK-C06`～`TASK-C10`。

### 4.2 2026-08-24 当前工作区事实

> 本节保留重构过程的历史记录。涉及 HTTPS、CA、测试数量和资源看板阻塞状态的
> 旧条目已被下面 4.3 的 2026-08-26 真机结果取代。

- 已恢复项目方案、ai_agent 实机调通和 Wi-Fi 文档。
- 队伍仓当前本地分支为 `local-planc-rebuild`，不跟踪远程问题分支。
- 旧环境的 Plan C C01–C05 源码、`TASK-C06`～`TASK-C10` 和 handoff 仍未恢复。
- 当前已从本地基线完成可运行的 Proxy P0 本机垂直切片：TLS、HMAC v1、nonce 防重放、严格 JSON schema、目标/资源白名单、有界无 shell 执行器、只读诊断、实体批准、`restart_service`、SQLite 幂等和 JSONL 审计；代码位于 `proxy/`。
- Proxy 支持 `check_service`、`check_port`、`check_disk`、`check_memory`、`read_service_log` 和 `restart_service`。非回环监听强制 TLS 1.2+。
- `make -C proxy check` 当前通过 89 项测试、Python 编译和 JSON 校验。
- 真实 CLI + HTTPS + user-systemd 端到端联调已通过：五个只读 Action、未批准拒绝、批准重启后 PID 变化、同 request ID 不二次重启、错误签名和 nonce 重放拒绝均已验证。
- 端到端测试共产生 18 条审计，不包含 Action 参数或测试密钥；幂等库仅有一条 succeeded 变更。临时 unit、证书、密钥、配置和状态目录已全部删除。
- 远端跟踪分支只保存已知有问题的直连 SSH 原型，**不得用其覆盖当前本地重构基线**。
- `packages/ai_agent`、`vendor/espressif` 和 `nuttx` 存在实机调通相关本地修改，修改前必须先看 diff。
- 设备端 C 客户端已在本地代码上重建：HMAC v1、严格 Proxy 响应解析、指定 CA 的 HTTPS、私密配置、NTP 校时和 ESP32-S3 硬件 TRNG 均已通过主机/目标编译及真机验证。
- 真机 `velaops auth-check` 已完成 TLS 1.2 + HMAC 认证；`velaops check-memory` 已完成白名单 Action 调用并返回结构化内存快照，Proxy 审计成对记录开始/成功事件。
- 当前手工联调的 `/data` 为 tmpfs，重启后私密配置与 CA 会丢失；这是测试环境事实，不得误记为已完成持久化。
- 纯领域层已实现 HealthSnapshot、内存一致性/阈值规则和 Incident 去抖状态机；持续异常只开一次 Incident，连续恢复后才产生 recovered 边沿，乱序或损坏快照不修改运行态。
- Proxy `check_memory` 结果已有严格 JSON 适配器，拒绝未知/重复字段、非整数字节数、超过 JSON 安全整数范围和字节/百分比不一致。
- 当前共 7 组设备端 C 主机测试，已通过严格警告门禁和 ASan/UBSan；本轮显示代码已完成 ESP32-S3-EYE 目标编译，且新增代码无编译警告。OpenCode 初始失败原因是 shell 未加载仓库内工具链 PATH，不是代码或工具链缺失。
- 已在队伍仓新增设备端 `velaops monitor`：复用 TLS + HMAC 的 `check_memory`，通过板级实际 LCD 节点 `/dev/lcd0` 在 ST7789 240x240 屏幕显示服务器内存、健康和连接状态；BOOT 短按通过 `/dev/buttons` 切换三页。最初误用 `/dev/fb0` 和 `/dev/video0`，已通过真机节点检查纠正：`/dev/video0` 是摄像头，不能作为 LCD。
- 2026-08-25 已使用仓库内工具链和 `/dev/ttyACM0` 真机完成两轮编译/烧录；最终固件 `nuttx/nuttx.bin` 大小 1,828,724 字节，SHA-256 为 `379dcdd1880dd8e712fd2b6006bbcfcbaefe579b20719cf5ef5fc111c5153597`，esptool 写入后 Hash of data verified，启动进入 NSH，`/dev/lcd0` 和 `/dev/buttons` 均存在。
- 设备端显示适配经过 `/dev/fb0 + mmap + FBIO_UPDATE` 与 `/dev/lcd0 + LCDDEVIO_PUTAREA` 两条路径对比；framebuffer 路径 ioctl 返回成功但页面颜色未在 LCD 上体现，当前已切回之前实际能显示 UI 的 `/dev/lcd0` 路径。BOOT 按键串口已确认产生 `sample=0x1/0x0` 和 `page=2/3/1`，按键链路正常；视觉页面切换仍需用户确认。
- 真机执行 `velaops monitor` 已不再报告显示设备打开错误，进入监控循环；但烧录重启后 `/data` 是 tmpfs，私密配置和 CA 丢失，当前只观察到 `配置加载失败: io_error`，尚未完成 Proxy 联网资源显示和实际按键翻页的视觉验收。重新下发 `/data/velaops/config.json` 与 `/data/velaops/proxy-ca.pem` 后才能完成该项真机回归。
- 已创建本机私密联调文件 `.velaops.local.env`，包含用户提供的 Wi-Fi、MiMo 和测试服务器凭据，并由 `.gitignore` 忽略；凭据不得进入 Git、日志或普通文档。SD 卡已识别为板上 `/dev/mmcsd1`，本轮未清空或改写，避免误删旧数据。
- Proxy 已新增只读聚合 Action `check_resources`，一次返回内存、配置白名单磁盘、服务和端口摘要；设备端新增严格资源结果适配器，`velaops monitor` 三页分别显示内存、服务/端口和磁盘/延迟。Proxy 89 项回归、设备端主机测试和 ESP32-S3-EYE 目标编译均通过；真实 Proxy 凭据下发和联网资源显示真机联调待下一步执行。
- 当前真实联网联调阻塞于两项未提供/未生成的本地资产：设备 HMAC secret，以及 Proxy TLS 服务器证书对应的 CA。Wi-Fi、MiMo API Key 和 Linux 测试用户已保存到被忽略的 `.velaops.local.env`，不得替代设备 HMAC secret，也不得进入普通日志。未获得这些资产前，不能把真实资源显示记为真机通过。
- 2026-08-25 已完成本机 Proxy + ESP32-S3-EYE 真实资源联调：局域网 NTP 校时成功，TLS 1.2/HMAC `auth-check` 成功；设备持续获取真实资源并显示，实测内存约 24.4%、根磁盘约 85.07%，`dbus.service` 为 active/running，Proxy 端口可达。测试 Proxy、CA、HMAC 和 NTP 资产均在 `/tmp/opencode/velaops-live/`，不进入 Git。
- 本轮修复了实际 vendor 板级 `esp32s3_bringup.c` 的 `/data` tmpfs 挂载；修复 `serial_push.py` 对 `rm -f` 和 NSH 提示符的误判。设备配置与 CA 可写入 `/data/velaops/`，但该目录仍是 tmpfs，重启/烧录后会丢失。
- 设备 HMAC 配置要求设备端 ASCII secret 与 Proxy `secret_hex` 解码后的原始字节一致；联调中已使用 32 字节 ASCII 测试密钥配对成功。

“C01–C05 已完成”是旧环境的历史验收记录，不代表当前工作区已具备对应旧代码。当前重建功能只以本轮新测试和新提交为验收依据。

### 4.3 2026-08-26 当前真机基线

- 比赛 Demo 已从 HTTPS 调整为可信局域网 HTTP + HMAC；Proxy 仅在配置显式设置
  `allow_insecure_http: true` 时允许非回环 HTTP，TLS 实现保留供后续 Guarded 模式使用。
- 设备端使用独立 POSIX socket HTTP 适配器，上层 HMAC 协议客户端、严格 JSON
  响应解析和领域规则不变；不再读取 `/data/velaops/proxy-ca.pem`。
- Proxy 提供普通用户可运行的局域网 NTP 模块。当前固件配置时间源为
  `192.168.31.139:40123`，避免依赖公网或 Linux 特权 UDP 123 端口。
- `make -C proxy check` 通过 93 项测试；设备端 9 组主机测试和 ESP32-S3 目标构建
  通过。固件大小 1,828,956 字节，SHA-256 为
  `455fe66c41f201c7d9277b94d4b1423178405c2c9ddefb68c64e44f7af2bfdfa`。
- ESP32-S3-EYE 真机烧录哈希校验通过，首次局域网 NTP 校时和 HTTP+HMAC 认证通过；
  `velaops monitor` 连续五轮取得真实内存、磁盘、服务和端口数据。
- 主动停止 Proxy 后设备保持运行并连续报告 `transport_error`；Proxy 恢复后无需重启
  板子，在下一刷新周期自动恢复资源数据。
- `/data` 和 Wi-Fi 仍未持久化，烧录或断电后需按 `DEMO_RUNBOOK.md` 重新连接和下发
  配置。这是明示限制，不阻塞当前通电状态下的比赛演示。

### 4.4 2026-08-27 AI Agent 只读巡检

- 已在队伍目录实现外部 Tool Provider `velaops_check_resources`，只接受空对象并
  固定调用 `check_resources`；证据标记为 `untrusted_server_evidence`，不向 LLM
  暴露任意主机、Action 或 shell 参数。
- 已新增自定义 Skill `server-incident-response.md`，定义 normal/warning/critical/
  unknown 阈值、严格 JSON 输出和实体批准边界。实际数据目录由 defconfig 决定为
  `/data/ai_agent/skills/`，不是官方文档中的通用示例 `/data/agent/skills/`。
- 当前上游 Skill 加载器会在标题后的首个空行停止读取摘要，因此队伍 Skill 标题
  下一行必须直接放路由描述，且标题与文件名一致；对应约束已有自动测试。
- 已多次真机完成自然请求 → `read_file` 精确 Skill → 只读 Tool → 单个严格 JSON
  的完整闭环；真实数据约为内存 25.12%、磁盘 84.65%、服务 active、端口可达。
  MiMo 公网链路仍存在间歇超时，失败时本地看板、Proxy 和规则降级路径不受影响。
- ai_agent 同一启动周期退出后重进存在上游 Provider 清理问题；Demo 中每次硬复位后
  先注册队伍工具，再只启动一次 ai_agent。
- AI Agent 只读接入阶段固件大小 1,829,972 字节，SHA-256 为
  `58f61cdc34fbb883ffe0e864e063d07ea883005a81fe7b996de47868184753b5`，目标构建、
  烧录写入哈希校验、LCD 命令、Wi-Fi、HTTP+HMAC 均已通过。
- 已新增 `velaops diagnose-local` 确定性降级路径，复用同一份资源结果适配器和
  Skill 阈值，输出相同 7 字段 JSON，并在摘要中明确标记“本地规则降级”。真机
  已验证真实磁盘 85.21% 输出 `warning`；Proxy 停止时输出
  `unknown + retry_check`，恢复后无需重启设备即可再次输出真实诊断。
- 本地降级阶段固件大小 1,830,604 字节，SHA-256 为
  `a2ce90ebdc45ec60d3f2a286b00e4ff1a14a8ac034881d407d4d7283c09678b7`，烧录写入
  哈希校验通过。该降级是规则引擎结果，不能在演示中冒充 LLM 推理。
- 资源看板已接入本地主动 Incident：连续两次 warning/critical 开单，持续异常去重，
  连续两次 normal 恢复；坏证据不改变状态。真机磁盘 85.21% 在第二次刷新产生唯一
  `opened generation=1`，随后三次持续告警未重复。
- 已增加 Agent 生命周期安全的主动巡检适配器：只在首次成功的
  `velaops_check_resources` 调用后启动单个 5 秒后台采样线程，连续两次异常向
  Agent 消息总线注入一次 `opened`，持续异常去重，连续两次正常注入一次
  `recovered`。提示词只携带可信事件类型和代次，要求重新读取 Skill 并用固定只读
  Tool 取证，不拼接 Proxy 证据。真机已通过不可达端口与恢复端口的开单/去重/恢复
  回归。当前固件大小 1,830,972 字节，SHA-256 为
  `9d9106d1d91960fbedb238258841747b55a988e14280e964baab8e25fb66a167`；14 组设备端
  主机测试、ESP32-S3 目标构建、烧录写入校验及 HTTP+HMAC 认证通过。
- 2026-08-30 开发机重启验证 `/tmp/opencode/velaops-live/` 会消失；已新增
  `docs/tools/prepare_live_demo.py` 原子生成成对 Proxy/设备配置。目录权限固定为
  `0700`、配置固定为 `0600`，脚本不输出密钥并默认拒绝覆盖；显式 `--force`
  轮换后必须重新下发设备配置。生成器 3 项测试及 Proxy 严格配置加载通过。
- Demo 配置现包含独立的无特权 user-systemd 目标 `velaops-demo-target.service`：Proxy
  保持监听 `28790`，目标只监听回环 `28791`，可停止目标制造故障而不切断控制面。
  管理器与生成器各 3 项测试、真实 start/stop、Proxy HMAC 批准重启均通过；重启
  返回 service active 后端口仍有短暂启动窗口，因此板端必须再做有界独立复核。
- 已实现固定无参数 Agent Tool `velaops_restart_service` 和同实现 CLI
  `velaops repair-demo`。设备在 30 秒窗口内要求 BOOT 连续长按 2 秒，松开会重新
  计时；批准 ID 来自硬件随机源且有效 60 秒。LLM 无法传入服务、目标、命令、批准
  或时间。执行后最多 5 次使用新 request ID 重新获取 `check_resources`，传输结果
  不确定时禁止自动重试。
- 2026-08-31 真机拒绝路径返回 `not_started + approval_timeout`，目标保持停止且无
  `restart_service` 审计；正向长按仅产生一次变更审计，第一次独立复核即返回
  `completed + verified + recovered`。当前固件 1,832,020 字节，SHA-256 为
  `9689a2e2d7119073e279d591971e48ce69fa515ceee5474715652dfbcb9b4ea7`；16 组设备
  主机测试、Skill 契约、目标构建、烧录写入校验通过。自然语言触发变更 Tool 仍需
  视 MiMo 公网可用性补做回归，不能把 CLI 真机结果冒充 LLM 已调用该 Tool。

### 4.5 2026-08-31 LLM 执行闭环真机验收与交付固件

- **自然语言调用变更 Tool 的回归已完成**：两步法措辞（①`ask 检查demo服务状态`
  得到 critical + restart_service 诊断；②`ask 我确认执行修复，请立即调用
  velaops_restart_service工具重启demo服务`）下，模型真实调用 `velaops_restart_service`，
  串口出现 BOOT 批准提示，批准后服务恢复 `active/running`、端口可达，Proxy 审计新增唯一
  `restart_service` 开始/成功对，`curl` 目标返回 200。这是 DEMO_RUNBOOK 第 5 节定义的闭环。
- 联调期间用临时文件钩子（`/data/velaops/auto_approval` 存在时模拟长按 BOOT 2 秒）
  完成批准环节自动化；批准后流程与真实按键完全一致。**验收通过后钩子已删除**，
  交付固件恢复真实按键路径，工作树与交付固件均不含临时调试代码。
- 确定性路径同轮复验：`velaops repair-demo` 返回 `completed + verified + recovered`，
  审计与 HTTP 200 证据齐全。
- 交付固件 1,832,316 字节，SHA-256 为
  `27fd89d4d956410df06ecc79393ececaa55d70b2cdd0b744cf8746a999b6315b`，烧录哈希校验通过。
- `packages/ai_agent` 本地分支新增 7 个功能提交：传输错误纳入退避重试；vela_tls 读/写
  阶段总时长封顶；LLM 超时调整为总时长 180s + 间隔 60s；WiFi 连接健壮性；MiMo 预置模型
  改 `mimo-v2.5`；两处构建修复。上游依赖与 PR 方向待大赛规则确认后整理。
- 板端联调陷阱（已固化进 DEMO_RUNBOOK）：`/data` 是 21K tmpfs，复位后必须重新 `mkdir`
  并下发；`quit` 退出 Agent 后同周期重进会丢 velaops Provider（上游注册表清理问题），
  异常一律硬复位重做；SIMPLE 复杂度回复会被 LLM 缓存，复测必须换措辞；含中文命令必须用
  `send_slow.py` 逐字符发送，否则 MiMo 报 400；两步 ask 不能背靠背连发，须等①的诊断
  JSON 出现后再发②。
- WiFi 存在“新鲜窗口”规律（未根治）：复位后前 1~3 次 LLM 调用健康，之后链路劣化
  （ssl_read ret=0x4c、net_connect ret=0x52、巡检 transport_error、整机静默卡死），
  只能硬复位恢复。该现象不影响单次闭环演示，已如实记录。
- 新增联调工具：`docs/tools/boot_watch.py`（后台串口监听 + BOOT 提示标志文件）、
  `docs/tools/orchestrate_two_step.sh`（两步法闭环编排）；`serial_push.py` 兼容 `vela>`
  提示符。

### 4.6 2026-08-31 屏显管理器与 DEBUG 弹窗联调闭环（纯队伍仓）

- 新功能全部在队伍仓实现（上游约束见 `docs/AI_AGENT_TRACK_CHECKLIST.md`）：
  `velaops_screen` 屏显线程跑在 ai_agent 进程内（Tool Provider 链接），首次成功取证或
  弹窗调用时幂等拉起；250ms 节拍：5s 资源看板刷新、弹窗 500ms 琥珀/红边框闪烁、
  BOOT 短按消除弹窗/看板翻页。LCD 字库仅 5x7 ASCII，弹窗文本在屏显层净化。
- 新工具 `velaops_show_message`：唯一接受文本参数的工具（text ≤ 16 ASCII），只做屏显
  不触服务端；工具描述强制模型对屏显/调试类请求直接调用，跳过巡检与 Skill。
- **DEBUG 弹窗闭环已真机验收**：主机 `docs/tools/debug_event_gui.py`（纯标准库
  浏览器 GUI，127.0.0.1:8765）一键触发 → 模型 10s 内直调工具 → LCD 弹窗 →
  中文回复测试正常 → 短按 BOOT 消除回看板。交付固件（含下述死循环修复），SHA-256
  `f8d0921e625068bc7361e003537334bbc8732a1c353497a795bcc9c8c9e97c45`。
- **新增陷阱（已修复）**：屏显/监控双线程 5s 后台取证若每次打印 540 字节 JSON，
  会刷屏压死串口控制台输入（ask/help 全部无回显）并双线程并发打 Proxy；
  修复：后台取证全静默 + 共享 4s TTL 缓存单锁串行。
- **阈值教训**：演示服务器磁盘基线长期驻留 85.4%，85% 告警阈值会让主动事件永久触发，
  会话（每次 90~300s）挤占深度 16 的消息队列，用户 ask 排不上；事件判定阈值已调 90%。
- **白屏死循环教训（已修复）**：NuttX `/dev/buttons` 的 `read` 是“当前状态快照”而非事件队列，
  每次立即返回全尺寸数据；屏显线程里“循环排空队列”的 while 永不退出，线程空转占满 CPU，
  LCD 永远渲染不到（屏幕常白）且饿死智能体消息处理（触发无响应、整机卡死）。
  对状态快照型驱动每节拍只读一次；另屏显线程栈已提到 64K（内含 TLS/HTTP 取证）。
- 主机侧单测新增弹窗边界用例；磁盘阈值相关夹具同步更新；16+ 组主机测试全绿。
- 构建工具链正确路径（之前曾写错）：编译器 `prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin`，
  esptool `.buildlog/esptool-venv/bin`；pkill 必须锚定 `^python3 docs/tools/<名>` 否则自杀。

## 5. 安全不变式

- LLM 只生成结构化 Incident / Action 建议，不能直接执行 shell。
- 设备端和 Proxy 端都要验证 Action 名、参数、目标和风险等级。
- Guarded 模式中只读诊断可自动运行，可变更 Action 必须实体长按授权。
- 修复后必须执行独立健康检查，不只信任命令退出码。
- 网络、Proxy 或 LLM 失败时安全降级，不执行不确定变更。
- 私钥、密码、API Key、HMAC 密钥和完整敏感日志不得进入 LLM 上下文或普通日志。
- 变更使用唯一请求 ID，避免重试或掉电造成重复执行。

## 6. 开发与提交约定

详见 `DEVELOPMENT_RULES.md`，核心原则是：

1. 只在当前本地基线上开发；未经用户明确同意，不拉取、合并或恢复远端代码。
2. 一次只实现一个可验收功能，先定义验收标准再编码。
3. 功能通过相应的静态检查、编译、单元/集成测试和必要的真机测试后才提交。
4. 每个验收通过的功能对应一个 Git commit，不混入无关改动。
5. 源码分层、高内聚低耦合；硬件、传输、领域和 UI 代码通过明确接口隔离。
6. 注释使用中文，重点解释设计原因、安全边界和硬件限制。

## 7. 后续路线

AI 只读诊断、主动触发、实体批准和设备侧复核已形成可演示闭环；自然语言修复执行闭环已真机验收。

### 当前功能之后的推荐下一步

1. 上电自动联网与自动配置：凭据存 TF 卡（`/dev/mmcsd1`）文本文件，启动时读取并自动
   连 WiFi、下发配置、进 agent（用户已授权读 `.velaops.local.env`）。
2. 根治 WiFi“新鲜窗口”劣化（复位后多次 LLM 调用后链路失速），候选方向：WiFi 省电模式、
   巡检与 LLM 调用的并发节流、mbedtls 会话复用。
3. 实现 Incident tracker 的版本化、原子持久化，避免重启后重放不确定事件。
4. 修复上游 `quit` 后 Provider 丢失的注册表清理问题（当前靠不 quit 规避）。

## 8. 2026-09-16：WiFi 稳定性排查结论与串口传输

### 排查结论

- 通过“看板单独跑 / agent 单独跑 / 两者同跑”的对照实验，确认 ESP32-S3 的
  WiFi 会在以下场景掉线（`reason=8`，之后重连不回来，只能重新上电）：
  1. 复位后看板首个请求与 ai_agent 建链并发（20s 启动延迟可缓解）；
  2. 任何 ~10-20KB 的 LLM 请求（家用路由器同样复现，排除 AP 因素）。
- 应用层做过的缓解均已验证收益有限：请求裁剪、分块发送、全局网络锁、驱动断开
  必重连。结论是 NuttX + esp32s3 WiFi 驱动层问题。
- 期间对上游 `nuttx`/`packages/ai_agent` 的临时改动已全部回退；队伍仓之外只保留
  既有的 `patches/nuttx`、`patches/vendor`。新增 `patches/nuttx/0002`（SDMMC 关闭
  DMA 的 Kconfig）以补齐此前未入 patch 的上游改动。

### 当前交付：串口隧道（有线）

- 设备侧业务 HTTP 全部指向 `127.0.0.1:18080`，由 `velaops tunnel` 帧化经 USB 串口
  发给开发机 `serial_llm_relay.py`，按路径转发到本机 Proxy/LLM 转发器。
- 传输层与业务解耦：凭据 `TRANSPORT=serial|wifi` 一键切换，代码不改。
- 已知遗留：单串口被日志与帧复用，偶发插帧会让 relay 丢帧；未做重发（会触发
  Proxy 的 `replay_detected`）。详见 `docs/SERIAL_TRANSPORT.md`。
- 交接文档：`docs/SERIAL_TRANSPORT.md`（设计/开关/准备/已知问题/切回 WiFi）。

## 9. 2026-09-19：看板增强、CPU 告警、LLM 上屏、实体批准与 WiFi 复测

### 本次交付
- **三页看板重做**（概览/性能/运维）：CPU/MEM/DISK 大数值+仪表、CPU 负载+历史曲线、
  服务/端口/磁盘；数值全整数格式化，避免该路径上的浮点/长整型 printf 风险。
- **CPU 监控**：Proxy 从 `/proc/stat` 差分 + `getloadavg` 采样 `cpu`；
  新增 `top_process`（`ps -eo args=,pcpu=` 取占用最高进程），供 LLM 指出"谁在吃 CPU"。
- **主动告警**：阈值 CPU≥85% / 内存≥80% / 磁盘≥90% / 服务非 active / 端口不可达；
  触发后 LCD 弹闪烁告警框 + **板载 LED（`/dev/userleds`）同步闪烁**；本地摘要带元凶进程
  （如 `stress_cpu.py 96%`）。
- **LLM 上屏**：ai_agent outbound 弱钩子（函数指针注册）把诊断结论映射为短英文写入
  `/tmp/velaops-popup.txt`，看板弹窗更新（如 `CPU HIGH`）；prompt 要求输出 `display`
  字段（≤15 ASCII）。诊断完成信号 `/tmp/velaops-llm-done` 让看板按完成放行。
- **开机体验**：看板在 autoconfig 最开始即启动（不再等认证），显示 `VELAOPS/BOOTING`
  开机画面，未连上时显示 `CONNECTION / SERIAL WAIT RELAY`，连上后进看板；显示设备
  open 重试 + 每 2s 强制重绘自愈花屏。
- **稳定性**：LLM 请求与板端隧道请求共用同一把有界锁（`llm_set_io_lock_hook`）；
  relay 转发移入工作线程 + 丢弃过期 xid 响应 + 90s 快速 504；串口写 EAGAIN 处理；
  auth nonce 增加 40s 有界重传容忍（执行仍按 request_id 幂等）。多次真机：
  告警→诊断→恢复 全程 `invalid_response/transport_error/panic` 全 0。
- **上游 0 改动**：ai_agent 的两处钩子与 ask 文件队列通道全部在 `patches/ai_agent/`，
  由 `apply_nuttx_patches.sh` 编译前注入。

### 实体批准（当前状态：交互通、执行不稳）
- 看板按键线程统一读 `/dev/buttons`，长按 2s 写 `/tmp/velaops-approve`，修复流程据此
  放行；批准弹窗长按达标即消、回看板。
- 但单条半双工隧道下，"重启请求"偶发 `transport_error/no-ack`，Proxy 未执行、有时短暂
  拖累链路（看板显示 `SERIAL LINK DOWN`，复位恢复）。**结论：不换独立 UART/多连接难以
  100% 稳定**，故比赛演示主线调整为"告警→LLM 诊断"，批准仅讲设计。

### WiFi 复测（2026-09-19）
- 已具备同网段条件（板子 `10.93.57.80`、开发机 `10.93.57.90`），且凭据可一键
  `TRANSPORT=wifi` 切换（Proxy/转发器需监听局域网、白名单板子 IP）。
- 实测：**板端 autoconfig 卡在"连接 WiFi"阶段**（日志止于 `TF 已挂载`，无 `传输模式`），
  设备配置未写入 → 看板 `配置加载失败: io_error`、隧道未起。**仍是 WiFi 驱动的坑**，
  与当初放弃 WiFi 的结论一致。已回退 `TRANSPORT=serial` 并恢复有线。
- 注：推送 SD 凭据（`serial_push.py`）时若板端控制台被刷屏，首行可能被冲散，需回读校验。

### 下一步（新会话）
- **按键扩展**：ESP32-S3-EYE 有 6 个功能键（RST 不可配，另 5 个可配：MENU/UP+/DOWN/
  PLAY/BOOT），但 NuttX 现仅注册 `BUTTON_BOOT`。需在板级按钮驱动补上其余按键 GPIO
  （查 esp32-s3-eye 原理图）→ 用 `UP/DOWN` 翻页、`PLAY/MENU` 做批准（解放 BOOT）。
- **LLM 结论分页上屏**：把 LLM 英文回复切成多屏，用 UP/DOWN 翻页，充分展示 AI 作用
  （解释进程/给建议）。Skill 与诊断 prompt 均改为英文（LCD 仅 ASCII），并要求
  `display` 为 ≤15 ASCII 英文短句。
- **Skill 审阅**：`app/hello_app/skills/server-incident-response.md` 需人工过一遍
  （比赛要求形成 Skill）。

## 10. 2026-09-19：英文 LLM 结论分页与 ESP32-S3-EYE 五键注入

- 诊断 prompt 与 `server-incident-response` Skill 已改为英文；JSON 增加 `display`，要求
  仅含可打印 ASCII 且不超过 15 字符。`summary`、`reason` 也要求英文。
- Agent 回发钩子把 summary、root cause、recommended action 写入
  `/tmp/velaops-llm-pages.txt`，看板新增三页 LLM 内容页；告警弹窗仍只在异常或有动作时出现。
- ESP32-S3-EYE 原理图确认 MENU/PLAY/UP+/DOWN 共用 ADC1_CH0 电阻梯（约 2.41/1.98/
  0.82/0.38V），BOOT 保持 GPIO0。新增 `patches/vendor/0002-esp32s3-eye-adc-buttons.patch`，
  开启 ADC1_CH0、注册 `/dev/adc0`，按电压映射五个按钮位；应用层用 UP/DOWN 翻页、
  PLAY/MENU 长按批准，BOOT 不再批准。
- 当前开发机目标构建被 `xtensa-esp32s3-elf-gcc: command not found` 阻断；主机 dashboard
  与 Skill 合约测试通过，尚未进行真机烧录验证。

## 11. 2026-09-19：真机联调——按键保底、LLM 链路两个真 bug

### 11.1 编译/烧录（已打通）
- 开发机重编前需先 `apply_nuttx_patches.sh`；`distclean` 会删除
  `nuttx/arch/xtensa/src/esp32s3/esp-hal-3rdparty` 并重新 clone。GitHub 直连 TLS 经常失败，
  实测可给 git 配置镜像：`git config --global url."https://ghfast.top/https://github.com/".insteadOf "https://github.com/"`
  （GitHub 约 15KB/s → 镜像约 1.1MB/s，含 5 个子模块）。
- HAL 子模块：`git -C esp-hal-3rdparty submodule update --init --depth=1 components/mbedtls/mbedtls
  components/esp_phy/lib components/esp_wifi/lib components/bt/controller/lib_esp32c3_family components/esp_coex/lib`。
- mbedtls 补丁在 HAL 仓库内 `nuttx/patches/components/mbedtls/mbedtls/*.patch`，**必须按 0001…0006 顺序**
  逐个 `git apply`（一次性传多个会失败）。
- 烧录：`esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 460800 --before default-reset --after hard-reset write-flash 0x0 nuttx/nuttx.bin`。

### 11.2 按键：ADC 四键读不到 → 先用 BOOT 保底
- 原理图与官方 BSP 均确认 MENU/PLAY/UP+/DOWN 接 **ADC1_CH0（GPIO1）**，补丁引脚/通道正确；
  但 NuttX `esp32s3_adc.c` 经 `ANIOC_TRIGGER`+`read` 读出**恒为 ~1334mV，按任何键不变**
  （怀疑 S3 ADC 驱动/pad 配置问题）。`board_buttons()` 每次仍会调 ADC，`btn_read` 每次都会
  重新采样，机制本身没问题。
- 决策（用户选定）：**BOOT 保底**——短按翻页（6 页），长按 2 秒批准。保留 vendor 的 ADC 映射
  以便日后修驱动；`patches/vendor/0002` 已补 `#include <sys/ioctl.h>`（原为隐式声明）。
- 应用层：翻页判定的 `else` 分支前进一页；批准条件改为 `VELAOPS_BUTTON_BOOT`；弹窗文案改
  `RESTART DEMO / BOOT 2S`；看板页脚 `BOOT: NEXT PAGE / BOOT: LLM PAGES`。Skill 与
  `test_skill.py` 同步回 "physical BOOT-button approval"。

### 11.3 LLM 链路两个真 bug（重要）
1. **ask 文件通道只读第一行**：`patches/ai_agent/0001` 的 `fgets(line,1024)` 只取 `/tmp/vela-ask.txt`
   的第一行，多行 prompt（Skill+证据）会被整体丢弃。因此必须发**单行**提示，让 Agent 自己按
   Skill 调 `velaops_check_resources` 取证，再回结构化 JSON。旧的多行大提示设计无效。
2. **"play " 子串误触发 NL 快速通道**：`agent_loop.c` 用 `strcasestr(text,"play ")` 匹配"播放音乐"，
   而 `display must…` 含子串 `play `（dis**play **must），会把诊断请求误路由到 `music_search`
   （发 HTTP 失败→回 "Error: HTTP"）。修法：诊断 prompt 里 `display` 后一律不跟空格
   （写 `` `display` `` 或 `display,`）。

### 11.4 看板/暂停健壮性
- Agent 回包非 JSON（模型偶发寒暄/"让我查一下"）时，旧逻辑不写 `/tmp/velaops-llm-done`，
  看板会一直暂停到兜底上限（原 600s）。改为：**任何 cli 回包都先写 done**（让看板立即恢复），
  仅当回包含 `schema_version`+`status` 才解析/弹窗；兜底上限降到 **180s**。
- LLM 诊断完成自动跳到 `LLM SUMMARY` 页（仅当 `/tmp/velaops-llm-pages.txt` 已写出时才跳）；
  告警弹窗仍保留，短按 BOOT 消除后即见该页。

### 11.5 已知遗留
- NuttX ESP32-S3 ADC1_CH0 采样异常，ADC 四键暂不可用（驱动层问题）。
- MiMo 模型输出不稳定：多次返回非 JSON（寒暄/中间态），演示存在不确定性；后续可考虑
  温度=0、重试或换更稳模型。
- 开机初期偶发 `配置加载失败: io_error`（autoconfig 写配置前的瞬时），随后自愈，无碍。
