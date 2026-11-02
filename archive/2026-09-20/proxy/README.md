# VelaOps Proxy

VelaOps Proxy 是 VelaOps Sentinel Plan C 的 Linux 受控执行端。设备通过 HMAC v1
请求调用结构化 Action，Proxy 负责认证、防重放、资源白名单、实体批准校验、幂等
执行和审计。比赛 Demo 使用可信局域网 HTTP；Guarded 生产模式保留 TLS。

## 已实现能力

- 公开 `GET /healthz`；
- 受认证 `POST /v1/auth/check` 和 `POST /v1/actions/execute`；
- 只读 Action：`check_service`、`check_port`、`check_disk`、`check_memory`、`check_resources`、`read_service_log`；
- 变更 Action：`restart_service`，必须携带未过期的 `physical_button` 批准；
- SQLite 持久化 request ID 幂等状态，不重放运行中或失败的变更；
- JSONL 结构化审计，不记录请求参数、命令输出和密钥；
- 非回环 HTTP 监听必须显式设置 `allow_insecure_http: true`；
- 提供普通用户可运行的局域网演示 NTP 服务。

Proxy 不接受 shell 文本。所有 systemd unit、IP/端口和磁盘路径都由本机安全配置映射，设备只能提交别名。

## 本地检查

```bash
make check
```

## 配置和运行

1. 复制 `config.example.json`。设备 JSON 保存至少 32 字节的 ASCII secret；Proxy
   的 `secret_hex` 保存这段 ASCII 的十六进制编码，两端原始字节必须完全一致。
2. 配置文件必须由 Proxy 服务用户拥有，权限为 `0400` 或 `0600`。
3. `storage` 的两个父目录必须存在、归 Proxy 用户所有，且组用户/其他用户不可写。
4. 前台启动：

   ```bash
   PYTHONPATH=/opt/velaops-proxy/src \
     /usr/bin/python3 -m velaops_proxy \
     --config /etc/velaops-proxy/proxy.json
   ```

`config.example.json` 是局域网 Demo 示例，其密钥占位符故意无效，不能直接启动。
完整真机步骤见 `../docs/DEMO_RUNBOOK.md`。生产部署应删除
`allow_insecure_http` 并配置 `tls` 证书与私钥。

队伍 Demo 配置生成器还会生成固定的无特权
`velaops-demo-target.service`。使用 `../docs/tools/manage_demo_target.py start|stop`
管理它；目标只监听 `127.0.0.1:28791`，用于在不停止 Proxy 的情况下演示故障与
白名单修复。它不是生产部署模板。

## systemd 部署

`deploy/velaops-proxy.service` 假定代码位于 `/opt/velaops-proxy`，配置位于 `/etc/velaops-proxy/proxy.json`。需根据实际路径调整 `ExecStart`、`Environment` 和 `ReadWritePaths`。

生产建议使用专用非 root 用户 `velaops-proxy`。如需重启 system 级服务，为每个白名单 unit 在 `deploy/velaops-proxy.sudoers.example` 中增加精确命令，先用 `visudo -cf` 验证后安装。不得放宽为通配 `systemctl *`。

`manager: "user"` 适用于开发机隔离联调，且 `restart_via_sudo` 必须为 `false`。专用 system 服务通常使用 `manager: "system"` 和精确 sudoers。

协议字段和错误码见 `../docs/PROTOCOL_V1.md`。
