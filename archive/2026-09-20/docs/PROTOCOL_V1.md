# VelaOps Proxy API v1 协议

## 1. 传输约束

- 协议版本：`1`。
- 内容类型：`application/json; charset=utf-8`。
- 请求体最大长度：P0 默认 16 KiB，HTTP 层必须在解析 JSON 前限制。
- 时间戳：UTC Unix 秒。
- `request_id` 与 `nonce`：各 16 字节随机数的小写十六进制，共 32 字符。
- 设备密钥：至少 32 个随机字节，每台设备独立配置并可单独吊销。
- 签名：HMAC-SHA256，小写十六进制。
- 非回环地址监听必须使用 TLS，服务端最低允许 TLS 1.2；HMAC 不替代传输加密。

## 2. 认证请求头

```text
X-VelaOps-Version: 1
X-VelaOps-Device: <device_id>
X-VelaOps-Request-ID: <32 lowercase hex>
X-VelaOps-Timestamp: <unix seconds>
X-VelaOps-Nonce: <32 lowercase hex>
X-VelaOps-Signature: <64 lowercase hex>
```

`device_id` 长度不超过 64，只允许 ASCII 字母、数字、点、下划线和连字符，且首字符必须是字母或数字。

## 3. 规范化签名串

九个字段按下列顺序使用单个 LF（`0x0a`）连接，末尾不添加换行：

```text
VELAOPS-HMAC-SHA256
1
<device_id>
<request_id>
<timestamp>
<nonce>
<HTTP_METHOD>
<path_and_query>
<body_sha256_hex>
```

完整顺序为：

1. 固定算法名 `VELAOPS-HMAC-SHA256`；
2. 协议版本；
3. 设备 ID；
4. 请求 ID；
5. Unix 时间戳；
6. nonce；
7. 大写 HTTP 方法；
8. 原始请求目标，即路径和查询串；
9. 实际传输请求体的 SHA-256 小写十六进制摘要。

签名计算：

```text
hex_lower(HMAC-SHA256(device_secret, canonical_request_bytes))
```

签名绑定实际发送的请求体字节，不要求不同语言实现 JSON canonicalization。设备生成 JSON 后不得在签名与发送之间重新格式化。

## 4. 验证顺序

Proxy 在读取和执行 Action 前按顺序完成：

1. 限制请求头和请求体大小；
2. 校验协议版本与字段格式；
3. 查找 `device_id` 对应密钥；
4. 使用常数时间比较校验签名；
5. 校验时间窗；
6. 原子登记 `(device_id, nonce)`，拒绝重放；
7. 解析 JSON schema；
8. 执行 Policy 与 Action 参数校验。

认证失败不返回设备是否存在等细节，详细原因只进入受保护的 Proxy 审计日志。

## 5. API 与 Action schema

### 5.1 接口

- `GET /healthz`：不认证，只返回 Proxy 进程健康状态。
- `POST /v1/auth/check`：需认证，请求体必须为 `{}`。
- `POST /v1/actions/execute`：需认证，执行下述结构化 Action。

只读请求示例：

```json
{
  "schema_version": 1,
  "action": "check_service",
  "target": "local-dev",
  "parameters": {"service": "web"}
}
```

变更请求必须额外携带已由 HMAC 签名覆盖的实体批准：

```json
{
  "schema_version": 1,
  "action": "restart_service",
  "target": "local-dev",
  "parameters": {"service": "web"},
  "approval": {
    "approval_id": "0123456789abcdef0123456789abcdef",
    "approved_at": 1787582400,
    "expires_at": 1787582460,
    "source": "physical_button"
  }
}
```

`approval_id` 是 16 字节随机数的小写十六进制。批准有效期最长 120 秒，且同一设备的一个 `approval_id` 最多启动一个新变更。

### 5.2 P0 Action 参数

| Action | 风险 | parameters |
|---|---|---|
| `check_service` | 只读 | `{"service":"<service_alias>"}` |
| `check_port` | 只读 | `{"port":"<port_alias>"}` |
| `check_disk` | 只读 | `{"disk":"<disk_alias>"}` |
| `check_memory` | 只读 | `{}` |
| `read_service_log` | 只读 | `{"service":"<service_alias>","lines":1..200}` |
| `restart_service` | 变更 | `{"service":"<service_alias>"}` + `approval` |

别名必须存在于 Proxy 本地安全配置。请求不得直接传 unit、路径、IP 或命令。未知字段、重复 JSON 字段、非有限数值和过深嵌套都会被拒绝。

### 5.3 幂等与响应

变更使用 `(device_id, request_id)` 持久化幂等。相同请求已成功时只返回缓存结果；运行中、失败或掉电后状态不确定时不自动重放。Action 业务失败的 `retryable` 为 `false`。

成功响应：

```json
{"schema_version":1,"request_id":"<request_id>","ok":true,"result":{}}
```

失败响应：

```json
{"schema_version":1,"request_id":"<request_id>","ok":false,"error":{"code":"<code>","message":"<safe_message>","retryable":false}}
```

## 6. 稳定错误码

- `invalid_request`
- `not_found`
- `method_not_allowed`
- `payload_too_large`
- `unsupported_media_type`
- `unsupported_version`
- `invalid_auth`
- `signature_mismatch`
- `stale_timestamp`
- `replay_detected`
- `unknown_device`
- `action_not_allowed`
- `invalid_action_parameters`
- `approval_required`
- `approval_expired`
- `approval_reused`
- `idempotency_conflict`
- `action_state_uncertain`
- `action_verification_failed`
- `execution_timeout`
- `internal_error`

对未认证客户端，HTTP 响应应统一使用 `invalid_auth`；更细错误码只供可信审计或已认证后的业务响应使用。

## 7. 固定测试向量

跨语言实现必须使用 `proxy/tests/vectors/hmac_v1.json` 验证。向量中的密钥仅用于测试，不得用于设备或部署环境。
