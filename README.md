# VelaOps Sentinel（维拉哨兵）

VelaOps 是运行在 ESP32-S3（N16R8）与 openvela 上的桌面式服务器哨兵。设备通过板载 SSH 拉取整机指标，计算健康分并在 ILI9341 320×240 屏幕上展示；异常时本地告警，并由 MiMo 生成结构化处置动作，经白名单/黑名单校验和实体按键确认后执行、复核。

## 当前业务能力

- 板载 libssh2 SSH：一次 exec 拉取 CPU、内存、磁盘、负载、服务、端口、网卡、温度、OOM、僵尸进程等指标。
- 整机健康分：可用性、资源、服务、网络、稳定性五维加权，100 分制，端口基线漂移检测。
- 6 页看板：概览、性能、运维、总结、根因、处置。
- 四键交互：UP / DOWN 翻页，BACK 静音/取消，长按 OK 批准处置。
- AI 结构化动作：none / kill / restart / start / stop / exec，带 target、command、reason；本地白名单/黑名单兜底。
- 可靠性：DNS/IP 缓存、SSH 重试、主循环看门狗、告警动作锁存。

## 仓库结构

```text
app/velaops/                 # VelaOps 业务代码，manifest 映射到 apps/examples/velaops
logs/                        # AI Coding 日志，原样保留
archive/2026-09-20/          # 旧版 ai_agent + Proxy 实现、旧文档与补丁
contest2026_023_0xS3.xml     # repo linkfile 映射
```

## 说明

本仓本次提交的是业务代码层。板级适配（ILI9341、四键、LED/蜂鸣器、MTD、配网）和公共仓补丁仍在开发分支，尚未并入本仓；构建时需要对应的板级支持。

## 安全边界

- 默认不自动执行，所有处置需长按 OK 确认；exec 先展示命令。
- 黑名单拒绝 `rm -rf /`、`mkfs`、`dd 写盘`、`shutdown` 等危险命令。
- AI 超时或不可用时回退本地规则告警，不影响本地看板与按键。
