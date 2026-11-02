#!/usr/bin/env python3
"""通过 NSH 串口安全推送小型配置文件，不输出文件内容。"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import select
import termios
import time


DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_MAX_LINE = 79
# 默认保持最稳妥的 10ms；只在短路径、无凭据的大文本资源
# 下由调试者显式调小。单条命令仍受 79 字符上限保护。
DEFAULT_CHAR_DELAY = float(os.environ.get("CHAR_DELAY", "0.01"))
MAX_FILE_SIZE = 16 * 1024
_REMOTE_PATH = re.compile(r"/[A-Za-z0-9._/-]+\Z")


def build_commands(data: bytes, remote_path: str,
                   max_line: int = DEFAULT_MAX_LINE) -> list[str]:
    """把文件拆成 NSH printf 命令，每行不超过固件的行长限制。"""
    if not _REMOTE_PATH.fullmatch(remote_path) or ".." in remote_path.split("/"):
        raise ValueError("远程路径不合法")
    if not data or len(data) > MAX_FILE_SIZE:
        raise ValueError("文件大小必须在 1 到 16384 字节之间")

    truncate = f"rm -f {remote_path}"
    if len(truncate) > max_line:
        raise ValueError("远程路径过长")

    commands = [truncate]
    # NuttX NSH printf 不是 POSIX printf：每个 \x 参数最多按小端序输出
    # 4 字节，且 CONFIG_NSH_MAXARGUMENTS=7（含命令本身）。
    groups: list[bytes] = []
    offset = 0
    while len(data) - offset >= 4:
        groups.append(data[offset:offset + 4])
        offset += 4
    remaining = len(data) - offset
    if remaining >= 2:
        groups.append(data[offset:offset + 2])
        offset += 2
    if offset < len(data):
        groups.append(data[offset:offset + 1])

    arguments: list[str] = []
    for group in groups:
        # NSH 将十六进制整数按小端序输出，所以文本字节顺序需反转。
        argument = "'\\x" + bytes(reversed(group)).hex() + "'"
        candidate = f"printf {' '.join(arguments + [argument])} >> {remote_path}"
        if arguments and (len(arguments) >= 6 or len(candidate) > max_line):
            commands.append(f"printf {' '.join(arguments)} >> {remote_path}")
            arguments = [argument]
        else:
            arguments.append(argument)
    if arguments:
        commands.append(f"printf {' '.join(arguments)} >> {remote_path}")

    if any(len(command) > max_line for command in commands):
        raise ValueError("远程路径过长")
    return commands


def _open_raw_tty(port: str) -> int:
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attributes = termios.tcgetattr(fd)
    attributes[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
                       termios.ISTRIP | termios.INLCR | termios.IGNCR |
                       termios.ICRNL | termios.IXON)
    attributes[1] &= ~termios.OPOST
    attributes[2] &= ~(termios.CSIZE | termios.PARENB)
    attributes[2] |= termios.CS8
    attributes[3] &= ~(termios.ICANON | termios.ECHO | termios.ECHOE |
                       termios.ISIG)
    attributes[6][termios.VMIN] = 0
    attributes[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attributes)
    return fd


def _drain(fd: int) -> None:
    deadline = time.monotonic() + 0.3
    while time.monotonic() < deadline:
        readable, _, _ = select.select([fd], [], [], 0.05)
        if readable:
            try:
                os.read(fd, 4096)
            except BlockingIOError:
                pass


def _write_byte(fd: int, value: bytes) -> None:
    """Write one byte to a raw non-blocking tty, retrying EAGAIN."""
    while True:
        try:
            if os.write(fd, value) == len(value):
                return
        except BlockingIOError:
            pass
        select.select([], [fd], [], 0.5)


def _send_command(fd: int, command: str, timeout: float,
                  char_delay: float = DEFAULT_CHAR_DELAY) -> None:
    for character in command:
        _write_byte(fd, character.encode("ascii"))
        time.sleep(char_delay)
    _write_byte(fd, b"\n")

    output = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        readable, _, _ = select.select([fd], [], [], 0.2)
        if not readable:
            continue
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            continue
        output.extend(chunk)
        # 看板每 5 秒打印长行，提示符只会在最新的输出末尾出现，
        # 只检查尾部避免历史输出误判。AI Agent 启动后提示符变为 "vela> "，
        # 两者都要接受，否则 agent 运行时推文件会永远超时。
        if b"nsh> " in output[-64:] or b"vela> " in output[-64:]:
            if b"ERROR:" in output:
                raise RuntimeError("NSH 拒绝了文件写入命令")
            return
    raise TimeoutError("等待 NSH 提示符超时")


def push_file(local_path: Path, remote_path: str, port: str) -> None:
    data = local_path.read_bytes()
    commands = build_commands(data, remote_path)
    # 看板监测运行时每 5 秒打印长行，提示符等待窗口可通过环境变量放宽。
    timeout = float(os.environ.get("PUSH_PROMPT_TIMEOUT", "5.0"))
    fd = _open_raw_tty(port)
    try:
        _drain(fd)
        for command in commands:
            _send_command(fd, command, timeout=timeout)
    finally:
        os.close(fd)
    print(f"PUSHED: {local_path.name} -> {remote_path} ({len(data)} bytes)")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="通过 NSH 串口推送小文件（不打印内容）")
    parser.add_argument("local_file", type=Path)
    parser.add_argument("remote_file")
    parser.add_argument("--port", default=os.environ.get("PORT", DEFAULT_PORT))
    arguments = parser.parse_args()
    push_file(arguments.local_file, arguments.remote_file, arguments.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
