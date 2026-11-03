# 构建、烧录与开发机

## 开发机

- 地址：`192.168.100.220`，用户 `lingview`（密码不在 skill 中保存）。
- openvela 工作区：`/home/lingview/openvela/`。
- 构建仓库：`/home/lingview/openvela/nuttx`、`/home/lingview/openvela/apps`。
- 队伍仓克隆：`/home/lingview/openvela/contest2026_023_0xS3.clone`。
- 板子：ESP32-S3 N16R8 自购板，使用 `esp32s3-eye` 板级配置。

## 工具链

非交互 SSH 不会加载 `.bashrc`，必须显式加 PATH：

```bash
export PATH=/home/lingview/openvela/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:/home/lingview/.local/bin:$PATH
```

- 编译器：`xtensa-esp32s3-elf-gcc`。
- `esptool.py` 在 `/home/lingview/.local/bin/` 或 `/home/lingview/.espressif/python_env/idf5.3_py3.10_env/bin/`。

## 编译

```bash
cd /home/lingview/openvela/nuttx
make -j8
```

- 板级配置：`boards/xtensa/esp32s3/esp32s3-eye/configs/ai_agent`。
- 产物：`nuttx/nuttx.bin`。
- 队伍仓的等价构建见 `contest2026_023_0xS3.clone/docs/BUILD.md`，需要先应用 `patches/nuttx`、`patches/apps`。

## 烧录

当前常用的 `/tmp/flash.sh`：

```bash
#!/bin/bash
set -e
ESPTOOL=/home/lingview/.espressif/python_env/idf5.3_py3.10_env/bin/esptool.py
BIN=/home/lingview/openvela/nuttx/nuttx.bin
PORT=""
for d in /dev/ttyACM0 /dev/ttyACM1; do
  vid=$(udevadm info -q property -n "$d" 2>/dev/null | sed -n 's/^ID_VENDOR_ID=//p')
  if [ "$vid" = "1a86" ] || [ "$vid" = "303a" ]; then PORT="$d"; fi
done
if [ -z "$PORT" ]; then echo "no esp32 serial port found" >&2; exit 1; fi
sg dialout -c "$ESPTOOL --chip esp32s3 --port $PORT --baud 460800 \
  write_flash -fs detect -fm dio -ff 40m 0x0 $BIN"
```

烧录后如 RTS 复位不可靠：

```bash
sg dialout -c '/home/lingview/.espressif/python_env/idf5.3_py3.10_env/bin/esptool.py \
  --chip esp32s3 --port /dev/ttyACM0 --after watchdog_reset chip_id'
```

## 串口与 console

- `303a:1001` 是 USB-JTAG，通常枚举为 `/dev/ttyACM0`，**能烧录，但没有 NuttX console 输出**。
- `1a86` 是 CH343，插上后通常提供 NuttX console；串口枚举顺序可能互换。
- 只看 USB-JTAG 时不要因为“没有日志”误判固件没启动；先确认是不是 console 口不对。
- 复位/烧录脚本先按 VID 识别，不要硬编码单一端口。

## 开发机 DNS 与 GitHub

VM 默认 systemd-resolved 可能没有上游 DNS，`github.com` 解析失败：

```bash
sudo resolvectl dns ens33 223.5.5.5 8.8.8.8
sudo resolvectl flush-caches
getent hosts github.com
```

GitHub HTTPS 使用 `~/.git-credentials-github` + `git config --global credential.helper 'store --file=...'`；不要把 token 写进仓库或 URL。

## 常见错误

| 现象 | 原因 | 处理 |
|---|---|---|
| `xtensa-esp32s3-elf-gcc: command not found` | 非交互 shell 没加载 PATH | 显式 export prebuilts 工具链 |
| `esptool.py: command not found` | PATH 缺 `.local/bin` | 加 `/home/lingview/.local/bin` 或用 espressif venv 绝对路径 |
| 编译到 `elf2image` 失败 | 同上 | 同上 |
| USB-JTAG 烧完无任何输出 | USB-JTAG 没有 NuttX console | 用 CH343/UART，或按屏幕现象判断 |
| RTS 复位不生效 | 板子 RTS 复位电路不可靠 | 用 `--after watchdog_reset` |
| `git apply` 对新文件报 `does not exist in index` | 不要加 `--index` | 去掉 `--index`，应用后 `git add`；以冲突列表判失败 |
| `make` 提示源文件缺失 | 切换了分支但工作区有旧产物 | 先看 `git status`；必要时只清理相关对象，避免误删 `esp-hal-3rdparty` |

## SSH/自动化注意

- 通过 paramiko 跑 make 时显式传 PATH；stdout 用 UTF-8 解码，否则中文提交信息/日志会乱码。
- 后台命令不要用 `echo` 结尾制造假 exit code；看日志确认。
- 停服务、重打包、烧录前先确认没有其他人在用板子/构建树。
