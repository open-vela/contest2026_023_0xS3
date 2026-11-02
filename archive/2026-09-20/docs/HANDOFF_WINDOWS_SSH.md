# Windows SSH 接手说明

当前队伍仓库分支：`local-planc-rebuild`。最新提交：`2de0457`，包含 ESP32-S3 Wi-Fi 重连竞态 patch、应用层短超时和真机验证记录。

## 电脑端

热点地址应为 `192.168.71.90`。进入队伍仓库后运行：

```bash
python3 docs/tools/prepare_live_demo.py --host 192.168.71.90 --force
python3 docs/tools/manage_demo_target.py start
PYTHONPATH=proxy/src python3 -u -m velaops_proxy.lan_ntp --port 40123
PYTHONPATH=proxy/src python3 -m velaops_proxy --config /tmp/opencode/velaops-live/proxy.json
```

NTP 和 Proxy 必须在持久 SSH 终端中运行，不能使用会随 shell 退出的后台命令。

## 板端

串口通常为 `/dev/ttyACM0`。烧录前确保 NuttX 已应用队伍 patch：

```bash
bash docs/tools/apply_nuttx_patches.sh
```

配置下发和校时：

```bash
python3 docs/tools/send_raw.py 'mkdir /data/velaops'
python3 docs/tools/serial_push.py /tmp/opencode/velaops-live/device-config.json /data/velaops/config.json
epoch_now=$(date +%s)
python3 docs/tools/send_slow.py "velaops set-time $epoch_now"
python3 docs/tools/send_slow.py 'velaops auth-check'
python3 docs/tools/send_slow.py 'velaops monitor'
```

## Debug 事件

单独终端运行：

```bash
PORT=/dev/ttyACM0 GUI_PORT=8765 python3 docs/tools/debug_event_gui.py
```

浏览器打开 `http://127.0.0.1:8765/`，点击“触发 DEBUG 事件”。串口日志出现 `lcd_popup` 即表示 Agent 已调用 `velaops_show_message`，板端 LCD 应显示 `TEST-OK`；短按 BOOT 关闭弹窗。

## 验证重点

```bash
ping 192.168.71.80
python3 docs/tools/send_raw.py 'wapi sense wlan0'
python3 docs/tools/send_raw.py 'ifconfig wlan0'
```

`wapi sense` 应返回负信号值（例如 `-64`），而不是 `-128`。若出现 `transport_error`，先看 Proxy 是否仍监听 `192.168.71.90:28790`，再看 `ifconfig` 的 RX/TX 错误和超时计数，避免把 Proxy/NTP 进程退出误判成 Wi-Fi 故障。
