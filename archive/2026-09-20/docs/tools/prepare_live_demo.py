#!/usr/bin/env python3
"""原子生成本机 Demo 的成对 Proxy/设备配置，不输出任何密钥。"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import secrets
import tempfile
from typing import Any


# 与 user-systemd 服务使用的持久目录保持一致：/tmp 会在开发机重启时被清空，
# 若生成到 /tmp 而服务仍读旧目录，设备与 Proxy 的配对密钥就会不一致而认证失败。
DEFAULT_DIRECTORY = Path.home() / ".local/state/velaops-live"
DEFAULT_PORT = 28790
DEFAULT_TARGET_PORT = 28791
DEVICE_ID = "eye-001"
TARGET_UNIT = "velaops-demo-target.service"


def _atomic_private_json(path: Path, document: dict[str, Any]) -> None:
    """同目录写临时文件再替换，目标始终只允许当前用户读写。"""
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        os.fchmod(fd, 0o600)
        stream = os.fdopen(fd, "w", encoding="utf-8", closefd=True)
        fd = -1
        with stream:
            json.dump(document, stream, ensure_ascii=False, separators=(",", ":"))
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        os.chmod(path, 0o600)
    finally:
        if fd >= 0:
            os.close(fd)
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def _atomic_private_text(path: Path, content: str) -> None:
    """以与私密配置相同的原子方式写入演示 unit。"""
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        os.fchmod(fd, 0o600)
        stream = os.fdopen(fd, "w", encoding="utf-8", closefd=True)
        fd = -1
        with stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        os.chmod(path, 0o600)
    finally:
        if fd >= 0:
            os.close(fd)
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def prepare(directory: Path, host: str, port: int, *, force: bool = False) -> None:
    if not host or not host.isascii() or any(character.isspace() for character in host):
        raise ValueError("host 必须是非空 ASCII 地址")
    if not 1 <= port <= 65535:
        raise ValueError("port 必须在 1..65535")
    if directory.is_symlink():
        raise ValueError("输出目录不能是符号链接")

    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(directory, 0o700)
    state_directory = directory / "state"
    log_directory = directory / "logs"
    for child in (state_directory, log_directory):
        if child.is_symlink():
            raise ValueError("状态目录不能是符号链接")
        child.mkdir(mode=0o700, exist_ok=True)
        os.chmod(child, 0o700)

    proxy_path = directory / "proxy.json"
    device_path = directory / "device-config.json"
    unit_path = directory / TARGET_UNIT
    if not force and any(path.exists() for path in (proxy_path, device_path, unit_path)):
        raise FileExistsError("配置已存在；如需轮换密钥请显式使用 --force")

    # 设备端配置使用可安全逐字符下发的 32 字节 ASCII 密钥；Proxy 保存其原始
    # 字节的十六进制形式，两边不会出现编码歧义。
    device_secret = secrets.token_hex(16)
    proxy_config = {
        "listen": {"host": host, "port": port},
        "devices": {DEVICE_ID: {"secret_hex": device_secret.encode().hex()}},
        "targets": {
            "local-dev": {
                "allowed_actions": [
                    "check_memory",
                    "check_resources",
                    "restart_service",
                ],
                "services": {
                    "demo": {
                        "unit": TARGET_UNIT,
                        "manager": "user",
                        "restart_via_sudo": False,
                    }
                },
                "ports": {
                    "demo-http": {"host": "127.0.0.1", "port": DEFAULT_TARGET_PORT}
                },
                "disks": {"root": "/"},
            }
        },
        "storage": {
            "state_database": str(state_directory / "state.db"),
            "audit_log": str(log_directory / "audit.jsonl"),
        },
        "allow_insecure_http": True,
    }
    device_config = {
        "schema_version": 1,
        "host": host,
        "port": str(port),
        "device_id": DEVICE_ID,
        "secret": device_secret,
    }

    _atomic_private_json(proxy_path, proxy_config)
    _atomic_private_json(device_path, device_config)
    _atomic_private_text(
        unit_path,
        "[Unit]\n"
        "Description=VelaOps disposable demo target\n\n"
        "[Service]\n"
        "Type=simple\n"
        f"ExecStart=/usr/bin/python3 -m http.server {DEFAULT_TARGET_PORT} "
        "--bind 127.0.0.1\n"
        "Restart=no\n",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="开发机局域网 IPv4 地址")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--directory", type=Path, default=DEFAULT_DIRECTORY)
    parser.add_argument("--force", action="store_true", help="轮换并覆盖现有配对密钥")
    arguments = parser.parse_args()
    prepare(arguments.directory, arguments.host, arguments.port,
            force=arguments.force)
    print(f"Demo configs ready: {arguments.directory} (device={DEVICE_ID})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
