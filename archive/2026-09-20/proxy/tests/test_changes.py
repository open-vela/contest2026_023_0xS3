import unittest

from velaops_proxy.actions import ActionError, ActionRegistry, ActionRequest
from velaops_proxy.changes import GuardedChanges
from velaops_proxy.config import ServiceConfig, TargetConfig
from velaops_proxy.diagnostics import ServiceSnapshot
from velaops_proxy.protocol import ErrorCode


class FakeOperator:
    def __init__(self, active_state="active"):
        self.active_state = active_state
        self.service = None

    def restart_service(self, service):
        self.service = service
        return ServiceSnapshot(
            "loaded", self.active_state, "running", "success", 0
        )


def request(service="proxy-test"):
    return ActionRequest.from_payload(
        {
            "schema_version": 1,
            "action": "restart_service",
            "target": "local-dev",
            "parameters": {"service": service},
        }
    )


class ChangesTest(unittest.TestCase):
    def setUp(self):
        self.operator = FakeOperator()
        self.target = TargetConfig(
            allowed_actions=frozenset({"restart_service"}),
            services={
                "proxy-test": ServiceConfig("velaops-test.service", "user", False)
            },
            ports={},
            disks={},
        )
        self.changes = GuardedChanges({"local-dev": self.target}, self.operator)

    def test_restart_uses_alias_and_verifies_active_state(self):
        registry = ActionRegistry()
        self.changes.register(registry)
        result = registry.execute(request())
        self.assertEqual(self.operator.service.unit, "velaops-test.service")
        self.assertEqual(self.operator.service.manager, "user")
        self.assertTrue(result.data["verified"])

    def test_rejects_unknown_alias_and_failed_verification(self):
        with self.assertRaises(ActionError):
            self.changes.restart_service(request("unknown"))
        failing = GuardedChanges(
            {"local-dev": self.target}, FakeOperator(active_state="failed")
        )
        with self.assertRaises(ActionError) as caught:
            failing.restart_service(request())
        self.assertEqual(caught.exception.code, ErrorCode.ACTION_VERIFICATION_FAILED)


if __name__ == "__main__":
    unittest.main()
