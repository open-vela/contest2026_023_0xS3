"""适用于单进程 P0 的线程安全内存适配器。"""

from __future__ import annotations

from threading import RLock
from typing import Mapping

from ..ports import ReplayStoreError


MIN_SECRET_BYTES = 32
DEFAULT_MAX_REPLAY_ENTRIES = 4096


class InMemoryDeviceSecretStore:
    """线程安全的设备密钥表；后续可替换为受保护配置存储。"""

    def __init__(self, secrets: Mapping[str, bytes] | None = None) -> None:
        self._lock = RLock()
        self._secrets: dict[str, bytes] = {}
        for device_id, secret in (secrets or {}).items():
            self.set_secret(device_id, secret)

    def set_secret(self, device_id: str, secret: bytes) -> None:
        if not device_id:
            raise ValueError("device_id 不能为空")
        if len(secret) < MIN_SECRET_BYTES:
            raise ValueError("设备密钥不能小于 32 字节")
        with self._lock:
            self._secrets[device_id] = bytes(secret)

    def revoke(self, device_id: str) -> None:
        with self._lock:
            self._secrets.pop(device_id, None)

    def get_secret(self, device_id: str) -> bytes | None:
        with self._lock:
            secret = self._secrets.get(device_id)
            return None if secret is None else bytes(secret)


class InMemoryReplayStore:
    """有界、线程安全的 nonce 存储，登记操作在锁内保持原子性。"""

    # 串口隧道在 ACK 丢失时会重发同一请求（同 nonce）。若一律拒绝，重发会被判
    # replay，导致"服务其实已执行却报失败"。这里对短时间内（<=窗口）的同 nonce
    # 重发予以放行——执行侧仍按 request_id 幂等，重复请求只会拿到缓存结果，不会
    # 重复执行；只读 Action 重复执行也无副作用。
    RETRANSMIT_TOLERANCE_SECONDS = 40

    def __init__(self, max_entries: int = DEFAULT_MAX_REPLAY_ENTRIES) -> None:
        if max_entries <= 0:
            raise ValueError("max_entries 必须大于零")
        self._max_entries = max_entries
        self._lock = RLock()
        self._entries: dict[tuple[str, str], tuple[int, int]] = {}

    def claim(
        self,
        device_id: str,
        nonce: str,
        *,
        expires_at: int,
        now: int,
    ) -> bool:
        key = (device_id, nonce)
        with self._lock:
            # expires_at 使用开区间：到期秒开始即可清理。
            expired = [
                item
                for item, (_expiry, _seen) in self._entries.items()
                if _expiry <= now
            ]
            for item in expired:
                del self._entries[item]

            existing = self._entries.get(key)
            if existing is not None:
                _expiry, first_seen = existing
                if now - first_seen <= self.RETRANSMIT_TOLERANCE_SECONDS:
                    return True  # 视为同一请求的重发，允许
                return False
            if len(self._entries) >= self._max_entries:
                raise ReplayStoreError("nonce 存储容量已满")

            self._entries[key] = (expires_at, now)
            return True

    def __len__(self) -> int:
        with self._lock:
            return len(self._entries)
