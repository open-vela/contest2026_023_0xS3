# 踩坑记录与解决方式

按症状查。每条都给出根因和验证方式，避免“看起来像网络问题”就改网络。

## 快速索引

| # | 症状 | 根因 |
|---|---|---|
| 1 | ai_agent 启动即崩，PC=0、EXCM=1 | 删了启动期 `mov sp` |
| 2 | 烧录成功但串口无输出/启动卡住 | USB-serial console 与 UART console 混用 |
| 3 | WiFi 发不出去，`ESP_ERR_NO_MEM` 永久失败 | 内部 DRAM 耗尽，WiFi 分配到 PSRAM 被拒 |
| 4 | 加按键后 WiFi 四次握手失败/EAPOL 超时 | 注册了额外 `/dev/gpio` 设备 |
| 5 | 蜂鸣器常响/按键反了 | 蜂鸣器高有效、按键低有效 |
| 6 | 屏幕方向反/颜色不对/显示错位 | ILI9341 方向/字节序/尺寸配置 |
| 7 | 英文大小写不一致、上下不在一条线 | 5×5 大写 + 5×7 小写混用 |
| 8 | 中文/命令被裁切、行间重叠 | 绘制宽度/行距/换行上限不足 |
| 9 | 高负载下 SSH 握手 -9/-13 | 握手超时/重试/内存池不足 |
| 10 | 告警后恢复不了、AI 卡住 | DNS 阻塞、无 IP 缓存、无 watchdog |
| 11 | TOP 进程变了，处置动作消失 | 动作没锁存 |
| 12 | 长按 OK 没触发 AI/执行 | AI 调用时序或确认状态错误 |
| 13 | 手机连上 SoftAP 但数据不通 | 配网数据面问题（未彻底解决） |
| 14 | 烧录器包体 128MB | conda 打包带入全套依赖 |
| 15 | `git apply` 对新文件报 index 错误 | 用了 `--index` |
| 16 | 队伍仓 PR 合不进去/历史只有一条 | 分支保护 + rebase-only + 单提交 |
| 17 | 队伍仓补丁打了但板级不生效 | 补丁打在 nuttx board，实际编译可能走 vendor |
| 18 | 云端看不到追加提交 | 看的是 fork 默认分支，未同步上游 |

## 1. ai_agent 启动崩溃（PC=0 / EXCM=1）

- 症状：启动早期双异常，PC=0，EXCM=1，日志栈帧像 IDLE。
- 根因：误删 `arch/xtensa/src/esp32s3/esp32s3_start.c` 启动期 `mov sp`。早期帧写到未知栈，被记进 IDLE 的 xcp.regs。
- 修复：恢复上游 `mov sp`，不要“优化”掉。
- 注意：`mov sp` 不是之前怀疑的 bootloader/MMU 问题；`esp_loader` 本身和 ESP-IDF 等价。

## 2. 烧录成功但没日志/启动卡住

- 症状：USB-JTAG 烧录成功，复位后没有 NuttX banner；换 UART 才看到。
- 根因：`303a:1001` USB-JTAG 没有 NuttX console；`CONFIG_ESP32S3_USBSERIAL`/console 选择混用会卡启动。
- 修复：
  - 烧录用 USB-JTAG 可以，但看日志用 CH343/`1a86` UART。
  - 需要稳定 console 时不要启用 USB-serial console。
  - 复位不可靠时用 `esptool --after watchdog_reset chip_id`。
- 验证：CH343 上能看到 NSH/应用日志，USB-JTAG 只负责烧录。

## 3. WiFi 发送永久 `ESP_ERR_NO_MEM`

- 症状：应用申请大缓冲后 WiFi 停止发送，`esp_wifi_internal_tx` 持续 ENOMEM，重连无效。
- 根因：内部 DRAM 被应用大缓冲耗尽；WiFi 驱动的内部内存分配落到 PSRAM，被 `esp_malloc_internal` 拒绝。
- 修复：启用 `CONFIG_XTENSA_IMEM_USE_SEPARATE_HEAP`（128KB）给 WiFi 独立内部堆；任务栈继续用普通堆，避免挤占该池。
- 验证：6 次 15KB 请求连续通过；压测后 WiFi 仍可发送。

## 4. 加按键导致 WiFi 四次握手失败

- 症状：注册新的 `/dev/gpio` 设备后，WiFi 关联/四次握手超时。
- 根因：额外 gpio 设备干扰该板 WiFi/EAPOL 路径。
- 修复：四键/LED/蜂鸣器全部走板级 `board_hwtest_*` 直连 GPIO，不注册 `/dev/gpio`。
- 验证：按键、LED、蜂鸣器正常，WiFi 连续重连不失败。

## 5. 蜂鸣器常响 / 按键极性反

- 硬件：蜂鸣器 GPIO16 高电平响；LED GPIO17 高电平亮；四键 GPIO4–7 低有效、内部上拉。
- 修复：开机把 LED/蜂鸣器写 0；按键判断 `read()==0` 为按下；`key_drive` 临时输出低再恢复输入上拉。
- 验证：开机静音；按下面板第 4 个 OK 键能翻页/批准。

## 6. ILI9341 方向/颜色/尺寸

- 使用 MSP2402 2.4 寸 320×240，RLANDSCAPE，RGB565，SPI2 20MHz。
- 方向不对先查 `CONFIG_LCD_ILI9341_IFACE0_RLANDSCAPE` 和 `xres/yres`；颜色不对查 RGB565/字节序。
- 换屏时同时改板级 `esp32s3_board_ili9341.c`、Kconfig、Make.defs、defconfig，不要只改驱动。

## 7. 英文大小写不一致/基线错位

- 旧实现：A–Z 是 5×5 表，a–z 是 5×7 表；混排时小写低两行，`V` 看起来比 `O` 大。
- 修复：统一 5×7；大写 0–6 行，小写 x-height 2–6 行，共用基线；去掉 `a–z` 强制转大写；2× 时 `dy=1`。
- 验证：`VelaOps` 的 V/O 同高，`ela` 与 `Ops` 在同一基线，命令里小写正常。

## 8. 文本裁切/重叠

- 常见原因：固定宽度不够、`wrap_text` 行数上限太大、行距 12/16 混用、2× 字高被低估。
- 修复：
  - 2× 主文本按 14px 高预留；行距至少 16–18px。
  - 长文本先 `wrap_text`，限制最大行数，超出截断。
  - 处置页把状态/目标/结果/理由/命令/输出分区，分隔线上下各留 4–6px。
  - 告警浮层最多 3 行；确认框行距 18px。
- 验证：六页逐页看，底栏、分隔线、弹窗都不压字。

## 9. SSH 高负载握手失败

- 症状：`libssh2_session_handshake` 返回 -9/-13，重试有时成功。
- 根因：握手超时、内部内存紧张、WiFi/SSH 同时抢资源。
- 修复：握手超时 30s、重试 3 次、SSH/TLS 固定 256KB 池；采集合并为一次 exec，减少握手次数。
- 验证：CPU 压测时仍能完成采集/执行。

## 10. AI 卡住/告警恢复不了

- 症状：告警后 AI 一直不出结果，恢复后告警框不消失。
- 根因：DNS 解析阻塞、LLM 无超时、AI 调用占住交互窗口、没有主循环看门狗。
- 修复：
  - 默认 DNS 223.5.5.5；DNS 成功后缓存 `llm_ip`，后续跳过 DNS。
  - LLM socket 超时 8s；AI 调用放到 5s 交互窗口之后。
  - 45s 主循环无进展 `BOARDIOC_RESET`。
  - 告警开始清 `g_ai_valid`；恢复后不显示旧结论。
- 验证：断网/错 DNS 时看板与按键仍可用，恢复后告警能关闭。

## 11. 处置动作消失

- 症状：TOP 进程变化后，已经生成的 kill/restart 动作从处置页消失。
- 根因：动作每次重算，依赖当前 TOP。
- 修复：`g_latch_*` 锁存 kind/target/verb/command/ai 标志；确认/执行使用锁存值。
- 验证：告警后让 CPU 进程变化，处置页动作仍保留。

## 12. 长按 OK 没执行

- 根因：确认状态、AI 调用时序、动作锁存、页面状态相互影响。
- 修复：
  - 告警页长按 OK 直接执行；其他页长按弹确认。
  - exec 必须确认并展示命令。
  - AI 调用在交互窗口之后，但确认状态优先，AI 不覆盖确认。
  - 按键线程直接渲染，不等主循环。
- 验证：告警页长按一次执行；处置页长按弹确认；BACK 取消；执行结果显示 VERIFIED/BLOCKED/FAILED。

## 13. SoftAP 配网数据面

- 已知历史问题（2026-10-25）：手机能关联 SoftAP，但数据面不通。
- 不要先归因路由器/手机；先用 CH343 console 看 DHCP/DNS/httpd 日志、ARP/路由和端口。
- 如果未解决，演示不要依赖手机配网；用已写入的配置或串口/文件方式准备配置。

## 14. 烧录器体积

- 干净 venv（pyinstaller + esptool 5.5）约 18MB；conda 环境打包会带全套依赖到 128MB。
- 打包后要校验内置 `nuttx.bin` 与当前构建一致，不能带旧固件。

## 15. `git apply` 新文件

- `git apply --3way --index` 对新文件会报 `does not exist in index`，即使内容能落地。
- 用 `git apply --3way`（不要 `--index`），然后 `git add` 目标路径；用 `git diff --name-only --diff-filter=U` 判断冲突。
- 排除无关未跟踪目录（如 `testing/drivers/nist-sts`），不要 `git add -A` 全收。

## 16. 队伍仓 PR 与历史

- 大赛只在 GitHub；主作品在 `open-vela/contest2026_023_0xS3`。
- 分支有 ruleset：`cla/signature` + PR-only + 只允许 rebase merge；`protected: true`，不要 force push 改写。
- 旧历史 40 条是 `hanbingniao`；本次新工作用 `lingview`。提交前确认账号/CLA。
- 已合并的汇总提交不能无痛拆分；要拆只能改共享历史，需管理员/组委会放行。追加改动走新 PR。

## 17. 队伍仓板级补丁路径

- 队伍仓实际编译可能走 `vendor/espressif/boards/esp32s3/esp32s3-eye`；我们的补丁目前按 `nuttx/boards/.../esp32s3-eye` 生成，构建命令也走 nuttx board config。
- 如果必须用 vendor board，需要把 ILI9341、四键、LED/蜂鸣器、MTD、配网、defconfig 移植到 vendor；只打 nuttx 补丁不会生效。
- 先确认 build.sh 用的 config 路径，再决定补丁目标。

## 18. 云端看不到提交

- PR 合并只更新目标仓的 `dev-ai-contest-2026`，不会自动更新 fork 的默认分支。
- 看提交记录要去 `open-vela/contest2026_023_0xS3/tree/dev-ai-contest-2026`；fork 需要 `gh repo sync`。

## 其他新发现

- GB2312 全量字库 7445 字符（6763 汉字 + 682 符号），16×16 点阵约 1.5MB 头文件；二分查找可用。
- `CONFIG_XTENSA_IMEM_USE_SEPARATE_HEAP` 是 WiFi 稳定性关键，不是普通性能选项。
- 四键直连 GPIO 比 `/dev/gpio` 更稳；不要为了“通用性”再包一层 gpio 设备。
- AI 动作锁存 + 交互窗口后调用，是同时解决“动作消失”和“按键卡顿”的关键组合。
- 比赛公共仓 PR 的合并方式可能只允许 rebase；`--merge` 会被拒，`--squash` 也可能被禁。
- 生成 Mermaid 示意图时，CJK 文本容易在节点右边缘被裁；用固定宽度 `<div style='width:...'>` 包裹可避免（仅展示用，不影响设备 UI）。
