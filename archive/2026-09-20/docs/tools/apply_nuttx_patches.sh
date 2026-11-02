#!/usr/bin/env bash
# 可重复执行：已应用的补丁自动跳过；无法确认的补丁只告警不中断，
# 避免某个仓的历史状态拖垮整套环境准备。
set -uo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
team_root=$(cd "$script_dir/../.." && pwd)
workspace_root=$(cd "$team_root/.." && pwd)

apply_patch_dir() {
  local patch_dir="$1"
  local target_repo="$2"
  local patch_file name
  [ -d "$patch_dir" ] || return 0
  # 按文件名顺序应用补丁；已应用的补丁（反向检查通过）自动跳过。
  for patch_file in "$patch_dir"/*.patch; do
    [ -e "$patch_file" ] || continue
    name=$(basename "$patch_file")
    if git -C "$target_repo" apply --check --reverse "$patch_file" \
        2>/dev/null; then
      echo "$name already applied"
      continue
    fi

    if git -C "$target_repo" apply --check "$patch_file" 2>/dev/null; then
      git -C "$target_repo" apply "$patch_file"
      echo "Applied $name"
    else
      echo "WARN: $name 无法应用（可能已被等效修改），请人工确认" >&2
    fi
  done
}

# patches/nuttx：打到 NuttX 主仓（SD 卡写路径修复、ST7789 字节序等）。
apply_patch_dir "$team_root/patches/nuttx" "$workspace_root/nuttx"
# patches/ai_agent：打到 ai_agent 仓（ask 文件队列通道、Agent 回发钩子、
# LLM 请求与板端隧道共用全局锁）。上游保持 0 改动，全部在编译前打补丁。
apply_patch_dir "$team_root/patches/ai_agent" "$workspace_root/packages/ai_agent"
# patches/vendor：打到板级 vendor 仓（bringup 自启、defconfig）。
# 注意：真实编译的 bringup 是 vendor 仓 esp32s3-eye/src/esp32s3_bringup.c
# （nuttx boards common/ 下是符号链接），nuttx 仓 esp32s3-eye/src/ 的同名副本从不参与编译。
apply_patch_dir "$team_root/patches/vendor" "$workspace_root/vendor/espressif"
