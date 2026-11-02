# VelaOps 设备端应用

本目录通过软链接映射到 openvela
`packages/demos/contest2026_023_hello_app`，固件命令名为 `velaops`。

当前分层：

- `velaops_protocol`：HMAC v1 字段校验、规范串和签名。
- `velaops_proxy_client`：签名请求编排和 Proxy v1 响应解析。
- `velaops_proxy_http_transport`：局域网 HTTP socket 薄适配。
- `velaops_device_config`：加载设备私密配置。
- `velaops_agent_tools`：向 ai_agent 注册只读资源取证 Provider。
- `velaops_agent_monitor`：去抖主动事件并安全注入 Agent 消息总线。
- `velaops_approval`：与平台无关的连续长按、松开重置和超时状态机。
- `velaops_button_approval`：通过 `/dev/buttons` 等待 BOOT 实体批准。
- `velaops_guarded_repair`：固定修复请求、执行状态和独立复核规则。
- `velaops_local_diagnosis`：LLM 不可用时的确定性结构化诊断。
- `tests/`：电脑上运行的固定向量、Client 和传输边界测试。

运行主机测试：

```bash
make -C app/hello_app/tests check
```

## AI Agent 只读巡检集成

`velaops agent-install` 通过 ai_agent 的外部 Tool Provider 接口注册
`velaops_check_resources`，不修改 `packages/ai_agent` 上游源码。该工具只能
执行编译期固定的 `check_resources` Action，不接受目标、Action 名或
服务器参数。

首次成功调用该工具后，生命周期适配器会启动单个 5 秒后台巡检。连续两次异常只
注入一次 `opened`，持续异常去重；连续两次正常注入一次 `recovered`。事件提示只
包含本地可信的类型和代次，不拼接 Proxy 证据，并要求 Agent 重新读取精确 Skill、
调用固定只读工具取证。这样领域状态机不依赖 ai_agent 的内部生命周期。

Provider 还暴露无参数的 `velaops_restart_service`。它只允许固定的 `demo` 服务，
仅用于用户明确要求执行最近一次 critical 建议；设备必须在 30 秒内检测到 BOOT
连续长按 2 秒，随后生成 60 秒有效的硬件随机批准 ID。变更完成后最多 5 次重新调用
`check_resources`，不能只信任 systemd 返回值。无需 LLM 时可用同一实现验收：

```text
velaops repair-demo
```

烧录或断电后，先在 NSH 提示符下安装队伍 Skill 并注册工具，
再启动 `ai_agent`：

```bash
bash docs/tools/provision_ai_agent_assets.sh
```

当前 ESP32-S3-EYE defconfig 的 `CONFIG_EXAMPLES_AI_AGENT_VELA_DATA_DIR`
为 `/data/ai_agent`，因此真机 Skill 路径是
`/data/ai_agent/skills/server-incident-response.md`。官方赛道文档中的
`/data/agent/skills/` 是通用示例，部署时必须以当前 defconfig 与启动
日志为准。

Skill 标题必须与文件名一致，且标题下一行直接写路由摘要。当前上游加载器会在
标题后的首个空行停止提取描述；按常规 Markdown 在两者之间留空行会导致 Agent
只看见技能名。该兼容约束由 `tests/test_skill.py` 固化。

配对密钥和 Proxy 地址属于本地私密配置，不得写入 Git。
设备端固定读取：

- `/data/velaops/config.json`：`schema_version`、`host`、`port`、`device_id`、`secret`。

配置完成后执行 `velaops auth-check` 验证 HTTP、HMAC 和设备身份。完整启动步骤见
`../../docs/DEMO_RUNBOOK.md`。

## 本地规则降级

MiMo 或公网不可用时执行：

```text
velaops diagnose-local
```

该命令仍从 Proxy 取得同一份 HMAC 保护的 `check_resources` 证据，但不调用 LLM。
输出字段与 Agent Skill 合约一致，`summary` 明确标记“本地规则降级”。服务或端口
故障为 `critical`，内存达到 80% 或磁盘达到 85% 为 `warning`；取证失败输出
`unknown + retry_check`。模块只生成建议，不执行 Action。

## 服务器资源监控屏

ESP32-S3-EYE 已提供 240×240 ST7789 LCD（设备节点为 `/dev/lcd0`）和 BOOT 按键设备。执行：

```text
velaops monitor
```

设备每 5 秒通过 HTTP + HMAC 通路读取 Proxy 的 `check_resources` 结果，并在
`/dev/lcd0` 上显示服务器真实资源摘要。BOOT 短按切换三个页面：

1. 内存使用率、已用/可用内存和在线状态；
2. 白名单服务状态、端口可达性和 Proxy 状态；
3. 磁盘使用率、端口延迟和只读提示。

监控失败时屏幕显示 `OFFLINE` 或 `PROXY FAIL`，不会执行任何变更 Action。

同一看板循环还维护本地资源 Incident：连续两次 `warning`/`critical` 才输出一次
`proactive_event opened`，持续异常不重复；连续两次 `normal` 后输出
`recovered`。网络或坏证据不会改变 Incident 状态。Agent 主动巡检使用前述独立
适配器，不依赖是否启动 LCD 看板。
