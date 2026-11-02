import math
import unittest

from velaops_proxy.actions import (
    ActionError,
    ActionRegistry,
    ActionRequest,
    ActionResult,
    RiskLevel,
)
from velaops_proxy.approval import ApprovalPolicy
from velaops_proxy.protocol import ErrorCode


class ActionsTest(unittest.TestCase):
    def test_parses_strict_request(self):
        request = ActionRequest.from_payload(
            {
                "schema_version": 1,
                "action": "check_service",
                "target": "local-dev",
                "parameters": {"service": "velaops-test"},
            }
        )
        self.assertEqual(request.action, "check_service")
        self.assertEqual(request.target, "local-dev")
        self.assertEqual(request.parameters["service"], "velaops-test")
        with self.assertRaises(TypeError):
            request.parameters["service"] = "changed"

    def test_nested_parameters_are_immutable_and_json_safe(self):
        request = ActionRequest.from_payload(
            {
                "schema_version": 1,
                "action": "check_service",
                "target": "local-dev",
                "parameters": {"options": {"labels": ["a", "b"]}},
            }
        )
        self.assertEqual(request.parameters["options"]["labels"], ("a", "b"))
        with self.assertRaises(TypeError):
            request.parameters["options"]["new"] = True
        for invalid in (object(), math.inf, math.nan):
            with self.subTest(invalid=invalid):
                with self.assertRaises(ActionError):
                    ActionRequest.from_payload(
                        {
                            "schema_version": 1,
                            "action": "check_service",
                            "target": "local-dev",
                            "parameters": {"value": invalid},
                        }
                    )

    def test_boolean_schema_version_is_rejected(self):
        with self.assertRaises(ActionError):
            ActionRequest.from_payload(
                {
                    "schema_version": True,
                    "action": "check_service",
                    "target": "local-dev",
                    "parameters": {},
                }
            )

    def test_parses_and_validates_physical_approval(self):
        payload = {
            "schema_version": 1,
            "action": "restart_service",
            "target": "local-dev",
            "parameters": {"service": "proxy-test"},
            "approval": {
                "approval_id": "a" * 32,
                "approved_at": 1000,
                "expires_at": 1060,
                "source": "physical_button",
            },
        }
        request = ActionRequest.from_payload(payload)
        self.assertEqual(request.approval.approval_id, "a" * 32)
        ApprovalPolicy().validate(request, now=1030)

    def test_approval_policy_rejects_missing_expired_and_invalid_source(self):
        base = {
            "schema_version": 1,
            "action": "restart_service",
            "target": "local-dev",
            "parameters": {"service": "proxy-test"},
        }
        with self.assertRaises(ActionError) as missing:
            ApprovalPolicy().validate(ActionRequest.from_payload(base), now=1030)
        self.assertEqual(missing.exception.code, ErrorCode.APPROVAL_REQUIRED)

        for approval in (
            {"approval_id": "a" * 32, "approved_at": 900, "expires_at": 960, "source": "physical_button"},
            {"approval_id": "a" * 32, "approved_at": 1000, "expires_at": 1300, "source": "physical_button"},
            {"approval_id": "a" * 32, "approved_at": 1000, "expires_at": 1060, "source": "voice"},
        ):
            with self.subTest(approval=approval):
                with self.assertRaises(ActionError):
                    request = ActionRequest.from_payload({**base, "approval": approval})
                    ApprovalPolicy().validate(request, now=1030)

    def test_rejects_unknown_or_missing_fields(self):
        base = {
            "schema_version": 1,
            "action": "check_service",
            "target": "local-dev",
            "parameters": {},
        }
        for payload in ({**base, "extra": True}, {key: value for key, value in base.items() if key != "target"}):
            with self.subTest(payload=payload):
                with self.assertRaises(ActionError) as caught:
                    ActionRequest.from_payload(payload)
                self.assertEqual(caught.exception.code, ErrorCode.INVALID_ACTION_PARAMETERS)

    def test_rejects_nested_or_invalid_parameter_names(self):
        with self.assertRaises(ActionError):
            ActionRequest.from_payload(
                {
                    "schema_version": 1,
                    "action": "check_service",
                    "target": "local-dev",
                    "parameters": {"bad-name": "value"},
                }
            )

    def test_registry_rejects_duplicates_and_unknown_actions(self):
        registry = ActionRegistry()
        registry.register(
            "check_service", RiskLevel.READ_ONLY, lambda _: ActionResult.of(ok=True)
        )
        with self.assertRaises(ValueError):
            registry.register(
                "check_service", RiskLevel.READ_ONLY, lambda _: ActionResult.of()
            )
        with self.assertRaises(ActionError) as caught:
            registry.get("restart_service")
        self.assertEqual(caught.exception.code, ErrorCode.ACTION_NOT_ALLOWED)

    def test_registry_executes_registered_handler(self):
        registry = ActionRegistry()
        registry.register(
            "check_service",
            RiskLevel.READ_ONLY,
            lambda request: ActionResult.of(target=request.target, active=True),
        )
        request = ActionRequest.from_payload(
            {
                "schema_version": 1,
                "action": "check_service",
                "target": "local-dev",
                "parameters": {},
            }
        )
        result = registry.execute(request)
        self.assertEqual(result.data, {"target": "local-dev", "active": True})


if __name__ == "__main__":
    unittest.main()
