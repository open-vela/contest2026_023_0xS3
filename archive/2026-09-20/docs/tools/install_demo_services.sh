#!/usr/bin/env bash
# 安装/刷新 VelaOps 开发机演示服务栈（user-systemd）。
#
# 覆盖四个常驻服务：Proxy、局域网 NTP、受限 MiMo 转发器、演示目标；全部使用
# 持久目录 $HOME/.local/state/velaops-live，并开启 linger，使开发机重启或注销
# 后仍自动拉起。可重复执行（写入同名 unit 后 daemon-reload + enable --now）。
#
# 用法：
#   bash docs/tools/install_demo_services.sh            # 自动探测开发机 IP
#   HOST_IP=192.168.71.90 BOARD_IP=192.168.71.80 \
#     bash docs/tools/install_demo_services.sh          # 显式指定
set -uo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
team_root=$(cd "$script_dir/../.." && pwd)
state_dir=${VELAOPS_STATE_DIR:-"$HOME/.local/state/velaops-live"}
unit_dir="$HOME/.config/systemd/user"

HOST_IP=${HOST_IP:-}
BOARD_IP=${BOARD_IP:-192.168.71.80}
if [ -z "$HOST_IP" ]; then
  HOST_IP=$(ip -4 -o addr show scope global 2>/dev/null |
             awk '{print $4}' | cut -d/ -f1 | head -n1)
fi
if [ -z "$HOST_IP" ]; then
  echo "无法自动探测开发机 IP，请设置 HOST_IP" >&2
  exit 1
fi

mkdir -p "$unit_dir" "$state_dir"

write_unit() {
  local name="$1"
  cat > "$unit_dir/$name"
}

write_unit velaops-proxy.service <<EOF
[Unit]
Description=VelaOps authenticated device proxy
Wants=network-online.target
After=network-online.target

[Service]
Type=simple
WorkingDirectory=$team_root
Environment="PYTHONPATH=$team_root/proxy/src"
ExecStart=/usr/bin/python3 -u -m velaops_proxy --config $state_dir/proxy.json
Restart=always
RestartSec=2
NoNewPrivileges=true

[Install]
WantedBy=default.target
EOF

write_unit velaops-lan-ntp.service <<EOF
[Unit]
Description=VelaOps LAN time service
Wants=network-online.target
After=network-online.target

[Service]
Type=simple
WorkingDirectory=$team_root
Environment="PYTHONPATH=$team_root/proxy/src"
ExecStart=/usr/bin/python3 -u -m velaops_proxy.lan_ntp --port 40123
Restart=always
RestartSec=2
NoNewPrivileges=true

[Install]
WantedBy=default.target
EOF

write_unit velaops-llm-forwarder.service <<EOF
[Unit]
Description=VelaOps restricted MiMo forwarder
Wants=network-online.target
After=network-online.target

[Service]
Type=simple
WorkingDirectory=$team_root
EnvironmentFile=$state_dir/forwarder.env
Environment="BIND_HOST=$HOST_IP"
Environment="ALLOWED_CLIENT=$BOARD_IP"
ExecStart=/usr/bin/python3 -u $team_root/docs/tools/llm_forwarder.py 28792
Restart=always
RestartSec=2
NoNewPrivileges=true

[Install]
WantedBy=default.target
EOF

# 演示目标是可被主动停止的故障注入点，因此 Restart=no。
write_unit velaops-demo-target.service <<EOF
[Unit]
Description=VelaOps disposable demo target

[Service]
Type=simple
ExecStart=/usr/bin/python3 -u $team_root/docs/tools/demo_target_app.py 28791
Restart=no

[Install]
WantedBy=default.target
EOF

systemctl --user daemon-reload
systemctl --user enable --now \
  velaops-proxy.service velaops-lan-ntp.service \
  velaops-llm-forwarder.service velaops-demo-target.service

# linger：注销/重启后仍运行；需要一次 polkit 授权，失败时给出提示。
if ! loginctl enable-linger "$USER" 2>/dev/null; then
  echo "提示：无法开启 linger，请手动执行 loginctl enable-linger $USER" >&2
fi

echo "VelaOps 演示服务已安装：HOST_IP=$HOST_IP BOARD_IP=$BOARD_IP"
systemctl --user is-active \
  velaops-proxy velaops-lan-ntp velaops-llm-forwarder velaops-demo-target
