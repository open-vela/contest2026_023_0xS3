# ESP32-S3-EYE：编译、烧录与 WiFi 连接

本文档记录在 openvela（`dev-ai-contest-2026`）上，让 ESP32-S3-EYE 开发板正常编译、烧录并
连接 WiFi 的完整过程，以及过程中遇到的关键坑（尤其是 `wapi psk` 命令参数错误导致的 WiFi 连不上）。

> 队伍编号：`023`　板卡：ESP32-S3-EYE
> Wi-Fi：使用 WPA2 网络；SSID、密码、BSSID 和网段属于本地私密配置，不写入仓库。

---

## 一、最终结论（一句话）

WiFi 连不上的根因是：**`wapi psk wlan0 <密码> 3 wpa` 里最后一个参数 `wpa` 是无效的**，
导致 wapi 进程在执行到 `wapi_str2ndx()` 时直接 `exit()`，**WiFi 密钥根本没被设置**，
于是无法关联 AP、DHCP 失败、接口停留在陈旧的静态 IP `10.0.0.2`。

**正确命令是把 `wpa` 换成 WPA2 的下标 `2`：**

```bash
wapi psk wlan0 <wifi_password> 3 2 # alg=3(CCMP), ver=2(WPA2)
```

验证结果（本板实测通过）：

```text
wapi sense wlan0                 # -> -55（已关联，信号强）
renew wlan0                      # DHCP 成功
ifconfig wlan0                   # inet 192.168.31.242  DRaddr 192.168.31.1  Mask 255.255.255.0
ping -c 3 192.168.31.1           # 0% packet loss（路由网关）
ping -c 2 223.5.5.5              # 0% packet loss（公网，阿里 DNS）
```

---

## 二、完整的 WiFi 连接命令序列（NSH）

在 NSH 里依次执行（先断开旧连接，设置模式、密钥，再指定 ESSID 触发关联，最后 `renew` 走 DHCP）：

```bash
wapi disconnect wlan0
wapi mode wlan0 2
wapi psk wlan0 <wifi_password> 3 2 # 注意是 2，不是 wpa
wapi essid wlan0 <wifi_ssid> 1
wapi sense wlan0                 # 期望看到负信号值（-55 左右）而非 -128
renew wlan0
ifconfig wlan0                   # 期望 inet 192.168.31.x
```

### `wapi psk` 参数解释

`wapi psk <ifname> <passphrase> <alg> <ver>`

- `alg`：算法，用下标。`0=NONE 1=WEP 2=TKIP 3=CCMP`。家用 WPA2 路由器用 `3`（CCMP）。
- `ver`：WPA 版本，用下标。`0=NONE 1=WPA(1) 2=WPA2 3=WPA3`。家用路由器用 `2`（WPA2）。

源码依据（`apps/wireless/wapi/src/wapi.c` 的 `wapi_str2ndx()`）：

```c
if (isdigit(name[0]))
  {
    return atoi(name);          // 传数字下标
  }
for (ndx = 0; list[ndx]; ndx++)
  {
    if (strcmp(name, list[ndx]) == 0)
      {
        return ndx;
      }
  }
WAPI_ERROR("ERROR: Invalid option string: %s\n", name);
...
exit(EXIT_FAILURE);             // 传了非法字符串，进程直接退出！
```

`g_wapi_wpa_ver_flags[]` 只有这些合法串：`WPA_VER_NONE / WPA_VER_1 / WPA_VER_2 / WPA_VER_3`。
所以传 `wpa`（不在其中）会命中 `exit(EXIT_FAILURE)`，密钥根本没设上。
要传 WPA2 请用数字 `2`，或完整的 `WPA_VER_2`。

---

## 三、编译

板级全功能 openvela 配置路径：

```
vendor/espressif/boards/esp32s3/esp32s3-eye/configs/openvela
```

实际执行的构建命令（与 vendor 自带 build.sh 流程一致）：

```bash
export PATH="$PWD/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH"

./build.sh esp32s3-eye:openvela distclean
bash packages/ai_agent/fix_esp32s3.sh &     # 构建期后台应用修复（见第四节）
./build.sh esp32s3-eye:openvela
```

构建成功标志：

```text
Successfully created ESP32-S3 image.
Generated: nuttx.bin
```

产物（位于 `nuttx/`）：

- `nuttx`（ELF，约 21.5 MB）
- `nuttx.bin`（约 1.6 MB，烧录偏移 `0x0`）
- `nuttx.hex`（约 4.4 MB）

### 关于陈旧 `.config`

`nuttx/.config` 可能携带上一次构建（例如 VelaOps 原型）的遗留配置，例如
`CONFIG_NETINIT_IPADDR=0x0a000002`（10.0.0.2）、`CONFIG_NETINIT_DHCPC is not set`，
这就是 `ifconfig wlan0` 显示陈旧 `10.0.0.2` 的来源。若遇此问题，应删除 `nuttx/.config`、
`nuttx/defconfig` 后重新 configure 生成干净配置再编译。

---

## 四、修复脚本 `packages/ai_agent/fix_esp32s3.sh`

`fix_esp32s3.sh` 用于解决**只能在构建期临时应用、无法写进 defconfig** 的编译问题，
从而保证上游 `nuttx / apps / vendor` **零改动**。它需在首次构建时后台运行，等
esp-hal-3rdparty 克隆完成并打补丁后，依次应用四处修复：

1. **`apps/crypto/mbedtls/Make.defs`：`-I` → `-isystem`**（头文件优先级）
   ESP-IDF 与 NuttX 的 `cipher_info_t` 布局不同，用 `-isystem` 让 ESP-IDF 的 `-I` 头文件
   对 esp-hal 源文件生效。

2. **esp-hal-3rdparty `mbedtls_config.h`：关闭 `MBEDTLS_CCM_C`**（结构体冲突）
   CCM*-NO-TAG 结构体在 ESP-IDF 与 NuttX 的 mbedtls fork 中不同，禁用 CCM 避免冲突。

3. **esp-hal-3rdparty `clk_ctrl_os.c`：spinlock 初始化**
   ESP-IDF 用 `0` 初始化 spinlock_t，NuttX 要求 `SP_UNLOCKED` 宏：
   `#define LOCK_INITIALIZER_UNLOCKED 0` → `... SP_UNLOCKED`
   （对应 vendor 板自带补丁
   `vendor/espressif/boards/esp32s3/esp32s3-eye/scripts/patches/0001-esp-hal-3rdparty-fix-spinlock-init.patch`）

4. **`esp32s3_bringup.c`：在 `/data` 挂 tmpfs**
   ai_agent 把配置持久化到 `/data/ai_agent/config/config.json`，不挂载则
   `config_show` 一直显示 `(not set)`。`/data` 挂载无法用 defconfig 表达，只能改板级初始化。

> 注意：这四处是构建期临时修改，**不能提交**。上游要求零改动。

---

## 五、烧录

esptool 装在 venv（系统无 pip，用 `python3 -m venv` 创建）：

```
/home/lu/桌面/openvela/.buildlog/esptool-venv/bin/
```

烧录命令（`--before default-reset --after hard-reset` 硬复位后可正常启动到 NSH）：

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 460800 \
  --before default-reset --after hard-reset \
  write-flash 0x0 nuttx/nuttx.bin
```

### 串口操作注意事项（很重要）

- pyserial 打开 `/dev/ttyACM0` 会切换 DTR/RTS，**导致板反复复位、启动挂起**。
- 交互必须用**不碰调制控制线**的原始 tty 方式（`os.open` + `termios`，见下）。

诊断用的最小原始 tty 会话脚本骨架（`/tmp/tty_session.py`）：

```python
import os, termios, time, select
fd = os.open("/dev/ttyACM0", os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] &= ~(termios.IGNBRK|termios.BRKINT|termios.PARMRK|termios.ISTRIP|
          termios.INLCR|termios.IGNCR|termios.ICRNL|termios.IXON)
a[1] &= ~termios.OPOST
a[2] &= ~(termios.CSIZE|termios.PARENB); a[2] |= termios.CS8
a[3] &= ~(termios.ICANON|termios.ECHO|termios.ECHOE|termios.ISIG)
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
os.set_blocking(fd, False)
```

### 串口权限

已将用户加入 `dialout` 组；在不重登录的情况下用 `sg dialout -c '...'` 运行带串口的命令。

---

## 六、排查过程速记（为什么最后这么调）

1. `wapi scan wlan0` 能找到目标 SSID，且信号强度正常 → 驱动正常。
2. 用 `wapi psk ... 3 wpa` 后 `wapi essid`，`wapi sense wlan0` 返回 `-128`（未关联），
   `renew wlan0` 报 `netlib_obtain_ipv4addr() failed`，IP 一直是陈旧 `10.0.0.2`。
3. 读 `wapi.c` 源码发现 `wpa` 不是合法版本串，`wapi_str2ndx()` 直接 `exit(EXIT_FAILURE)`，
   密钥从未设置 → 关联失败。
4. 改用 `wapi psk wlan0 <wifi_password> 3 2`（CCMP+WPA2）→ 关联和 DHCP 成功，
   ping 通路由网关与公网。✅

---

## 七、遗留/可选事项

- 陈旧 `.config` 携带静态 IP `10.0.0.2` 的问题未在本次处理（WiFi 连接本身已不受影响）。
  如需干净基线，建议删除 `nuttx/.config`、`nuttx/defconfig` 后重新 configure + 重编重烧。
- 若路由器使用 WPA3，把 `wapi psk` 的 `ver` 参数改成 `3`；TKIP 老路由器把 `alg` 改成 `2`。

## 2026-09 Wi-Fi 假在线调查

当前上游 `esp32s3_wifi_adapter.c` 与远端最新版本一致，未发现针对该现象的修复。队伍侧补丁 `patches/nuttx/0004-esp32s3-wifi-stop-reconnect-race.patch` 修复 STA 停止期间排队的断开事件再次触发 `esp_wifi_connect()` 的竞态：停止前关闭自动重连，事件处理仅在 STA 仍已启动时重连。该补丁须经真机长时间断链/恢复测试后再决定是否提交上游。

### 真机验证记录（2026-09-13）

刷入含 `0004-esp32s3-wifi-stop-reconnect-race.patch` 的固件后，板端自动配置获得 `192.168.71.80`，`wapi sense wlan0` 返回 `-64`。主机对板端持续逐秒 ICMP 发送 167 次，全部成功（0% 丢包）；结束时 `ifconfig wlan0` 仍为 RUNNING，RX/TX 错误、超时和丢弃计数均为 0。该结果覆盖了此前约 1~3 次请求后失效的窗口，但尚未替代跨小时和热点真实断电恢复测试。
