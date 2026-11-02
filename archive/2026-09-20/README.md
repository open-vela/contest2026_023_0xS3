# VelaOps Sentinel（维拉哨兵）

VelaOps Sentinel 是一台运行在 ESP32-S3-EYE、openvela 与 `ai_agent` 上的桌面式可信服务器运维 Agent。设备通过受限 Proxy 获取服务器资源证据，由自定义 Skill 和 MiMo 生成结构化诊断；涉及变更时，必须由用户长按实体 BOOT 键批准，执行后再独立复核服务状态。

项目把服务器巡检、AI 分析、物理授权、白名单修复和结果复核收敛到一块常驻桌面的硬件上，面向小团队、个人开发者、家庭实验室和高校实验室。

## 核心能力

- ESP32-S3-EYE 真机运行 openvela 与 `ai_agent`，使用 MiMo 作为 LLM 后端。
- `server-incident-response` Markdown Skill 约束诊断顺序、输出结构和安全边界。
- `velaops_check_resources` 只读工具获取内存、磁盘、服务和端口证据。
- 后台巡检对异常进行连续采样、去抖和去重，主动注入 `opened`/`recovered` 事件。
- `velaops_restart_service` 只允许修复固定白名单服务，并要求 BOOT 长按实体批准。
- 修复后使用新的 request ID 再次取证，不把 Action 返回值直接当作恢复结论。
- 看板巡检延迟到 Agent 网络建链稳定后再开始；掉线时只诊断不改接口，避免打断
  正常链路，`velaops net-check` 可现场复核。
- ST7789 LCD 提供 6 页看板：概览 / 性能 / 运维 + LLM SUMMARY / ROOT CAUSE / ACTION。
  告警弹出闪烁告警框，LLM 诊断完成后**自动跳到 LLM SUMMARY 页**；`BOOT` 短按翻页、
  长按 2 秒批准；`velaops_show_message` 已真机验证显示 `TEST-OK`。
- 默认演示链路为**有线串口隧道**：板端 HTTP 经 USB CDC 交给主机 relay 转发到 Proxy/LLM，
  避开 WiFi 不稳定；SD 凭据卡 `TRANSPORT=serial|wifi` 一键切换。
- MiMo 不可用时可使用本地规则降级，安全输出结构化诊断且不执行变更。

## 系统架构

```text
ESP32-S3-EYE / openvela
  ├─ ai_agent + Markdown Skill
  ├─ VelaOps Tool Provider
  ├─ Incident / Approval / Repair 状态机
  ├─ LCD + BOOT 按键
  └─ HTTP + HMAC v1
              │
              ▼
VelaOps Proxy（认证、防重放、Schema、白名单、审计）
              │
              ▼
Linux 演示服务 / 端口 / 内存 / 磁盘
```

默认演示链路：`板端 HTTP → 串口隧道（USB CDC）→ 主机 relay → Proxy / MiMo 转发器`，
有线稳定、无需 WiFi；按需可切回局域网 HTTP + HMAC。设备密钥、Wi-Fi 密码和 MiMo API Key
仅保存在本地私密配置中，不提交到 Git。

## 目录结构

```text
app/hello_app/        设备端 VelaOps 应用、Tool Provider、LCD 与主机测试
proxy/                HMAC Proxy、白名单 Action、审计与测试
patches/              openvela 公共目录所需的可复现补丁
docs/                 架构、协议、接手说明和唯一 Demo Runbook
contest2026_023_0xS3.xml
                      repo linkfile 映射
```

## 编译与烧录

完整环境和故障恢复步骤以 `docs/DEMO_RUNBOOK.md` 为准。以下命令均在 openvela 工作区执行
（`cd <openvela 工作区>`）：

```bash
bash contest2026_023_0xS3/docs/tools/apply_nuttx_patches.sh

export PATH="$PWD/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH"
export PATH="$PWD/.buildlog/esptool-venv/bin:$PATH"
export CCACHE_DISABLE=1

./build.sh vendor/espressif/boards/esp32s3/esp32s3-eye/configs/openvela

esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 460800 \
  --before default-reset --after hard-reset \
  write-flash 0x0 nuttx/nuttx.bin
```

> `distclean` 会删除 `esp-hal-3rdparty` 并重新 clone；该仓库在 GitHub 上较慢，可先配镜像
> `git config --global url."https://ghfast.top/https://github.com/".insteadOf "https://github.com/"`，
> 再 `submodule update --init --depth=1 components/{mbedtls/mbedtls,esp_phy/lib,esp_wifi/lib,bt/controller/lib_esp32c3_family,esp_coex/lib}`，
> 并在 HAL 内按 `0001…0006` 顺序 `git apply nuttx/patches/components/mbedtls/mbedtls/*.patch`。

## Demo 启动

1. 在项目目录准备本地私密环境文件 `.velaops.local.env`（含 MiMo API Key 等），不要提交。
2. 启动主机侧服务：VelaOps Proxy、MiMo 转发器、局域网 NTP、演示目标
   （`docs/tools/install_demo_services.sh`）。
3. 启动串口 relay：`PORT=/dev/ttyACM0 python3 docs/tools/serial_llm_relay.py`
   （自动探测 `ttyACM*`、断线重连）。
4. SD 凭据卡写入 `TRANSPORT=serial`，给开发板物理上电；板端自动认证、拉起看板并巡检。
5. 触发演示：`python3 docs/tools/stress_cpu.py --seconds 200` 压满主机 CPU →
   看板弹出告警框 + LED 闪烁 → LLM 诊断上屏并自动跳到 LLM SUMMARY 页；停压测后自动恢复。

受限 MiMo 转发器示例：

```bash
set -a; source ./.velaops.local.env; set +a
BIND_HOST=<DEV_HOST_IP> ALLOWED_CLIENT=<BOARD_IP> \
  python3 docs/tools/llm_forwarder.py 28792
```

Debug GUI 的 `TEST-OK` 请求会经过：

```text
GUI → ask 队列 → ai_agent → MiMo → velaops_show_message → LCD
```

## 测试

```bash
make -C proxy check
make -C app/hello_app/tests check
python3 -m py_compile docs/tools/debug_event_gui.py docs/tools/llm_forwarder.py
```

设备端网络、HMAC、资源看板、主动事件、BOOT 批准修复和 MiMo 工具调用均已完成真机验证。详细证据和已知限制见：

- `docs/DEMO_RUNBOOK.md`
- `docs/handover.md`
- `docs/AI_AGENT_TRACK_CHECKLIST.md`
- `docs/PROTOCOL_V1.md`

## 安全边界

- LLM 不能传入主机、shell、服务名或任意 Action。
- Proxy 仅执行编译期/配置期白名单 Action。
- HMAC 签名覆盖方法、路径、时间、nonce 和请求体摘要。
- 可变更操作需要实体按键批准，并具有超时和防重放约束。
- 日志、仓库和示例配置不得包含密码、Token、API Key 或设备 HMAC 密钥。
