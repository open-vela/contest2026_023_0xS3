# SSH 采集、健康评分与 AI 处置

## 板载 SSH / libssh2

- 移植位置：`apps/netutils/libssh2`，mbedTLS 后端；`apps/crypto/mbedtls/Make.defs` 增加编译接入。
- 使用密码认证；`velaops_ssh.c` 负责会话、执行、重试、解析。
- SSH/TLS 使用固定 256KB 池（`velaops_pool.c`），避免和 UI/按键抢内部 RAM。
- 握手/执行超时 30s，失败重试 3 次；高负载下握手可能返回 -9/-13，重试是必要的。
- 板端不要用 `system()` 或 shell 拼接不可信输入；命令走受限构造。

## 采集脚本

一次 SSH exec 拉回整机指标，输出 `KEY=VALUE`，减少握手次数。采集项：

- CPU 使用率（两次采样差值）、内存/Swap、磁盘/inode、load、核数
- 失败 systemd unit、活跃 unit 数、监听端口
- ESTAB / TIME_WAIT、网卡 errors/drops
- 温度、TOP 进程、NTP 状态、OOM、僵尸进程、时钟
- 端口基线：首次采集学习，后续做漂移检测

TOP 过滤必须排除工具进程本身：`ps/sh/awk/sort/head/cut/sed/grep/tr/wc/cat/df/sleep`，否则压测时会误判成业务进程。

## 健康评分

五维加权，总分 100：

| 维度 | 权重 |
|---|---|
| 可用性 | 25 |
| 资源 | 30 |
| 服务 | 25 |
| 网络 | 10 |
| 稳定性 | 10 |

- 关键项异常时总分封顶 40。
- 告警：分数 <70 连续 2 个周期；恢复：分数 ≥80。
- 阈值、告警/恢复分数、忽略端口/单元、kill 白名单都在配置里，不要硬编码。
- 评分逻辑在 `velaops_health.c`，UI 只读 state。

## AI 返回与动作

MiMo 走 OpenAI 兼容 HTTPS，返回单行 JSON：

```json
{"action":"none|kill|restart|start|stop|exec","target":"...","command":"...","reason":"..."}
```

- 动作仅允许 `none/kill/restart/start/stop/exec`。
- `none`：只给建议，不执行。
- `kill/restart/start/stop`：target 必须在配置白名单/修复单元内。
- `exec`：允许任意命令，但必须先展示命令并人工确认；本地黑名单兜底。
- 命令执行统一包 `timeout 20 sh -c ...`，避免挂死。
- 执行后必须独立复核（进程消失/单元 active/端口恢复），不能把命令返回码当结论。

## 安全边界

- 黑名单至少包含：根目录 `rm -rf /`、`mkfs`、`dd of=/dev/...`、`shutdown/reboot/halt`、fork bomb。
- 白名单：kill 目标、可重启/修复的 unit 必须在配置内。
- 默认不自动执行；所有变更都要长按 OK 确认；exec 先看命令。
- AI 失败/超时不能阻塞本地看板、按键和本地规则告警。

## 可靠性设计

- LLM socket 超时 8s；AI 调用放在 5s 交互窗口之后，避免按键被网络拖住。
- DNS 默认阿里 223.5.5.5；DNS 成功后缓存 `llm_ip`，后续跳过 DNS。
- 主循环 watchdog：45s 无进展 `boardctl(BOARDIOC_RESET)`。
- 告警动作锁存：TOP 进程变化不能让已生成的处置动作消失。
- 告警开始时清 `g_ai_valid`；恢复后不要继续显示旧 AI 结论。
- 告警页长按 OK 一次直接执行；其他页弹确认框；BACK 取消/静音。
- 配置键包括：阈值、告警/恢复分数、忽略端口/单元、kill_allow、llm_ip、DNS、静态 IP。

## 配网与持久化

- `/data` 使用 LittleFS；配置在 `/data` 下，掉电保留。
- 配置键包含 Wi-Fi、DNS、llm_ip、阈值、模型地址等；新增键要提供默认值并兼容旧配置。
- SoftAP 配网页的端口/表单要和配置解析保持一致；配网失败时用 CH343 console 看日志，不要先怪路由器。
- 已知历史问题：手机可关联 SoftAP 但数据面不通（2026-10-25 未最终解决）；改动前先复现并确认当前状态。

## 验证清单

1. 板端能 ping/解析 DNS，能建立 SSH。
2. 一次采集能拿回全部 KEY；解析无空指针/越界。
3. 压满 CPU 后 2 个周期内告警，蜂鸣器/LED/屏幕一致。
4. 长按 OK 能批准；AI 返回 JSON 能显示 action/target/reason/command。
5. kill 后进程消失，restart 后 unit active，exec 有输出。
6. 停压测后能恢复；45s 主循环卡死能复位。
7. 只插供电、不接串口 relay 时，翻页/静音/本地告警仍可用。
