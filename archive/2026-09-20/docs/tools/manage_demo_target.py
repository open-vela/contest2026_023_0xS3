#!/usr/bin/env python3
"""管理无特权的 VelaOps user-systemd 演示目标服务。"""

from __future__ import annotations

import argparse
from pathlib import Path
import socket
import subprocess
import time
from typing import Callable, Sequence

from prepare_live_demo import DEFAULT_DIRECTORY, DEFAULT_TARGET_PORT, TARGET_UNIT


Runner = Callable[..., subprocess.CompletedProcess[str]]


def _wait_for_target(timeout_seconds: float = 5.0) -> None:
    """等待目标真正接受连接，避免把 systemd 已启动误当成服务已就绪。"""
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(
                ("127.0.0.1", DEFAULT_TARGET_PORT), timeout=0.25
            ):
                return
        except OSError:
            time.sleep(0.05)
    raise TimeoutError("演示目标未在限定时间内就绪")


def manage(action: str, directory: Path, *, runner: Runner = subprocess.run) -> None:
    unit_path = directory / TARGET_UNIT
    if not unit_path.is_file() or unit_path.is_symlink():
        raise ValueError("演示 unit 不存在或不是普通文件，请先生成 Demo 配置")

    def run(arguments: Sequence[str]) -> None:
        runner(
            ["systemctl", "--user", *arguments],
            check=True,
            text=True,
        )

    if action == "start":
        # /tmp 配置重建后旧链接会悬空；仅覆盖这个固定 unit 名称。
        run(["link", "--force", str(unit_path.resolve())])
        run(["daemon-reload"])
        run(["start", TARGET_UNIT])
        _wait_for_target()
    elif action == "stop":
        run(["stop", TARGET_UNIT])
    elif action == "status":
        run(["status", "--no-pager", TARGET_UNIT])
    else:
        raise ValueError("不支持的演示目标操作")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("start", "stop", "status"))
    parser.add_argument("--directory", type=Path, default=DEFAULT_DIRECTORY)
    arguments = parser.parse_args()
    manage(arguments.action, arguments.directory)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
