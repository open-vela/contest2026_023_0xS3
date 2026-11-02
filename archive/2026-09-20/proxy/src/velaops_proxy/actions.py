"""Action 请求模型、风险等级与白名单注册表。"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
import math
import re
from types import MappingProxyType
from typing import Any, Callable, Mapping

from .protocol import ErrorCode


ACTION_SCHEMA_VERSION = 1
_NAME_PATTERN = re.compile(r"[a-z][a-z0-9_]{0,63}\Z")
_TARGET_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}\Z")
_MAX_JSON_DEPTH = 8
_HEX_128_PATTERN = re.compile(r"[0-9a-f]{32}\Z")


class RiskLevel(str, Enum):
    READ_ONLY = "read_only"
    CHANGE = "change"


class ActionError(ValueError):
    def __init__(self, code: ErrorCode, message: str):
        super().__init__(message)
        self.code = code


def _freeze_json(value: Any, depth: int = 0) -> Any:
    """校验并冻结 JSON 值，避免 handler 之间通过嵌套对象篡改请求。"""
    if depth > _MAX_JSON_DEPTH:
        raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "parameters 嵌套过深")
    if value is None or isinstance(value, (str, bool, int)):
        return value
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "parameters 包含非有限数值")
        return value
    if isinstance(value, list):
        return tuple(_freeze_json(item, depth + 1) for item in value)
    if isinstance(value, dict):
        if not all(isinstance(key, str) for key in value):
            raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "parameters 对象键必须是字符串")
        return MappingProxyType(
            {key: _freeze_json(item, depth + 1) for key, item in value.items()}
        )
    raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "parameters 包含非 JSON 值")


@dataclass(frozen=True, slots=True)
class ActionApproval:
    approval_id: str
    approved_at: int
    expires_at: int
    source: str

    @classmethod
    def from_payload(cls, payload: Any) -> ActionApproval:
        if not isinstance(payload, dict) or set(payload) != {
            "approval_id",
            "approved_at",
            "expires_at",
            "source",
        }:
            raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "批准声明字段不合法")
        approval_id = payload["approval_id"]
        approved_at = payload["approved_at"]
        expires_at = payload["expires_at"]
        source = payload["source"]
        if not isinstance(approval_id, str) or not _HEX_128_PATTERN.fullmatch(approval_id):
            raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "approval_id 格式不合法")
        if type(approved_at) is not int or type(expires_at) is not int:
            raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "批准时间必须是 Unix 秒")
        if source != "physical_button":
            raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "批准来源不受信")
        return cls(approval_id, approved_at, expires_at, source)


@dataclass(frozen=True, slots=True)
class ActionRequest:
    action: str
    target: str
    parameters: Mapping[str, Any]
    approval: ActionApproval | None = None
    schema_version: int = ACTION_SCHEMA_VERSION

    @classmethod
    def from_payload(cls, payload: Any) -> ActionRequest:
        if not isinstance(payload, dict):
            raise ActionError(
                ErrorCode.INVALID_ACTION_PARAMETERS,
                "Action 请求必须是 JSON 对象",
            )
        required = {"schema_version", "action", "target", "parameters"}
        allowed = required | {"approval"}
        keys = set(payload)
        if not required.issubset(keys) or not keys.issubset(allowed):
            raise ActionError(
                ErrorCode.INVALID_ACTION_PARAMETERS,
                "Action 请求字段不完整或包含未知字段",
            )
        if type(payload["schema_version"]) is not int or payload["schema_version"] != ACTION_SCHEMA_VERSION:
            raise ActionError(ErrorCode.INVALID_REQUEST, "Action schema 版本不受支持")
        action = payload["action"]
        target = payload["target"]
        parameters = payload["parameters"]
        approval_payload = payload.get("approval")
        if not isinstance(action, str) or not _NAME_PATTERN.fullmatch(action):
            raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "Action 名格式不合法")
        if not isinstance(target, str) or not _TARGET_PATTERN.fullmatch(target):
            raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "target 格式不合法")
        if not isinstance(parameters, dict):
            raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "parameters 必须是对象")
        if len(parameters) > 32 or any(
            not isinstance(name, str) or not _NAME_PATTERN.fullmatch(name)
            for name in parameters
        ):
            raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "parameters 字段不合法")
        return cls(
            action=action,
            target=target,
            parameters=_freeze_json(parameters),
            approval=(
                None
                if approval_payload is None
                else ActionApproval.from_payload(approval_payload)
            ),
        )


@dataclass(frozen=True, slots=True)
class ActionResult:
    data: Mapping[str, Any]

    @classmethod
    def of(cls, **data: Any) -> ActionResult:
        return cls(MappingProxyType(data))


ActionHandler = Callable[[ActionRequest], ActionResult]


@dataclass(frozen=True, slots=True)
class RegisteredAction:
    name: str
    risk: RiskLevel
    handler: ActionHandler


class ActionRegistry:
    """只允许显式注册的结构化 Action，拒绝任意命令名称。"""

    def __init__(self) -> None:
        self._actions: dict[str, RegisteredAction] = {}

    def register(self, name: str, risk: RiskLevel, handler: ActionHandler) -> None:
        if not _NAME_PATTERN.fullmatch(name):
            raise ValueError("注册的 Action 名格式不合法")
        if name in self._actions:
            raise ValueError(f"Action 已注册: {name}")
        self._actions[name] = RegisteredAction(name, risk, handler)

    def get(self, name: str) -> RegisteredAction:
        try:
            return self._actions[name]
        except KeyError as exc:
            raise ActionError(ErrorCode.ACTION_NOT_ALLOWED, "Action 不在白名单中") from exc

    def execute(self, request: ActionRequest) -> ActionResult:
        return self.get(request.action).handler(request)

    def names(self) -> tuple[str, ...]:
        return tuple(sorted(self._actions))
