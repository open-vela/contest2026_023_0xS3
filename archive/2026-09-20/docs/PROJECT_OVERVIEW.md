# VelaOps Sentinel（维拉哨兵）项目方案

> 版本：v1.3（2026-08-26）
>
> 本文只描述当前 Plan C 产品方案。板端直连 SSH 是历史原型，不再是 P0 主架构。

## 1. 一句话介绍

VelaOps Sentinel 是一台基于 ESP32-S3-EYE、openvela 和 ai_agent 的桌面式可信服务器运维 Agent：它主动发现异常、生成结构化处置建议，经用户实体按键授权后执行受控修复，并独立验证服务是否恢复。

## 2. 产品价值

小团队、个人开发者和实验室往往没有完整的 7×24 小时运维平台。传统故障处理需要收到告警、打开电脑、登录服务器、收集日志、判断原因、执行修复再复核。

本项目将这条链路收敛到一台常驻桌面的专用设备：

- 始终可见，不依赖用户打开 App；
- 本地规则先筛选，只在异常或用户询问时调用 LLM；
- 实体按键构成敏感操作的“人在回路”授权；
- 使用独立、受限、可撤销的设备身份，不把生产权限暴露给普通聊天入口。

## 3. 目标用户与 P0 场景

目标用户包括小型开发团队、个人开发者、家庭实验室、高校实验室和边缘计算站点维护人员。

P0 演示闭环：

1. 设备定时通过 Proxy 检查 nginx 或 Docker 服务。
2. 本地规则在连续多次异常后创建 Incident，避免瞬时抖动误报。
3. Proxy 收集服务状态、端口、磁盘、内存和有限日志证据。
4. ai_agent / LLM 输出结构化故障摘要、根因候选、风险和建议 Action。
5. LCD 显示服务器、故障、Action 和授权倒计时。
6. 用户长按 BOOT 键批准可变更 Action。
7. 设备携带唯一请求 ID 和 HMAC 签名调用 Proxy。
8. Proxy 执行白名单 Action，设备再做独立健康检查。
9. 设备持久化 Incident 和审计事件，LCD / LED 恢复健康状态。

## 4. 双模式设计

### 4.1 Guarded 生产安全模式

- 面向生产服务器、数据库和重要 NAS；
- LLM 只能选择预注册的结构化 Action，不能提交任意 shell；
- 只读诊断可自动执行，可逆变更需实体长按确认；
- 删除、格式化、账户权限、SSH 配置等破坏性操作默认禁止；
- Proxy 使用非 root 专用账户、wrapper 和 sudoers 白名单；
- 每次操作都有超时、输出上限、执行后复核和审计。

P0 优先 Action：

- `check_service`
- `check_port`
- `check_disk`
- `check_memory`
- `read_service_log`
- `restart_service`
- `restart_container`
- `run_healthcheck`

### 4.2 Lab 家庭/开发模式

- 面向个人开发机、家庭服务器和测试环境；
- 允许生成多步诊断与修复计划，但必须先展示命令、目录、diff 和风险；
- 默认限定非 root 账户和工作目录；
- `sudo`、系统包管理、服务、网络、删除和覆盖操作提高授权等级；
- 优先使用容器、虚拟环境、临时目录或 Git 工作树，失败时尽可能回滚；
- 限时高权限会话到期、切换服务器或设备移动后自动锁定。

Lab 是 P1 能力，不进入当前 P0 最小闭环。

## 5. 系统架构

```text
┌────────────── ESP32-S3-EYE / openvela ──────────────┐
│ LCD / Button / LED / SD / Accelerometer             │
│                    │                                │
│              Product State Machine                 │
│                    │                                │
│       Incident Manager ─ Policy / Approval Engine  │
│             │                    │                  │
│      Local Rules / cron       Audit Store          │
│             │                    │                  │
│        ai_agent / Skills      Proxy API Client     │
│             │                    │                  │
│          Cloud LLM          HMAC / Retry / Timeout │
└─────────────────────────────────┬───────────────────┘
                                  │ Demo: HTTP + HMAC / LAN
                                  ▼
┌─────────────────── VelaOps Proxy ───────────────────┐
│ Auth → Schema Validation → Policy → Action Registry │
│                                │                    │
│                  Local Executor (P0) / SSH (P1)     │
│                                │                    │
│                     Redaction / Audit / Response    │
└─────────────────────────────────┬───────────────────┘
                                  ▼
                    Linux Server / NAS / Dev Machine
```

### 5.1 分层边界

- **Platform**：LCD、LED、按键、SD、网络、时钟和设备节点封装。
- **Transport**：HTTP/TLS、HMAC、重试、超时和错误分类，不包含业务规则。
- **Domain**：ServerProfile、HealthSnapshot、Incident、ActionRequest、AuditEvent 和状态机。
- **Policy**：模式、风险、白名单、授权有效期和幂等约束。
- **Agent**：证据裁剪、脱敏、Skill、LLM 调用与结构化结果校验。
- **Application**：编排巡检、诊断、授权、执行、复核和审计用例。
- **Presentation**：LCD / LED / CLI 只消费应用层状态，不直接执行网络或运维 Action。

依赖方向从外层指向领域接口。Domain 不依赖 LCD、HTTP、SSH 或 LLM 的具体实现。

当前比赛 Demo 明确采用可信局域网 HTTP + HMAC，TLS 不作为演示前置条件；Guarded
生产模式再启用 TLS。该取舍不改变 Transport 接口或上层领域模型。

## 6. 核心状态机

```text
HEALTHY
   ↓ 连续异常
SUSPECTED
   ↓ 收集证据
DIAGNOSING
   ↓ 生成建议
WAIT_APPROVAL
   ├── 超时/拒绝 ──→ ESCALATED
   ↓ 用户批准
EXECUTING
   ↓ 执行结束
VERIFYING
   ├── 恢复 ──→ RECOVERED ──→ HEALTHY
   └── 失败 ──→ ESCALATED
```

关键不变式：

- Incident 和 Action 状态可持久化；
- Action 使用唯一 ID，同一 ID 不得重复执行；
- 只有处于 `WAIT_APPROVAL` 且授权未过期时才能执行变更；
- `EXECUTING` 中异常重启不得盲目重放，应转人工复核；
- 恢复结论必须来自独立健康检查。

## 7. 核心数据模型

- **ServerProfile**：服务器 ID、别名、Proxy 地址、模式、角色、允许 Action/服务/容器/目录、巡检周期和阈值。
- **HealthSnapshot**：采集时间、服务、端口、磁盘、负载、内存、结果码和裁剪后证据。
- **Incident**：ID、状态、严重度、摘要、证据、根因候选、建议 Action 和时间戳。
- **ActionRequest**：Action 名、结构化参数、风险、批准要求、唯一 ID、有效期、前置条件和复核方式。
- **AuditEvent**：时间、服务器、Incident、事件类型、批准来源、Action、结果和脱敏错误。

## 8. Proxy API 安全边界

### 8.1 认证与防重放

- 每台设备使用独立、可撤销的密钥和 `device_id`；
- 签名覆盖 HTTP 方法、路径、时间戳、nonce 和请求体摘要；
- Proxy 检查有效时间窗和 nonce 唯一性；
- 时间未同步时暂停依赖绝对时间的变更 Action；
- HMAC 比较使用常数时间实现。

### 8.2 Action 执行约束

- API 只接受版本化 schema 中注册的 Action；
- 参数必须做类型、长度、枚举和资产归属校验；
- 不允许把 LLM 文本拼接到 shell 命令；
- 每个 Action 有超时、输出上限和明确错误码；
- Proxy 与设备端都记录脱敏审计，并通过 request ID 对账。

## 9. AI Agent 边界

- 服务器输出是不可信输入，可能包含 Prompt Injection；
- 发送 LLM 前必须脱敏、限长并标注证据类型；
- LLM 只输出符合 schema 的 Incident / Action 建议；
- 非法 JSON、超时或 LLM 不可用时安全失败，不执行变更；
- 本地规则和固定 Runbook 在 LLM 不可用时仍完成基础巡检与告警；
- P0 自定义 Skill 为 `server-incident-response`。

## 10. 硬件交互

- **LCD**：服务器健康、Incident、风险、Action、倒计时和复核结果。
- **BOOT 键**：短按查看详情，长按批准；运动手势不能批准服务器变更。
- **LED**：绿色健康，黄色警告/待确认，红色严重事故/执行失败。
- **microSD**：扩展审计和历史数据，不明文保存长期密钥。
- **QMA7981**：P1 用于离位锁定、跌落保护和静音，不承担授权。
- **麦克风/摄像头**：不进入 P0；后续用于只读查询、事故播报或一次性配对。

## 11. P0 验收标准

P0 只做 Guarded 单服务器闭环，不同时扩展多服务器、语音、摄像头或 Lab 自由执行。

- ESP32-S3-EYE 稳定运行 openvela + ai_agent；
- 设备和 Proxy 能拒绝错误签名、过期请求和 nonce 重放；
- 主动发现至少两类异常；
- LLM 返回结构化 Incident，失败时有本地降级；
- 未批准的变更不得执行；
- 长按批准后完成一次白名单修复；
- 修复后完成独立复核；
- 相同故障无重复执行或告警风暴；
- 掉电重启不重放不确定变更；
- 日志和 LLM 请求不出现密钥、密码或 API Key；
- 5 分钟内稳定展示“异常 → 分析 → 授权 → 修复 → 复核”闭环。

## 12. 开发顺序

以最小可验收垂直切片推进，每项通过测试后独立提交：

1. 固化 ai_agent、Wi-Fi、TLS、LCD、按键、LED 和存储基线。
2. 定义版本化 Proxy API schema、错误模型和测试向量。
3. 实现 Proxy 认证、防重放和第一个只读 Action。
4. 实现设备端 API Client，打通签名请求和结构化响应。
5. 实现 HealthSnapshot、本地阈值和 Incident 去重状态机。
6. 接入 ai_agent 与 `server-incident-response` Skill，完成结构化分析和本地降级。
7. 实现 Policy / Approval Engine、BOOT 长按授权和超时拒绝。
8. 实现第一个可变更 Action，完成幂等、执行后复核和失败升级。
9. 实现 LCD / LED 产品状态显示和 SD 审计持久化。
10. 完成断网、Proxy/LLM 故障、掉电和 5 分钟演示回归。

当前本地重建已验收第 1～5 项中的 Proxy 协议、认证、首个只读 Action、设备端真机通路、HealthSnapshot 规则与 Incident 去抖状态机。第 1 项的存储基线仍需按当前代码重新验收，不沿用旧环境结论。下一切片是 Incident 原子持久化与定时巡检用例。

详细工程门禁见 `DEVELOPMENT_RULES.md`。
