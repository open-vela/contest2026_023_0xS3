"""变更 Action 的实体按键批准策略。"""

from __future__ import annotations

from dataclasses import dataclass

from .actions import ActionError, ActionRequest
from .protocol import ErrorCode


@dataclass(frozen=True, slots=True)
class ApprovalPolicy:
    max_validity_seconds: int = 120
    max_future_skew_seconds: int = 5

    def validate(self, request: ActionRequest, now: int) -> None:
        approval = request.approval
        if approval is None:
            raise ActionError(ErrorCode.APPROVAL_REQUIRED, "变更 Action 需要实体按键批准")
        if (
            approval.approved_at < 0
            or approval.expires_at <= approval.approved_at
            or approval.expires_at - approval.approved_at > self.max_validity_seconds
        ):
            raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "批准有效期不合法")
        if approval.approved_at > now + self.max_future_skew_seconds:
            raise ActionError(ErrorCode.APPROVAL_EXPIRED, "批准时间尚未生效")
        if approval.expires_at <= now:
            raise ActionError(ErrorCode.APPROVAL_EXPIRED, "批准已过期")
