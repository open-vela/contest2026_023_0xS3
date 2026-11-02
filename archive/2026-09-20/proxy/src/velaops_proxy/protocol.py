"""VelaOps HMAC v1 协议的语言无关核心。"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
import hashlib
import hmac
import re
from typing import Mapping


ALGORITHM = "VELAOPS-HMAC-SHA256"
PROTOCOL_VERSION = "1"
MAX_TARGET_LENGTH = 2048

HEADER_VERSION = "X-VelaOps-Version"
HEADER_DEVICE_ID = "X-VelaOps-Device"
HEADER_REQUEST_ID = "X-VelaOps-Request-ID"
HEADER_TIMESTAMP = "X-VelaOps-Timestamp"
HEADER_NONCE = "X-VelaOps-Nonce"
HEADER_SIGNATURE = "X-VelaOps-Signature"

_DEVICE_ID_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}\Z")
_HEX_128_PATTERN = re.compile(r"[0-9a-f]{32}\Z")
_HEX_256_PATTERN = re.compile(r"[0-9a-f]{64}\Z")
_METHOD_PATTERN = re.compile(r"[A-Z]+\Z")


class ErrorCode(str, Enum):
    """设备端可稳定处理的 v1 错误码。"""

    INVALID_REQUEST = "invalid_request"
    NOT_FOUND = "not_found"
    METHOD_NOT_ALLOWED = "method_not_allowed"
    PAYLOAD_TOO_LARGE = "payload_too_large"
    UNSUPPORTED_MEDIA_TYPE = "unsupported_media_type"
    UNSUPPORTED_VERSION = "unsupported_version"
    INVALID_AUTH = "invalid_auth"
    SIGNATURE_MISMATCH = "signature_mismatch"
    STALE_TIMESTAMP = "stale_timestamp"
    REPLAY_DETECTED = "replay_detected"
    UNKNOWN_DEVICE = "unknown_device"
    ACTION_NOT_ALLOWED = "action_not_allowed"
    INVALID_ACTION_PARAMETERS = "invalid_action_parameters"
    APPROVAL_REQUIRED = "approval_required"
    APPROVAL_EXPIRED = "approval_expired"
    APPROVAL_REUSED = "approval_reused"
    IDEMPOTENCY_CONFLICT = "idempotency_conflict"
    ACTION_STATE_UNCERTAIN = "action_state_uncertain"
    ACTION_VERIFICATION_FAILED = "action_verification_failed"
    EXECUTION_TIMEOUT = "execution_timeout"
    INTERNAL_ERROR = "internal_error"


class ProtocolValidationError(ValueError):
    """协议字段不合法，携带可映射到响应的稳定错误码。"""

    def __init__(self, message: str, code: ErrorCode = ErrorCode.INVALID_AUTH):
        super().__init__(message)
        self.code = code


@dataclass(frozen=True, slots=True)
class AuthMetadata:
    """参与签名的认证元数据。"""

    device_id: str
    request_id: str
    timestamp: int
    nonce: str
    version: str = PROTOCOL_VERSION

    def validate(self) -> None:
        if self.version != PROTOCOL_VERSION:
            raise ProtocolValidationError(
                "不支持的协议版本", ErrorCode.UNSUPPORTED_VERSION
            )
        validate_device_id(self.device_id)
        if not _HEX_128_PATTERN.fullmatch(self.request_id):
            raise ProtocolValidationError("request_id 必须是 32 位小写十六进制")
        if not _HEX_128_PATTERN.fullmatch(self.nonce):
            raise ProtocolValidationError("nonce 必须是 32 位小写十六进制")
        if self.timestamp < 0 or self.timestamp > 0x7FFF_FFFF_FFFF_FFFF:
            raise ProtocolValidationError("timestamp 超出有效范围")

    def to_headers(self, signature: str) -> dict[str, str]:
        """生成 HTTP 请求头；签名必须已经过本模块计算。"""
        self.validate()
        _validate_signature(signature)
        return {
            HEADER_VERSION: self.version,
            HEADER_DEVICE_ID: self.device_id,
            HEADER_REQUEST_ID: self.request_id,
            HEADER_TIMESTAMP: str(self.timestamp),
            HEADER_NONCE: self.nonce,
            HEADER_SIGNATURE: signature,
        }

    @classmethod
    def from_headers(cls, headers: Mapping[str, str]) -> tuple[AuthMetadata, str]:
        """从大小写不敏感的 HTTP 请求头解析认证元数据和签名。"""
        normalized = {name.lower(): value for name, value in headers.items()}

        def required(name: str) -> str:
            try:
                return normalized[name.lower()]
            except KeyError as exc:
                raise ProtocolValidationError(f"缺少请求头 {name}") from exc

        timestamp_text = required(HEADER_TIMESTAMP)
        if not timestamp_text.isascii() or not timestamp_text.isdecimal():
            raise ProtocolValidationError("timestamp 必须是十进制整数")

        metadata = cls(
            version=required(HEADER_VERSION),
            device_id=required(HEADER_DEVICE_ID),
            request_id=required(HEADER_REQUEST_ID),
            timestamp=int(timestamp_text),
            nonce=required(HEADER_NONCE),
        )
        metadata.validate()
        signature = required(HEADER_SIGNATURE)
        _validate_signature(signature)
        return metadata, signature


def body_sha256(body: bytes) -> str:
    """计算实际传输请求体的 SHA-256 小写十六进制摘要。"""
    return hashlib.sha256(body).hexdigest()


def validate_device_id(device_id: str) -> None:
    """验证设备 ID，供协议解析与安全配置加载共用。"""
    if not _DEVICE_ID_PATTERN.fullmatch(device_id):
        raise ProtocolValidationError("device_id 格式不合法")


def canonical_request(
    method: str,
    target: str,
    body: bytes,
    metadata: AuthMetadata,
) -> bytes:
    """构造签名字节串；不对 JSON 重编码，签名绑定实际传输字节。"""
    metadata.validate()
    if not _METHOD_PATTERN.fullmatch(method):
        raise ProtocolValidationError("HTTP 方法必须是大写 ASCII 字母")
    if (
        not target.startswith("/")
        or len(target) > MAX_TARGET_LENGTH
        or not target.isascii()
    ):
        raise ProtocolValidationError("请求目标必须是长度受限的绝对路径")
    if "\r" in target or "\n" in target or "#" in target:
        raise ProtocolValidationError("请求目标包含禁止字符")

    fields = (
        ALGORITHM,
        metadata.version,
        metadata.device_id,
        metadata.request_id,
        str(metadata.timestamp),
        metadata.nonce,
        method,
        target,
        body_sha256(body),
    )
    return "\n".join(fields).encode("ascii")


def calculate_signature(
    secret: bytes,
    method: str,
    target: str,
    body: bytes,
    metadata: AuthMetadata,
) -> str:
    """计算 HMAC-SHA256 小写十六进制签名。"""
    if len(secret) < 32:
        raise ProtocolValidationError("设备密钥长度不能小于 32 字节")
    canonical = canonical_request(method, target, body, metadata)
    return hmac.new(secret, canonical, hashlib.sha256).hexdigest()


def verify_signature(
    signature: str,
    secret: bytes,
    method: str,
    target: str,
    body: bytes,
    metadata: AuthMetadata,
) -> bool:
    """使用常数时间比较验证签名。"""
    _validate_signature(signature)
    expected = calculate_signature(secret, method, target, body, metadata)
    return hmac.compare_digest(signature, expected)


def _validate_signature(signature: str) -> None:
    if not _HEX_256_PATTERN.fullmatch(signature):
        raise ProtocolValidationError("签名必须是 64 位小写十六进制")
