"""认证用例依赖的端口接口。"""

from __future__ import annotations

from typing import Protocol


class DeviceSecretStore(Protocol):
    """按设备 ID 查询独立 HMAC 密钥。"""

    def get_secret(self, device_id: str) -> bytes | None:
        """设备不存在或已吊销时返回 None。"""
        ...


class ReplayStoreError(RuntimeError):
    """nonce 存储不可用或容量耗尽。"""


class ReplayStore(Protocol):
    """原子登记已通过签名验证的 nonce。"""

    def claim(
        self,
        device_id: str,
        nonce: str,
        *,
        expires_at: int,
        now: int,
    ) -> bool:
        """首次登记返回 True；nonce 已存在返回 False；故障时抛出异常。"""
        ...


class Clock(Protocol):
    """提供可替换的 Unix 秒时钟，便于边界测试。"""

    def now_seconds(self) -> int:
        ...
