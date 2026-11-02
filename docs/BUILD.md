# 构建与烧录

以下命令在 openvela 工作区根目录执行（队伍仓的上一级）。

## 1. 应用补丁

```bash
bash contest2026_023_0xS3/docs/tools/apply_contest_patches.sh
```

- `patches/nuttx`：ESP32-S3-EYE 板级适配与公共改动（ILI9341、四键、LED/蜂鸣器、MTD、配网、WiFi 稳定性）。
- `patches/apps`：libssh2 移植与 mbedTLS Make.defs 适配。

## 2. 编译

```bash
export PATH="$PWD/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:/home/lingview/.local/bin:$PATH"
./build.sh boards/xtensa/esp32s3/esp32s3-eye/configs/ai_agent
```

应用代码通过 manifest 的 linkfile 映射在 `apps/examples/velaops`，无需手动复制。

## 3. 烧录

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 460800   --before default-reset --after hard-reset   write-flash 0x0 nuttx/nuttx.bin
```
