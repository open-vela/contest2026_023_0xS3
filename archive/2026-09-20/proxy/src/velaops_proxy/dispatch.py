"""Action 风险分流、批准校验与幂等执行编排。"""

from __future__ import annotations

from typing import Protocol

from .actions import ActionError, ActionRegistry, ActionRequest, ActionResult, RiskLevel
from .approval import ApprovalPolicy
from .idempotency import ClaimState, ExecutionClaim
from .ports import Clock
from .protocol import ErrorCode


class ChangeExecutionStore(Protocol):
    def lookup(
        self, device_id: str, request_id: str, body_sha256: str
    ) -> ExecutionClaim | None: ...

    def begin(
        self,
        device_id: str,
        request_id: str,
        body_sha256: str,
        now: int,
        approval_id: str | None = None,
    ) -> ExecutionClaim: ...

    def succeed(
        self,
        device_id: str,
        request_id: str,
        result: ActionResult,
        now: int,
    ) -> None: ...

    def fail(
        self,
        device_id: str,
        request_id: str,
        error_code: ErrorCode,
        now: int,
    ) -> None: ...


class ActionDispatcher:
    """只读 Action 直接执行；变更 Action 必须先批准再原子占位。"""

    def __init__(
        self,
        registry: ActionRegistry,
        approval_policy: ApprovalPolicy,
        execution_store: ChangeExecutionStore,
        clock: Clock,
    ) -> None:
        self._registry = registry
        self._approval_policy = approval_policy
        self._execution_store = execution_store
        self._clock = clock

    def execute(
        self,
        request: ActionRequest,
        *,
        device_id: str,
        request_id: str,
        body_sha256: str,
    ) -> ActionResult:
        registered = self._registry.get(request.action)
        if registered.risk is RiskLevel.READ_ONLY:
            return registered.handler(request)

        existing = self._execution_store.lookup(device_id, request_id, body_sha256)
        if existing is not None:
            if existing.cached_result is None:
                raise RuntimeError("幂等缓存缺少 Action 结果")
            return existing.cached_result

        now = self._clock.now_seconds()
        self._approval_policy.validate(request, now)
        if request.approval is None:
            raise RuntimeError("批准策略通过后缺少批准声明")
        claim = self._execution_store.begin(
            device_id,
            request_id,
            body_sha256,
            now,
            approval_id=request.approval.approval_id,
        )
        if claim.state is ClaimState.CACHED:
            if claim.cached_result is None:
                raise RuntimeError("幂等缓存缺少 Action 结果")
            return claim.cached_result

        try:
            result = registered.handler(request)
        except ActionError as exc:
            self._execution_store.fail(
                device_id, request_id, exc.code, self._clock.now_seconds()
            )
            raise
        except Exception:
            self._execution_store.fail(
                device_id,
                request_id,
                ErrorCode.INTERNAL_ERROR,
                self._clock.now_seconds(),
            )
            raise
        self._execution_store.succeed(
            device_id, request_id, result, self._clock.now_seconds()
        )
        return result
