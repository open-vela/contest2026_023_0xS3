# 交接提示词（复制给下一个会话）

> 用途：本会话上下文已满。把下面「提示词正文」整段复制给下一个会话作为起点。

---

## 提示词正文

你是 openvela AI 大赛 **ai_agent 赛道**队伍仓项目的开发助手。项目路径：

- 工作区：`/home/lu/桌面/openvela`
- 队伍仓（只改这里）：`/home/lu/桌面/openvela/contest2026_023_0xS3`，分支 `fix/network-stability`
- 构建：`bash contest2026_023_0xS3/docs/tools/apply_nuttx_patches.sh` 然后
  `./build.sh vendor/espressif/boards/esp32s3/esp32s3-eye/configs/openvela`
- 板子：ESP32-S3-EYE，USB CDC 控制台端口会在 `/dev/ttyACM0` 与 `ttyACM1` 间跳变

### 约束（务必遵守）
- **上游 0 改动**：不改 `nuttx/`、`vendor/`、`packages/ai_agent/` 源码；所有上游改动放
  `patches/{nuttx,vendor,ai_agent}/`，由 `apply_nuttx_patches.sh` 编译前注入（可重复执行）。
- 代码注释用中文；**不要主动 git commit**，除非我明确要求。
- LCD 是 5x7 ASCII 字库，**只能显示英文/数字**，中文无效。

### 当前稳定状态（2026-09-19，已提交 9e86b9d 及以前）
- 串口有线（`TRANSPORT=serial`）稳定：认证成功、巡检持续、`invalid_response/transport_error/panic` 基本为 0。
- 三页看板：概览/性能/运维（CPU/MEM/DISK 仪表、CPU 负载+曲线、服务/端口/磁盘）。
- CPU 阈值告警（≥85%）→ LCD 闪烁告警框 + 板载 LED（`/dev/userleds`）闪烁；
  本地摘要含元凶进程（Proxy 新增 `top_process`）。
- **LLM 上屏已打通**：ai_agent outbound 钩子把诊断结论映射成短英文写入
  `/tmp/velaops-popup.txt`，看板弹窗更新（如 `CPU HIGH`）。
- 开机自启 + `VELAOPS/BOOTING` 开机画面 + 连接状态页（`CONNECTION / SERIAL WAIT RELAY`）。
- 性能压测工具：`docs/tools/stress_cpu.py`（压满开发机 CPU 触发告警）。

### 运行环境（开发机侧）
- 四个 user-systemd 服务：`velaops-proxy`(127.0.0.1:28790)、`velaops-llm-forwarder`
  (127.0.0.1:28792)、`velaops-lan-ntp`(40123)、`velaops-demo-target`(127.0.0.1:28791，
  状态页 `docs/tools/demo_target_app.py`，`/status` 返回 PID/uptime/requests)。
- relay 用 `systemd-run --user --unit=velaops-relay --collect -p Restart=always` 常驻：
  `PORT=/dev/ttyACM0 SET_TIME=1 PROXY_HOST=127.0.0.1 PROXY_PORT=28790 \
   FORWARD_HOST=127.0.0.1 FORWARD_PORT=28792 python3 docs/tools/serial_llm_relay.py`。
  relay 自动探测 `ttyACM*` 并断线重连；`RELAY_INJECT_FILE=<file>` 可在开机稳定后注入一条
  NSH 命令（联调用）。
- 板端凭据卡 `/mnt/sd/velaops-credentials.txt`（`TRANSPORT=serial|wifi` 一键切换）。
  用 `docs/tools/serial_push.py <local> <remote>` 下发；推送时板端若刷屏会冲散首行，**必须回读校验**。

### 已知问题（不要再踩）
- **单条半双工隧道**：日志/回显与 base64 帧共用一路 CDC，高突发（如"实体批准修复"）会偶发
  `transport_error/no-ack`。已做：全局互斥、逐行加锁、丢弃过期 xid、90s 快速 504、
  auth 40s 重传容忍。**结论：不换独立 UART/多连接，修复执行难以 100% 稳**，
  故演示主线定为"告警→LLM 诊断"，批准只讲设计。
- **WiFi 不可用**：同网段可达也无用——板端 autoconfig 卡在"连接 WiFi"阶段，配置写不进去，
  看板 `io_error`。别再花时间在 WiFi。
- **实体批准**：按键交互已通（看板线程统一读按键、长按 2s 写 `/tmp/velaops-approve`、
  达标即消弹窗），但重启请求在隧道上不稳。板子有 6 个功能键（RST 不可配，另 5 个：MENU/
  UP+/DOWN/PLAY/BOOT）；**ADC 四键在 NuttX 上读不到（见任务 1），当前仅 BOOT 可用**。
- 重启/复位：esptool 的 `run/chip-id` 会把板子带进 download 模式；用 `write-flash ... --after
  hard-reset` 或**物理重新上电**最可靠。
- **LLM 提示两个坑（必须遵守）**：① ai_agent 文件 ask 通道**只读第一行**（`fgets` 1024），
  诊断 prompt 必须单行；② `display ` 含子串 `play `，会被 NL 快速通道误判成播放音乐 →
  prompt 里 `display` 后不要跟空格。
- **重编前先把 HAL 下全**：`distclean` 会删 `nuttx/arch/xtensa/src/esp32s3/esp-hal-3rdparty`；
  GitHub 直连慢，配镜像 `git config --global url."https://ghfast.top/https://github.com/".insteadOf "https://github.com/"`，
  再 `git submodule update --init --depth=1 components/{mbedtls/mbedtls,esp_phy/lib,esp_wifi/lib,bt/controller/lib_esp32c3_family,esp_coex/lib}`，
  并在 HAL 内按 0001…0006 顺序 `git apply nuttx/patches/components/mbedtls/mbedtls/*.patch`。

### 上次任务完成情况（2026-09-19 本会话，改动未提交）
1. **英文 LLM 结论 + 分页**：已完成。Skill 与 prompt 改英文，JSON 增加 `display`（≤15 ASCII）；
   `velaops_agent_display.c` 写 `/tmp/velaops-llm-pages.txt`（summary/根因/建议三行），
   看板 6 页（3 资源 + 3 LLM），诊断完成后**自动跳到 LLM SUMMARY 页**。
2. **按键**：**未做成 ADC 四键，改 BOOT 保底**。原理图/BSP 确认四键在 ADC1_CH0（GPIO1），
   但 NuttX `esp32s3_adc.c` 读出恒为 ~1334mV、按键无变化（驱动问题）。现 `BOOT` 短按翻页、
   长按 2s 批准。vendor 补丁保留 ADC 映射待修。
3. **Skill 审阅**：Skill 已英文化，待你人工审阅。
4. 文档已更新（PROJECT_MEMORY 第 11 节、DEMO_RUNBOOK 第 9 节）。

### 本次要做的任务（按优先级）
1. **修 NuttX ESP32-S3 ADC 驱动**（若还想要四键）：让 ADC1_CH0/GPIO1 真正采样到电阻梯电压，
   然后恢复 UP/DOWN 翻页、PLAY/MENU 批准。排查方向：`esp32s3_configgpio(1, INPUT|FUNCTION_1)`
   的 pad 模拟配置、SAR 通道 mux、`ANIOC_TRIGGER`→`read` 时序；可用 `board_adc_button()`
   打印原始 `am_data` 对照 2.41/1.98/0.82/0.38V 四档。
2. **降 LLM 输出不确定性**：MiMo 多次返回非 JSON（寒暄/“让我查一下”）。可考虑温度=0、
   非 JSON 自动重试 1 次、或换更稳模型；回包非 JSON 目前已有兜底（看板立即恢复 + 180s 上限）。
3. **Skill 审阅 + 提交**：Skill 内容请我过一遍；改动按需 commit。
4. 端到端复演一次「CPU 压满 → 弹窗+LED → LLM 诊断 → 自动跳 LLM 页 → 恢复」。

### 关键文件
- 看板渲染：`app/hello_app/src/velaops_dashboard.c`（三页 + 弹窗渲染）
- 显示状态/接口：`app/hello_app/include/velaops_display.h`、`src/velaops_display.c`
- 主任务/看板/告警/LLM 入队/批准：`app/hello_app/velaops_main.c`
- 事件判定：`app/hello_app/src/velaops_resource_incident.c`、`velaops_local_diagnosis.c`
- 串口隧道：`app/hello_app/src/velaops_serial_tunnel.c`
- Agent 回发→LCD 桥接：`app/hello_app/src/velaops_agent_display.c`（钩子注册见
  `src/velaops_agent_tools.c`，上游钩子补丁在 `patches/ai_agent/`）
- 开发机 relay：`docs/tools/serial_llm_relay.py`
- 上游补丁：`patches/ai_agent/`、`patches/nuttx/`、`patches/vendor/`
- 交接文档：`docs/SERIAL_TRANSPORT.md`、`docs/PROJECT_MEMORY.md`（第 8、9 节）
- 压测：`docs/tools/stress_cpu.py`

### Skill 输出契约（需转英文）
```json
{"schema_version":1,"status":"normal|warning|critical|unknown",
 "summary":"<english short>",
 "evidence":[{"metric":"field","value":"observed","reason":"<english>"}],
 "root_cause_candidates":[],
 "recommended_action":{"action":"none|restart_service|retry_check","target":"alias or empty",
   "risk":"read_only|change","requires_physical_approval":true},
 "confidence":0.0,
 "display":"<==15 printable ASCII, e.g. CPU 99 PYTHON / RESTART PROXY / DISK FULL>"}
```

先只读代码与文档确认现状，再动手；每步小改并真机验证。
