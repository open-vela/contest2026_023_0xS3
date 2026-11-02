"""Proxy 受保护的结构化 JSONL 审计。"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from enum import Enum
import json
import os
from pathlib import Path
import re
import stat
from threading import RLock
from typing import Protocol

from .protocol import ErrorCode


_ID_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}\Z")
_REQUEST_ID_PATTERN = re.compile(r"[0-9a-f]{32}\Z")
_ACTION_PATTERN = re.compile(r"[a-z][a-z0-9_]{0,63}\Z")


class AuditEventType(str, Enum):
    AUTH_FAILED = "auth_failed"
    ACTION_STARTED = "action_started"
    ACTION_SUCCEEDED = "action_succeeded"
    ACTION_FAILED = "action_failed"


@dataclass(frozen=True, slots=True)
class AuditEvent:
    timestamp: int
    event_type: AuditEventType
    device_id: str | None = None
    request_id: str | None = None
    target: str | None = None
    action: str | None = None
    risk: str | None = None
    error_code: ErrorCode | None = None

    def validate(self) -> None:
        if type(self.timestamp) is not int or self.timestamp < 0:
            raise ValueError("审计时间不合法")
        for name, value in (("device_id", self.device_id), ("target", self.target)):
            if value is not None and not _ID_PATTERN.fullmatch(value):
                raise ValueError(f"审计 {name} 不合法")
        if self.request_id is not None and not _REQUEST_ID_PATTERN.fullmatch(
            self.request_id
        ):
            raise ValueError("审计 request_id 不合法")
        if self.action is not None and not _ACTION_PATTERN.fullmatch(self.action):
            raise ValueError("审计 action 不合法")
        if self.risk not in {None, "read_only", "change"}:
            raise ValueError("审计 risk 不合法")

    def to_json_bytes(self) -> bytes:
        self.validate()
        data = asdict(self)
        data["event_type"] = self.event_type.value
        data["error_code"] = (
            None if self.error_code is None else self.error_code.value
        )
        return (
            json.dumps(data, ensure_ascii=False, separators=(",", ":")) + "\n"
        ).encode("utf-8")


class AuditSink(Protocol):
    def record(self, event: AuditEvent) -> None: ...


class JsonlAuditSink:
    """单条追加并同步落盘，适用于 P0 单进程 Proxy。"""

    def __init__(self, path: str | os.PathLike[str]) -> None:
        self._path = Path(path)
        try:
            parent_info = os.stat(self._path.parent, follow_symlinks=False)
        except OSError as exc:
            raise ValueError("审计文件父目录不存在") from exc
        if (
            not stat.S_ISDIR(parent_info.st_mode)
            or parent_info.st_uid != os.geteuid()
            or stat.S_IMODE(parent_info.st_mode) & 0o022
        ):
            raise ValueError("审计父目录必须由当前用户拥有且不可被其他用户写入")
        flags = os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_CLOEXEC
        if hasattr(os, "O_NOFOLLOW"):
            flags |= os.O_NOFOLLOW
        try:
            self._fd = os.open(self._path, flags, 0o600)
        except OSError as exc:
            raise ValueError("无法安全打开审计文件") from exc
        try:
            info = os.fstat(self._fd)
            if not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid():
                raise ValueError("审计文件必须是当前用户拥有的普通文件")
            if stat.S_IMODE(info.st_mode) & 0o077:
                raise ValueError("审计文件不得向组或其他用户开放")
        except Exception:
            os.close(self._fd)
            raise
        self._lock = RLock()

    def record(self, event: AuditEvent) -> None:
        payload = event.to_json_bytes()
        with self._lock:
            view = memoryview(payload)
            while view:
                written = os.write(self._fd, view)
                if written <= 0:
                    raise OSError("审计文件写入失败")
                view = view[written:]
            os.fsync(self._fd)

    def close(self) -> None:
        with self._lock:
            os.close(self._fd)
