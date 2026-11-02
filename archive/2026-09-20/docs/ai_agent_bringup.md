# ESP32-S3-EYE 实机跑通 ai_agent 记录

> 队伍编号：`023`　板卡：ESP32-S3-EYE（正式比赛开发板）
> 目标：在 openvela 全功能配置上启用 ai_agent，实机完成 WiFi + LLM 对话。
> 结果：✅ 实机 `ask` 对话成功，连续多次稳定返回 AI 回复。

---

## 一、最终结论（一句话）

ai_agent 实机跑通的**两个关键坑**是：

1. **openvela 全功能配置默认开启 BLE，Kconfig 会自动打开 `ESP32S3_WIFI_BT_COEXIST`（WiFi/蓝牙射频共存）**，导致 WiFi 数据通路时好时坏（ping 0%→100% 丢包波动、TLS 握手时成时败）。**关掉 BLE 后 WiFi 立即稳定**。
2. **串口一次性发送长命令会丢尾部字符**，导致 `router_set mimo <key>` 的 API Key 被截断（末尾几个字符丢失），MiMo 返回 `401 Invalid API Key`。需**逐字符慢速发送**长命令。

> 其余还有一批小问题（SMP 崩溃、wapi psk 参数、RUNNING 标志、DNS、TLS 版本、TCP 接收缓冲、模型名），均在下方列出。

---

## 二、编译配置变更

板级配置：`vendor/espressif/boards/esp32s3/esp32s3-eye/configs/openvela/defconfig`

在原有 openvela 全功能配置基础上，做了以下变更：

| 变更 | 配置项 | 原因 |
|------|--------|------|
| 启用 ai_agent | `CONFIG_EXAMPLES_AI_AGENT_VELA=y` | 核心 |
| ai_agent 依赖 | `CONFIG_CRYPTO_MBEDTLS=y`<br>`CONFIG_FS_TMPFS=y`<br>`CONFIG_SYSTEM_POPEN=y`<br>`CONFIG_SYSTEM_SYSTEM=y` | mbedTLS/临时文件/`system()` 命令 |
| **关闭 SMP** | 删除 `CONFIG_SMP`/`CONFIG_SMP_NCPUS` | ai_agent 工具注册后双核崩溃（EXCCAUSE=001d） |
| **关闭 BLE** | 删除 `CONFIG_ESP32S3_BLE`<br>`CONFIG_ESPRESSIF_BLE`<br>`CONFIG_DRIVERS_BLUETOOTH`<br>`CONFIG_WIRELESS_BLUETOOTH`<br>`CONFIG_NET_BLUETOOTH`<br>`CONFIG_BLUETOOTH_CNTRL_HOST_FLOW_DISABLE`<br>`CONFIG_BTSAK` | **关键**：避免 WiFi/BT 共存干扰 |
| 固定 DNS | `CONFIG_NETDB_DNSSERVER_IPv4=y`<br>`CONFIG_NETDB_DNSSERVER_IPv4ADDR=0x72727272` | 114.114.114.114，否则 DNS 解析失败 |
| TCP 接收缓冲 | `CONFIG_NET_RECV_BUFSIZE=16384` | TLS 握手需接收较大证书链 |
| 关闭通道（省内存/少并发） | `# CONFIG_AI_AGENT_FEISHU is not set` 等 | 微信/飞书/MQTT/Node 通道 |

> 说明：`CONFIG_EXAMPLES_AI_AGENT_VELA` 的依赖（mbedTLS、TMPFS、POPEN、SYSTEM）是本板 Make 构建下 ai_agent 必需的，缺任一都会在链接期报 `undefined reference`。

---

## 三、ai_agent 包源码修复

### 1. `packages/ai_agent/src/infra/network_manager.c`

| 修复 | 内容 |
|------|------|
| **wapi psk 补 WPA2 参数** | `wapi psk %s %s 3` → `wapi psk %s %s 3 2`（缺 ver=2 会导致无法关联 WPA2 路由器） |
| **强制 carrier/RUNNING** | WiFi 关联后等待接口 `IFF_RUNNING`，超时则 `netdev_carrier_on()` 强制置位（驱动偶发丢 STA_CONNECT 事件，DHCP 依赖 RUNNING，否则 `EHOSTUNREACH`） |
| **显式配置 DNS** | 连网后 `dns_add_nameserver()` 加入 114.114.114.114 / 223.5.5.5 |
| **跳过重复重连** | `network_wifi_reconnect()` 先检查接口已 RUNNING 则跳过，避免启动时破坏已连好的 WiFi |

### 2. `packages/ai_agent/src/channels/cmd_llm.c`

- MiMo 主模型名 `mimo-v2-flash` → **`mimo-v2.5`**（按 MiMo 官方当前可用模型）。

### 3. `packages/ai_agent/Makefile`

- LVGL UI 源文件路径 `src/lvgl_ui/lvgl_ui_channel.c` → `src/ui/lvgl_ui_channel.c`（Make 构建路径错误；本配置未启用 LVGL_UI，属顺手修正）。

### 4. `packages/ai_agent/fix_esp32s3.sh`

- 等待 esp-hal-3rdparty clone 的超时从 180s 改为 1200s（首次构建 clone 较慢，原超时会导致补丁未打上）。

---

## 四、构建期临时补丁（不可提交）

ai_agent 编译依赖 `packages/ai_agent/fix_esp32s3.sh` 在**构建期间**打 4 处补丁（上游要求零改动，故不能进 defconfig/源码）：

1. `apps/crypto/mbedtls/Make.defs`：`-I` → `-isystem`（ESP-IDF 与 NuttX mbedtls 头文件优先级）
2. ESP-IDF `mbedtls_config.h`：禁用 `MBEDTLS_CCM_C`（结构体冲突）
3. ESP-IDF `clk_ctrl_os.c`：spinlock 初始化 `0` → `SP_UNLOCKED`
4. `esp32s3_bringup.c`：挂载 `/data` tmpfs（ai_agent 配置持久化目录）

> 注意：每次 `distclean` 会删除 `nuttx/arch/xtensa/src/esp32s3/esp-hal-3rdparty`（重新 clone），上述补丁会失效，需要重新执行 `fix_esp32s3.sh`（或在构建期后台运行）。

---

## 五、根因排查关键日志（供后续参考）

| 现象 | 根因 | 解决 |
|------|------|------|
| `xtensa_user_panic EXCCAUSE=001d task: ai_agent` | SMP 双核下工具注册后崩溃 | 关 SMP |
| `wapi sense wlan0` 返回 `-128`（未关联） | `wapi psk` 缺 ver=2 | 补 `3 2` |
| `netlib_obtain_ipv4addr() failed errno=113(EHOSTUNREACH)` | 接口未 RUNNING，DHCP 广播选不到设备 | 强制 carrier_on |
| `net_connect ... ret=0x52`（UNKNOWN_HOST） | DNS 未配置 | 固定 DNS |
| `ssl_handshake ret=-0x4c errno=107(ENOTCONN)` | WiFi 链路丢包（BLE 共存） | 关 BLE |
| LLM 返回 `401 Invalid API Key` | key 被串口截断 | 逐字符发送 |

---

## 六、交叉验证结论（普通板 vs eye 板）

排查中曾怀疑"是否开发板硬件问题"，用一块普通 N16R8 开发板交叉验证：

- **普通板（N16R8）**：无法引导（EFUSE 配置为 quad flash，ROM 加载镜像第二段失败：`SHA-256 comparison failed` / `ets_loader.c 78`）。**属该板 flash/EFUSE 硬件问题，与 vela 无关**。
- **eye 板（比赛板）**：同样固件正常引导、WiFi、LLM 全部正常。

> 结论：问题出在配置（BLE 共存）而非硬件。比赛用 eye 板即可。

---

## 七、实机运行流程

```bash
# 1. 烧录（eye 板，原生 USB 口 = /dev/ttyACM0 或按实际）
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 460800 \
  --before default-reset --after hard-reset write-flash 0x0 nuttx/nuttx.bin

# 2. 连 WiFi（NSH）
wapi mode wlan0 2
wapi psk wlan0 <password> 3 2      # 注意最后的 2 = WPA2
wapi essid wlan0 <ssid> 1
renew wlan0

# 3. 进入 ai_agent
ai_agent

# 4. 配置 LLM（逐字符发送，避免长命令丢字符）
vela> router_set mimo <api_key>

# 5. 对话
vela> ask 你好
```

---

## 八、注意事项 / 遗留

- **串口长命令丢字符**：`router_set`、`set_llm` 等含长 Key 的命令，用脚本逐字符发送（间隔 10ms 左右），否则 Key 尾部字符会丢失。
- **构建缓存**：改 defconfig 后 `build.sh` 会自动 distclean 并重新 clone esp-hal-3rdparty，网络慢时建议手动 clone（见 `fix_esp32s3.sh` 说明）。
- **模型名**：MiMo 当前用 `mimo-v2.5`，若后续 MiMo 调整模型，改 `cmd_llm.c` 里的 `g_router_presets`。
- 生产环境若要重新开启 BLE，需评估 WiFi/BT 共存对数据通路的影响（本次因演示需稳定 WiFi 而关闭）。

## 九、已知 Bug（待修）

**时钟跳变导致重启后首次 LLM 调用被 watchdog 误判超时**

- **现象**：系统重启后（RTC 未初始化，`time()` 返回 1970 年），首次 `ask` 时 `vela_tls` 检测到 `Clock too old, forcing to 2026`，用 `clock_settime()` 把系统时钟直接跳到 2026。LLM 实际**成功收到了回复**（日志 `llm] Response: N bytes text, finish=end_turn`），但 `agent_main.c` 的 **LLM watchdog** 用 `time()` 前后差值计算耗时，跨了 56 年 → 误判为超时（`LLM watchdog: call took 2748563488 ms (limit 60s)`），丢弃响应并提示"请求超时"。
- **影响**：只在**重启后的第一次对话**出现（第二次起时钟已是 2026，正常）。首次对话需重发一次。
- **建议修复**：LLM watchdog 计时改用单调时钟 `clock_gettime(CLOCK_MONOTONIC)`（不受 `clock_settime` 跳变影响），或 vela_tls 强制改时钟放到连接建立后、由 watchdog 之外统一处理。
- **状态**：已记录，后续修复。
