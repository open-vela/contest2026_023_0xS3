"""变更 Action 的 SQLite 持久化幂等存储。"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
import json
import os
from pathlib import Path
import sqlite3
import stat
from threading import RLock
from typing import Any, Mapping

from .actions import ActionError, ActionResult
from .protocol import ErrorCode


class ClaimState(str, Enum):
    NEW = "new"
    CACHED = "cached"


@dataclass(frozen=True, slots=True)
class ExecutionClaim:
    state: ClaimState
    cached_result: ActionResult | None = None


class SqliteExecutionStore:
    """对每台设备的 request_id 只允许一次变更执行。"""

    def __init__(self, path: str | os.PathLike[str]) -> None:
        self._path = Path(path)
        self._lock = RLock()
        self._prepare_file()
        self._connection = sqlite3.connect(
            self._path,
            timeout=5,
            isolation_level=None,
            check_same_thread=False,
        )
        self._connection.execute("PRAGMA journal_mode=WAL")
        self._connection.execute("PRAGMA synchronous=FULL")
        self._connection.execute(
            """
            CREATE TABLE IF NOT EXISTS action_executions (
                device_id TEXT NOT NULL,
                request_id TEXT NOT NULL,
                body_sha256 TEXT NOT NULL,
                approval_id TEXT,
                state TEXT NOT NULL CHECK (state IN ('running', 'succeeded', 'failed')),
                result_json TEXT,
                error_code TEXT,
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL,
                PRIMARY KEY (device_id, request_id)
            )
            """
        )
        columns = {
            row[1]
            for row in self._connection.execute(
                "PRAGMA table_info(action_executions)"
            ).fetchall()
        }
        if "approval_id" not in columns:
            self._connection.execute(
                "ALTER TABLE action_executions ADD COLUMN approval_id TEXT"
            )
        self._connection.execute(
            """
            CREATE UNIQUE INDEX IF NOT EXISTS action_executions_approval
            ON action_executions (device_id, approval_id)
            WHERE approval_id IS NOT NULL
            """
        )

    def begin(
        self,
        device_id: str,
        request_id: str,
        body_sha256: str,
        now: int,
        approval_id: str | None = None,
    ) -> ExecutionClaim:
        with self._lock:
            self._connection.execute("BEGIN IMMEDIATE")
            try:
                row = self._connection.execute(
                    """
                    SELECT body_sha256, state, result_json
                    FROM action_executions
                    WHERE device_id = ? AND request_id = ?
                    """,
                    (device_id, request_id),
                ).fetchone()
                if row is None:
                    self._connection.execute(
                        """
                        INSERT INTO action_executions
                            (device_id, request_id, body_sha256, approval_id,
                             state, created_at, updated_at)
                        VALUES (?, ?, ?, ?, 'running', ?, ?)
                        """,
                        (device_id, request_id, body_sha256, approval_id, now, now),
                    )
                    self._connection.execute("COMMIT")
                    return ExecutionClaim(ClaimState.NEW)
                claim = self._existing_claim(row, body_sha256)
                self._connection.execute("COMMIT")
                return claim
            except sqlite3.IntegrityError as exc:
                if self._connection.in_transaction:
                    self._connection.execute("ROLLBACK")
                raise ActionError(
                    ErrorCode.APPROVAL_REUSED,
                    "该实体批准已用于其他变更",
                ) from exc
            except Exception:
                if self._connection.in_transaction:
                    self._connection.execute("ROLLBACK")
                raise

    def lookup(
        self,
        device_id: str,
        request_id: str,
        body_sha256: str,
    ) -> ExecutionClaim | None:
        """在批准过期后仍可查询既有成功结果，但不创建执行占位。"""
        with self._lock:
            row = self._connection.execute(
                """
                SELECT body_sha256, state, result_json
                FROM action_executions
                WHERE device_id = ? AND request_id = ?
                """,
                (device_id, request_id),
            ).fetchone()
            if row is None:
                return None
            return self._existing_claim(row, body_sha256)

    def succeed(
        self,
        device_id: str,
        request_id: str,
        result: ActionResult,
        now: int,
    ) -> None:
        payload = json.dumps(
            _json_value(result.data), ensure_ascii=False, separators=(",", ":")
        )
        self._finish(device_id, request_id, "succeeded", payload, None, now)

    def fail(
        self,
        device_id: str,
        request_id: str,
        error_code: ErrorCode,
        now: int,
    ) -> None:
        self._finish(device_id, request_id, "failed", None, error_code.value, now)

    def close(self) -> None:
        with self._lock:
            self._connection.close()

    def _finish(
        self,
        device_id: str,
        request_id: str,
        state: str,
        result_json: str | None,
        error_code: str | None,
        now: int,
    ) -> None:
        with self._lock:
            cursor = self._connection.execute(
                """
                UPDATE action_executions
                SET state = ?, result_json = ?, error_code = ?, updated_at = ?
                WHERE device_id = ? AND request_id = ? AND state = 'running'
                """,
                (state, result_json, error_code, now, device_id, request_id),
            )
            if cursor.rowcount != 1:
                raise RuntimeError("幂等记录状态转换失败")

    @staticmethod
    def _existing_claim(
        row: tuple[str, str, str | None], body_sha256: str
    ) -> ExecutionClaim:
        stored_hash, state, result_json = row
        if stored_hash != body_sha256:
            raise ActionError(
                ErrorCode.IDEMPOTENCY_CONFLICT,
                "request_id 已绑定不同请求",
            )
        if state == "succeeded" and result_json is not None:
            data = json.loads(result_json)
            return ExecutionClaim(ClaimState.CACHED, ActionResult.of(**data))
        raise ActionError(
            ErrorCode.ACTION_STATE_UNCERTAIN,
            "该变更曾开始执行，禁止自动重放",
        )

    def _prepare_file(self) -> None:
        parent = self._path.parent
        try:
            parent_info = os.stat(parent, follow_symlinks=False)
        except OSError as exc:
            raise ValueError("幂等数据库父目录不存在") from exc
        if (
            not stat.S_ISDIR(parent_info.st_mode)
            or parent_info.st_uid != os.geteuid()
            or stat.S_IMODE(parent_info.st_mode) & 0o022
        ):
            raise ValueError("幂等数据库父目录必须由当前用户拥有且不可被其他用户写入")
        flags = os.O_RDWR | os.O_CREAT | os.O_CLOEXEC
        if hasattr(os, "O_NOFOLLOW"):
            flags |= os.O_NOFOLLOW
        try:
            fd = os.open(self._path, flags, 0o600)
        except OSError as exc:
            raise ValueError("无法安全打开幂等数据库") from exc
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid():
                raise ValueError("幂等数据库必须是当前用户拥有的普通文件")
            if stat.S_IMODE(info.st_mode) & 0o077:
                raise ValueError("幂等数据库不得向组或其他用户开放")
        finally:
            os.close(fd)


def _json_value(value: Any) -> Any:
    if isinstance(value, Mapping):
        return {key: _json_value(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_json_value(item) for item in value]
    return value
