#!/usr/bin/env bash
# 两步法闭环编排 v2：持久串口日志 + 步骤①诊断 + 步骤②修复。
set -u
cd /home/lu/桌面/openvela/contest2026_023_0xS3 || exit 1
LOG=/tmp/boot_watch.log
say() { echo "[$(date +%H:%M:%S)] $*"; }

: > "$LOG"
nohup env PORT=/dev/ttyACM0 python3 docs/tools/boot_watch.py >/dev/null 2>&1 &
WATCH_PID=$!
sleep 1
say "watcher pid=$WATCH_PID alive=$(kill -0 $WATCH_PID 2>/dev/null && echo yes || echo no)"

say "send step1"
PORT=/dev/ttyACM0 DRAIN=1 python3 docs/tools/send_slow.py "ask 检查demo服务状态" >/dev/null 2>&1
say "step1 bytes sent"

# 等步骤①：先出现工具调用，再出现诊断 JSON（recommended_action），最多 260s
for i in $(seq 1 130); do
  if grep -aq "Tool call: velaops_check_resources" "$LOG" && grep -aq "recommended_action" "$LOG"; then
    say "step1 diagnosis DONE"
    break
  fi
  sleep 2
done

sleep 5
say "send step2"
PORT=/dev/ttyACM0 DRAIN=1 python3 docs/tools/send_slow.py "ask 我确认执行修复，请立即调用velaops_restart_service工具重启demo服务" >/dev/null 2>&1
say "step2 bytes sent"

# 等步骤②：出现修复结果（最多 320s）
for i in $(seq 1 160); do
  if grep -aq "execution_state" "$LOG"; then
    say "step2 RESULT APPEARED"
    break
  fi
  sleep 2
done
sleep 10
say "orchestrate v2 done"
exit 0
