"""与具体 HTTP 服务器解耦的 Proxy API 路由和响应模型。"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from typing import Any, Mapping
from urllib.parse import urlsplit

from .auth import AuthenticationError, RequestAuthenticator
from .actions import ActionError, ActionRegistry, ActionRequest, RiskLevel
from .audit import AuditEvent, AuditEventType, AuditSink
from .dispatch import ActionDispatcher
from .ports import Clock
from .protocol import ErrorCode


SCHEMA_VERSION = 1
JSON_CONTENT_TYPE = "application/json"


@dataclass(frozen=True, slots=True)
class ApiRequest:
    method: str
    target: str
    headers: Mapping[str, str]
    body: bytes


@dataclass(frozen=True, slots=True)
class ApiResponse:
    status: int
    payload: Mapping[str, object]
    extra_headers: Mapping[str, str] | None = None

    def body_bytes(self) -> bytes:
        return json.dumps(
            self.payload,
            ensure_ascii=False,
            separators=(",", ":"),
        ).encode("utf-8")


class ProxyApi:
    """只编排 HTTP 语义；认证、执行与存储通过独立用例注入。"""

    def __init__(
        self,
        authenticator: RequestAuthenticator,
        actions: ActionRegistry | None = None,
        dispatcher: ActionDispatcher | None = None,
        audit_sink: AuditSink | None = None,
        clock: Clock | None = None,
    ) -> None:
        if (audit_sink is None) != (clock is None):
            raise ValueError("audit_sink 与 clock 必须同时配置")
        self._authenticator = authenticator
        self._actions = actions
        self._dispatcher = dispatcher
        self._audit_sink = audit_sink
        self._clock = clock

    def handle(self, request: ApiRequest) -> ApiResponse:
        path = urlsplit(request.target).path
        if path == "/healthz":
            if request.method != "GET":
                return error_response(
                    405,
                    ErrorCode.METHOD_NOT_ALLOWED,
                    "请求方法不受支持",
                    extra_headers={"Allow": "GET"},
                )
            return ApiResponse(
                200,
                {
                    "schema_version": SCHEMA_VERSION,
                    "ok": True,
                    "result": {"status": "ok"},
                },
            )

        if path not in {"/v1/auth/check", "/v1/actions/execute"}:
            return error_response(404, ErrorCode.NOT_FOUND, "接口不存在")
        if request.method != "POST":
            return error_response(
                405,
                ErrorCode.METHOD_NOT_ALLOWED,
                "请求方法不受支持",
                extra_headers={"Allow": "POST"},
            )

        try:
            authenticated = self._authenticator.authenticate(
                request.headers,
                request.method,
                request.target,
                request.body,
            )
        except AuthenticationError as exc:
            # 公开响应不区分未知设备、签名错误、过期和重放。
            self._record_auth_failure_best_effort(exc.audit_code)
            return error_response(401, ErrorCode.INVALID_AUTH, "请求认证失败")

        content_type = request.headers.get("content-type", "")
        if content_type.split(";", 1)[0].strip().lower() != JSON_CONTENT_TYPE:
            return error_response(
                415,
                ErrorCode.UNSUPPORTED_MEDIA_TYPE,
                "仅支持 application/json",
                request_id=authenticated.metadata.request_id,
            )

        try:
            payload = _decode_json(request.body)
        except (UnicodeDecodeError, json.JSONDecodeError, ValueError):
            return error_response(
                400,
                ErrorCode.INVALID_REQUEST,
                "请求体不是有效 JSON",
                request_id=authenticated.metadata.request_id,
            )
        if path == "/v1/actions/execute":
            return self._execute_action(
                payload,
                authenticated.metadata.device_id,
                authenticated.metadata.request_id,
                hashlib.sha256(request.body).hexdigest(),
            )

        if payload != {}:
            return error_response(
                400,
                ErrorCode.INVALID_REQUEST,
                "认证检查请求体必须是空对象",
                request_id=authenticated.metadata.request_id,
            )

        return ApiResponse(
            200,
            {
                "schema_version": SCHEMA_VERSION,
                "request_id": authenticated.metadata.request_id,
                "ok": True,
                "result": {"device_id": authenticated.metadata.device_id},
            },
        )

    def _execute_action(
        self,
        payload: Any,
        device_id: str,
        request_id: str,
        body_sha256: str,
    ) -> ApiResponse:
        if self._actions is None:
            return error_response(
                503,
                ErrorCode.INTERNAL_ERROR,
                "Action 服务未就绪",
                request_id=request_id,
            )
        try:
            action_request = ActionRequest.from_payload(payload)
            registered = self._actions.get(action_request.action)
            self._record_action(
                AuditEventType.ACTION_STARTED,
                device_id,
                request_id,
                action_request,
                registered.risk,
            )
            if registered.risk is RiskLevel.CHANGE:
                if self._dispatcher is None:
                    return error_response(
                        503,
                        ErrorCode.INTERNAL_ERROR,
                        "变更 Action 服务未就绪",
                        request_id=request_id,
                    )
                result = self._dispatcher.execute(
                    action_request,
                    device_id=device_id,
                    request_id=request_id,
                    body_sha256=body_sha256,
                )
            else:
                result = registered.handler(action_request)
        except ActionError as exc:
            self._record_action(
                AuditEventType.ACTION_FAILED,
                device_id,
                request_id,
                locals().get("action_request"),
                getattr(locals().get("registered"), "risk", None),
                exc.code,
            )
            statuses = {
                ErrorCode.ACTION_NOT_ALLOWED: 403,
                ErrorCode.INVALID_ACTION_PARAMETERS: 400,
                ErrorCode.INVALID_REQUEST: 400,
                ErrorCode.EXECUTION_TIMEOUT: 504,
                ErrorCode.APPROVAL_REQUIRED: 403,
                ErrorCode.APPROVAL_EXPIRED: 403,
                ErrorCode.APPROVAL_REUSED: 409,
                ErrorCode.IDEMPOTENCY_CONFLICT: 409,
                ErrorCode.ACTION_STATE_UNCERTAIN: 409,
                ErrorCode.ACTION_VERIFICATION_FAILED: 502,
            }
            return error_response(
                statuses.get(exc.code, 500),
                exc.code,
                str(exc),
                request_id=request_id,
                retryable=False,
            )
        except Exception:
            # 系统细节只能进入受保护审计，不向设备暴露。
            try:
                self._record_action(
                    AuditEventType.ACTION_FAILED,
                    device_id,
                    request_id,
                    locals().get("action_request"),
                    getattr(locals().get("registered"), "risk", None),
                    ErrorCode.INTERNAL_ERROR,
                )
            except Exception:
                # started 事件或本次写入失败已能表明需要人工检查。
                pass
            return error_response(
                500,
                ErrorCode.INTERNAL_ERROR,
                "Action 执行失败",
                request_id=request_id,
            )
        self._record_action(
            AuditEventType.ACTION_SUCCEEDED,
            device_id,
            request_id,
            action_request,
            registered.risk,
        )
        return ApiResponse(
            200,
            {
                "schema_version": SCHEMA_VERSION,
                "request_id": request_id,
                "ok": True,
                "result": _json_value(result.data),
            },
        )

    def _record_auth_failure_best_effort(self, error_code: ErrorCode) -> None:
        if self._audit_sink is None or self._clock is None:
            return
        try:
            self._audit_sink.record(
                AuditEvent(
                    self._clock.now_seconds(),
                    AuditEventType.AUTH_FAILED,
                    error_code=error_code,
                )
            )
        except Exception:
            # 认证已失败，审计故障不能把请求变成可执行。
            return

    def _record_action(
        self,
        event_type: AuditEventType,
        device_id: str,
        request_id: str,
        request: ActionRequest | None,
        risk: RiskLevel | None,
        error_code: ErrorCode | None = None,
    ) -> None:
        if self._audit_sink is None or self._clock is None:
            return
        self._audit_sink.record(
            AuditEvent(
                self._clock.now_seconds(),
                event_type,
                device_id=device_id,
                request_id=request_id,
                target=None if request is None else request.target,
                action=None if request is None else request.action,
                risk=None if risk is None else risk.value,
                error_code=error_code,
            )
        )


def _decode_json(body: bytes) -> Any:
    def reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("请求体包含重复 JSON 字段")
            result[key] = value
        return result

    def reject_constant(value: str) -> None:
        raise ValueError(f"请求体包含非标准数值: {value}")

    return json.loads(
        body,
        object_pairs_hook=reject_duplicates,
        parse_constant=reject_constant,
    )


def _json_value(value: Any) -> Any:
    """将领域层的不可变容器转为 JSON 编码器支持的副本。"""
    if isinstance(value, Mapping):
        return {key: _json_value(item) for key, item in value.items()}
    if isinstance(value, (tuple, list)):
        return [_json_value(item) for item in value]
    return value


def error_response(
    status: int,
    code: ErrorCode,
    message: str,
    *,
    request_id: str | None = None,
    extra_headers: Mapping[str, str] | None = None,
    retryable: bool | None = None,
) -> ApiResponse:
    payload: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "ok": False,
        "error": {
            "code": code.value,
            "message": message,
            "retryable": status >= 500 if retryable is None else retryable,
        },
    }
    if request_id is not None:
        payload["request_id"] = request_id
    return ApiResponse(status, payload, extra_headers)
