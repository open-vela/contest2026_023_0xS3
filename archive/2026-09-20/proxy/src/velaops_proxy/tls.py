"""Proxy TLS 服务端上下文与私钥文件安全检查。"""

from __future__ import annotations

import os
from pathlib import Path
import ssl
import stat

from .config import TlsConfig


def create_tls_context(config: TlsConfig) -> ssl.SSLContext:
    _validate_regular_file(config.certificate, private=False)
    _validate_regular_file(config.private_key, private=True)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.options |= ssl.OP_NO_COMPRESSION
    context.load_cert_chain(config.certificate, config.private_key)
    return context


def _validate_regular_file(path: str, *, private: bool) -> None:
    flags = os.O_RDONLY | os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(Path(path), flags)
    except OSError as exc:
        raise ValueError("TLS 文件无法安全打开") from exc
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise ValueError("TLS 路径必须是普通文件")
        if info.st_size <= 0 or info.st_size > 1024 * 1024:
            raise ValueError("TLS 文件大小不合法")
        if private:
            if info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) & 0o077:
                raise ValueError("TLS 私钥必须由服务用户独占")
    finally:
        os.close(fd)
