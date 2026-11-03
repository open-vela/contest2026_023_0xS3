---
name: velaops-openvela-dev
description: Develop, debug, build, flash, and submit VelaOps on openvela/NuttX for the custom ESP32-S3 N16R8 board (ESP32-S3-EYE configuration). Use when working on VelaOps UI, ILI9341 display, four-key handling, board GPIO/LED/buzzer, WiFi stability, libssh2 SSH collection, health scoring, MiMo structured actions, provisioning, watchdog recovery, flashing, or the 0xS3 contest repository and pull requests.
---

# VelaOps / openvela ESP32-S3 开发

## Overview

VelaOps（维拉哨兵）是运行在 ESP32-S3 N16R8 + openvela/NuttX 上的服务器哨兵：板载 SSH 拉取整机指标，计算健康分，在 ILI9341 320×240 屏幕上展示，并通过 MiMo 生成结构化处置动作，经白名单/黑名单和实体按键确认后执行、复核。

这个 Skill 记录项目的硬约束、构建/烧录流程、常见改动的落点，以及已经踩过的坑。**先读本文件，再按任务加载对应 reference。**

## 代码与分支地图

构建树在开发机 `/home/lingview/openvela/`：

| 路径 | 内容 |
|---|---|
| `nuttx/` | 内核、ESP32-S3 板级、ILI9341 驱动、WiFi、spiflash |
| `apps/examples/velaops/` | VelaOps 业务代码（UI、SSH、LLM、配置、健康分、配网） |
| `apps/netutils/libssh2/` | libssh2 移植（mbedTLS 后端） |
| `contest2026_023_0xS3.clone/` | GitHub 队伍仓本地克隆 |

GitHub 基线：`open-vela/nuttx`、`open-vela/nuttx-apps` 的 `dev-ai-contest-2026`。本地开发分支为 `contest/pr-velaops`，备份分支为 `backup/gitee-*-20261102`。

本地工作副本（Windows）`C:\Users\lingview\Desktop\openvela\` 下的 `velaops_*.c/.h` 是同步用的源码副本，最终以开发机 `/home/lingview/openvela/apps/examples/velaops/` 为准。

## 硬规则（违反会直接坏功能）

1. **不要删除 `arch/xtensa/src/esp32s3/esp32s3_start.c` 里启动期的 `mov sp`。** 删掉会让 ai_agent 在早期帧里写到错误栈，表现为 PC=0/EXCM=1 双异常；这是上游必需代码。
2. **四键、LED、蜂鸣器必须板级直读/直写 GPIO，不要注册新的 `/dev/gpio` 设备。** 额外 gpio 设备会干扰 WiFi EAPOL，导致四次握手超时。
3. **WiFi 内部内存必须独立。** 应用大缓冲耗尽内部 DRAM 后，WiFi 驱动的内部内存分配落到 PSRAM 会被拒绝，`esp_wifi_internal_tx` 永久返回 `ESP_ERR_NO_MEM`。保留 `CONFIG_XTENSA_IMEM_USE_SEPARATE_HEAP`（128KB），任务栈继续用普通堆。
4. **AI 处置默认不自动执行。** 任何 kill/restart/start/stop/exec 都要白名单/黑名单校验 + 长按 OK 确认；exec 先展示命令。
5. **中文字库是 GB2312 全量 7445 字符。** ASCII 必须和 CJK 共用基线：大写 7 行、小写 x-height 5 行；不要退回 5×5 大写表。
6. **比赛只在 GitHub 提交。** 主作品在队伍仓 `open-vela/contest2026_023_0xS3`；公共仓改动走 `dev-ai-contest-2026` 分支。Gitee 只是镜像。

## 标准开发循环

```bash
# 开发机
export PATH=/home/lingview/openvela/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:/home/lingview/.local/bin:$PATH
cd /home/lingview/openvela/nuttx
make -j8

# 烧录（USB-JTAG 或 CH343）
bash /tmp/flash.sh
# 如 RTS 复位不可靠：
sg dialout -c '/home/lingview/.espressif/python_env/idf5.3_py3.10_env/bin/esptool.py --chip esp32s3 --port /dev/ttyACM0 --after watchdog_reset chip_id'
```

- `nuttx.bin` 生成后必须做真机冒烟：屏幕六页、四键、SSH 采集、告警、AI 确认、恢复。
- 只插 USB 供电是演示场景；不要依赖串口 console 或主机 relay 才能跑。
- 改动 UI 后至少检查：标题区、分隔线、底栏、告警浮层、确认弹窗是否重叠/裁切。

## 常见改动落点

| 要改什么 | 先看哪里 |
|---|---|
| 新指标 / SSH 采集 | `velaops_ssh.c` 的采集脚本、解析器；`velaops_ui.h` 的 state；`velaops_health.c` 评分；`velaops_ui.c` 页面 |
| 新 AI 动作 | `velaops_llm.c` 的 JSON schema；`velaops_main.c` 的白名单/黑名单、确认、执行、复核 |
| 页面/字体/布局 | `velaops_ui.c`、`velaops_cn_font.h`；遵循 320×240、16px CJK、5×7 ASCII |
| 按键行为 | `board_hwtest_*` + `velaops_main.c` 的 `key_thread`；面板顺序 UP/DOWN/BACK/OK |
| 配网/配置 | `velaops_prov.c`、`velaops_conf.c`；`/data` 下的 LittleFS 配置 |
| 板级/屏幕/WiFi | `nuttx/boards/xtensa/esp32s3/esp32s3-eye/`、`nuttx/arch/xtensa/src/esp32s3/` |
| 比赛提交 | `references/contest-submission.md` |

## 任务前自检

- 先确认当前分支和远端：`git status --short`、`git log -1`，不要覆盖用户未提交改动。
- 修改前先搜同类实现，不要另起一套 GPIO/字体/网络抽象。
- 涉及 `mov sp`、GPIO 注册、IMEM 堆、AI 执行路径时，把本文件的硬规则再过一遍。
- 硬件测试若会触发蜂鸣器/LED/复位，先告知用户预期现象和时长。

## References

- `references/build-flash.md` — 开发机、工具链、编译、烧录、串口、DNS/GitHub 认证、常见构建错误。
- `references/hardware-ui.md` — 引脚/极性、ILI9341、GB2312 与 ASCII 字体、六页布局、四键语义。
- `references/network-ssh-ai.md` — SSH 采集与 libssh2、健康分、AI JSON 动作、安全边界、DNS/看门狗/锁存。
- `references/pitfalls.md` — 踩坑记录：症状、根因、修复、证据；含 WiFi/SSH/AI/字体/提交坑。
- `references/contest-submission.md` — 大赛规则、队伍仓/公共仓、CLA、日志、PR 与当前提交状态。
