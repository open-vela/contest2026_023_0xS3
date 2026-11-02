"""Guarded 模式可变更 Action。"""

from __future__ import annotations

from dataclasses import asdict
from typing import Mapping, Protocol

from .actions import ActionError, ActionRegistry, ActionRequest, ActionResult, RiskLevel
from .config import ServiceConfig, TargetConfig
from .diagnostics import ServiceSnapshot
from .protocol import ErrorCode


class ChangeOperator(Protocol):
    def restart_service(self, service: ServiceConfig) -> ServiceSnapshot: ...


class GuardedChanges:
    """只将结构化别名映射到配置中的服务，不接受任意命令。"""

    def __init__(
        self,
        targets: Mapping[str, TargetConfig],
        operator: ChangeOperator,
    ) -> None:
        self._targets = targets
        self._operator = operator

    def register(self, registry: ActionRegistry) -> None:
        registry.register("restart_service", RiskLevel.CHANGE, self.restart_service)

    def restart_service(self, request: ActionRequest) -> ActionResult:
        try:
            target = self._targets[request.target]
        except KeyError as exc:
            raise ActionError(ErrorCode.ACTION_NOT_ALLOWED, "目标不在白名单中") from exc
        if request.action not in target.allowed_actions:
            raise ActionError(ErrorCode.ACTION_NOT_ALLOWED, "目标未授权该 Action")
        if set(request.parameters) != {"service"}:
            raise ActionError(
                ErrorCode.INVALID_ACTION_PARAMETERS,
                "restart_service 参数不完整或包含未知字段",
            )
        alias = request.parameters["service"]
        if not isinstance(alias, str) or alias not in target.services:
            raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "服务别名不在白名单中")
        snapshot = self._operator.restart_service(target.services[alias])
        if snapshot.active_state != "active":
            raise ActionError(
                ErrorCode.ACTION_VERIFICATION_FAILED,
                "服务重启后未恢复 active 状态",
            )
        return ActionResult.of(service=alias, verified=True, **asdict(snapshot))
