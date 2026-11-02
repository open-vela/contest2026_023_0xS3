#!/usr/bin/env bash
# Apply VelaOps patches to the openvela workspace.
set -uo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
team_root=$(cd "$script_dir/../.." && pwd)
workspace_root=$(cd "$team_root/.." && pwd)

apply_patch_dir() {
  local patch_dir="$1"
  local target_repo="$2"
  local patch_file name

  [ -d "$patch_dir" ] || return 0
  for patch_file in "$patch_dir"/*.patch; do
    [ -e "$patch_file" ] || continue
    name=$(basename "$patch_file")
    if git -C "$target_repo" apply --check --reverse "$patch_file" 2>/dev/null; then
      echo "$name already applied"
      continue
    fi
    if git -C "$target_repo" apply --check "$patch_file" 2>/dev/null; then
      git -C "$target_repo" apply "$patch_file"
      echo "Applied $name"
    else
      echo "WARN: $name 无法应用，请人工确认" >&2
    fi
  done
}

apply_patch_dir "$team_root/patches/nuttx" "$workspace_root/nuttx"
apply_patch_dir "$team_root/patches/apps" "$workspace_root/apps"
