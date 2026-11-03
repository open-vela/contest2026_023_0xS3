# 硬件、显示与 UI

## 板级引脚与极性

自研 ESP32-S3 N16R8 板，ESP32-S3-EYE 配置：

| 功能 | GPIO | 极性/说明 |
|---|---|---|
| 四键 | GPIO4 / 5 / 6 / 7 | 低有效，内部上拉；面板顺序 UP / DOWN / BACK / OK |
| LED | GPIO17 | 高电平点亮 |
| 蜂鸣器 | GPIO16 | 高电平响（AO3400A 驱动） |
| BOOT | `/dev/gpio1` | 保底输入 |

- 四键、LED、蜂鸣器必须通过 `board_hwtest_gpio_init/read/write` 直连 GPIO。**不要注册 `/dev/gpio`**，额外 gpio 设备会让 WiFi EAPOL 超时。
- `board_hwtest_gpio_write(0, v)` 控制 LED，`board_hwtest_gpio_write(1, v)` 控制蜂鸣器。
- `board_hwtest_key_drive(id, ms)` 可把按键脚临时拉低做模拟自检；用完恢复输入上拉。
- 开机必须保持 LED/蜂鸣器静音；蜂鸣器是新板高电平有效，旧的“低有效”写法会常响。

## 四键语义

代码中 `i=0..3` 对应 GPIO4..7：

- `i=0`：上一页（UP）
- `i=1`：下一页（DOWN）
- `i=2/3`：短按静音/取消；长按 2 秒触发批准
- 面板第 4 个键是 OK，长按用于批准；演示时以 OK 为准
- 告警页：长按 OK 一次直接执行（不再弹二次确认）
- 其他页：长按 OK 弹确认框
- BACK：关闭告警/取消确认；BOOT 仅作保底

按键线程 20ms 扫描、30ms 去抖，直接读 GPIO，不等待网络；翻页/静音不能阻塞在主循环或 SSH 上。

## 显示屏

- ILI9341 MSP2402 2.4 寸，320×240，**RLANDSCAPE 反向横屏**（按键朝上安装方向）。
- SPI2，20MHz，RGB565；默认 CS=10、DC=8、RST=9、MOSI=11、SCK=12、MISO=13。
- 关键 defconfig：

```text
CONFIG_ESP32S3_EYE_LCD_ILI9341=y
CONFIG_ESP32S3_EYE_ILI9341_DC=8
CONFIG_ESP32S3_EYE_ILI9341_RESET=9
CONFIG_ESP32S3_EYE_ILI9341_FREQUENCY=20000000
CONFIG_LCD_ILI9341=y
CONFIG_LCD_ILI9341_IFACE0_RLANDSCAPE=y
CONFIG_LCD_ILI9341_IFACE0_RGB565=y
```

- 驱动改动在 `drivers/lcd/ili9341.c` 和板级 `esp32s3_board_ili9341.c`；换屏/换方向先确认 `xres/yres` 和颜色字节序。

## 中文字库

- GB2312 全量 **7445 字符**（6763 汉字 + 682 符号），SimSun 16px，16×16 点阵。
- 生成脚本：项目根目录 `gen_full_font.py`；输出 `velaops_cn_font.h`。
- 查找使用二分查找 `cjk_glyph()`；不要线性扫 7445 项。
- 字库文件必须 LF 换行；CRLF 会触发 `git diff --check` 的 trailing whitespace，导致 PR 不干净。

## ASCII 字体

- 5×7 点阵，`FONT_WIDTH=5`、`FONT_HEIGHT=7`、`ADVANCE=6`、`CJK_SIZE=16`。
- 大写占第 0–6 行；小写 x-height 占第 2–6 行，和大写共用基线；上伸字母到第 0 行，降部压缩在 6 行内。
- 2× 字号时 `dy=1`，让 14px ASCII 在 16px CJK 行里居中。
- 不允许把 a–z 强制转大写；必须保留独立小写字形表。
- 老字体是 5×5 大写 + 5×7 小写，混排会基线错位、大小不一致；已统一为 5×7，不要退回。

## 页面与排版

六页：概览、性能、运维、总结、根因、处置。

- 标题/分隔线/底栏位置固定；每页元素之间至少留 4–6px。
- 主标签/数值用 2× ASCII；命令和输出用 1×；中英文混排保持同一基线。
- 文字超长必须 `wrap_text()` 截断换行，不能画到屏幕外；预留行数上限。
- 概览：健康分数字下移，条形图右侧数值预留宽度，避免压条。
- 性能页：网络行与分隔线保持距离；“僵尸进程”不要简写成“僵尸”。
- 处置页：标题、状态、目标、结果、判定理由、待执行命令、输出之间要留足上下间距；命令 1×，理由 2×。
- 告警是全屏浮层：先清屏再画框，看板不会同时显示；文案不要写“看板与告警同屏”。
- 确认弹窗：中英文行距 18px，标题/分隔线/详情/hint 不要互相压。
- 第 6 页处置页的空闲状态可以显示健康分，但不能和待执行命令区域重叠。

## UI 验证清单

- 六页逐页翻，标题、内容、底栏不重叠、不裁切。
- 小写英文/命令/URL 正常显示，大小写同基线。
- 告警浮层闪烁、BACK 静音、长按 OK 执行/弹确认。
- 确认框取消、超时、执行后回显都正常。
- 只插 USB 供电时，翻页/静音不等待网络。
