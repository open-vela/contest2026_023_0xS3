#!/usr/bin/env bash
# 在 NSH 提示符下安装队伍 Skill 并注册只读 Tool Provider。
# 本脚本不读取、不传输任何 API Key 或 Proxy 密钥。

set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
skill_file="$repo_dir/app/hello_app/skills/server-incident-response.md"

send_slow()
{
  DRAIN=2 python3 "$script_dir/send_slow.py" "$@"
}

send_slow mkdir /data/ai_agent
send_slow mkdir /data/ai_agent/skills

# 先传到短路径，让每条 79 字符 NSH 命令容纳更多数据块；
# 全部传输成功后再原子替换目标文件，避免 Agent 读到半个 Skill。

CHAR_DELAY=${CHAR_DELAY:-0.002} \
  python3 "$script_dir/serial_push.py" "$skill_file" /data/s
send_slow mv /data/s /data/ai_agent/skills/server-incident-response.md
DRAIN=3 python3 "$script_dir/send_slow.py" \
  ls -l /data/ai_agent/skills/server-incident-response.md
DRAIN=3 python3 "$script_dir/send_slow.py" velaops agent-install

echo "AI Agent assets provisioned"
