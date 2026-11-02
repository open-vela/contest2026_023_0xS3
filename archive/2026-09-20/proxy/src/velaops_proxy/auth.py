"""Proxy 请求认证与防重放用例。"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Mapping

from .ports import Clock, DeviceSecretStore, ReplayStore, ReplayStoreError
from .protocol import (
    AuthMetadata,
    ErrorCode,
    ProtocolValidationError,
    verify_signature,
)


DEFAULT_ALLOWED_CLOCK_SKEW_SECONDS = 300
_DUMMY_SECRET = bytes(32)


class AuthenticationError(RuntimeError):
    """认证失败；公开响应统一，详细原因只供受保护审计使用。"""

    public_code = ErrorCode.INVALID_AUTH

    def __init__(self, audit_code: ErrorCode):
        super().__init__("请求认证失败")
        self.audit_code = audit_code


@dataclass(frozen=True, slots=True)
class AuthenticatedRequest:
    """已完成签名、时间窗和防重放校验的请求身份。"""

    metadata: AuthMetadata


class RequestAuthenticator:
    """编排设备密钥查询、签名、时间窗和 nonce 原子登记。"""

    def __init__(
        self,
        secrets: DeviceSecretStore,
        replay_store: ReplayStore,
        clock: Clock,
        *,
        allowed_clock_skew_seconds: int = DEFAULT_ALLOWED_CLOCK_SKEW_SECONDS,
    ) -> None:
        if allowed_clock_skew_seconds < 0:
            raise ValueError("允许的时钟偏差不能为负数")
        self._secrets = secrets
        self._replay_store = replay_store
        self._clock = clock
        self._allowed_skew = allowed_clock_skew_seconds

    def authenticate(
        self,
        headers: Mapping[str, str],
        method: str,
        target: str,
        body: bytes,
    ) -> AuthenticatedRequest:
        try:
            metadata, signature = AuthMetadata.from_headers(headers)
        except ProtocolValidationError as exc:
            raise AuthenticationError(exc.code) from exc

        secret = self._secrets.get_secret(metadata.device_id)
        if secret is None:
            unknown_device = True
            verification_secret = _DUMMY_SECRET
        else:
            unknown_device = False
            verification_secret = secret

        try:
            signature_valid = verify_signature(
                signature, verification_secret, method, target, body, metadata
            )
        except ProtocolValidationError as exc:
            raise AuthenticationError(exc.code) from exc
        # 未知设备也完成同一类 HMAC 运算，降低按响应耗时枚举设备 ID 的信号。
        if unknown_device:
            raise AuthenticationError(ErrorCode.UNKNOWN_DEVICE)
        if not signature_valid:
            raise AuthenticationError(ErrorCode.SIGNATURE_MISMATCH)

        now = self._clock.now_seconds()
        if abs(metadata.timestamp - now) > self._allowed_skew:
            raise AuthenticationError(ErrorCode.STALE_TIMESTAMP)

        # 保留到该请求时间戳的最晚可接受时刻之后，边界秒也不能重放。
        expires_at = metadata.timestamp + self._allowed_skew + 1
        try:
            claimed = self._replay_store.claim(
                metadata.device_id,
                metadata.nonce,
                expires_at=expires_at,
                now=now,
            )
        except ReplayStoreError as exc:
            # nonce 存储故障时必须关闭执行路径，不能降级为跳过防重放。
            raise AuthenticationError(ErrorCode.INTERNAL_ERROR) from exc
        if not claimed:
            raise AuthenticationError(ErrorCode.REPLAY_DETECTED)

        return AuthenticatedRequest(metadata=metadata)
