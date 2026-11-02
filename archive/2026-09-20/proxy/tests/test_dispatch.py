from pathlib import Path
import tempfile
import unittest

from velaops_proxy.actions import (
    ActionError,
    ActionRegistry,
    ActionRequest,
    ActionResult,
    RiskLevel,
)
from velaops_proxy.approval import ApprovalPolicy
from velaops_proxy.dispatch import ActionDispatcher
from velaops_proxy.idempotency import SqliteExecutionStore
from velaops_proxy.protocol import ErrorCode


NOW = 1000


class FixedClock:
    def now_seconds(self):
        return NOW


class MutableClock:
    def __init__(self, now):
        self.now = now

    def now_seconds(self):
        return self.now


def request(with_approval=True):
    payload = {
        "schema_version": 1,
        "action": "restart_service",
        "target": "local-dev",
        "parameters": {"service": "proxy-test"},
    }
    if with_approval:
        payload["approval"] = {
            "approval_id": "a" * 32,
            "approved_at": NOW - 10,
            "expires_at": NOW + 60,
            "source": "physical_button",
        }
    return ActionRequest.from_payload(payload)


class DispatchTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.store = SqliteExecutionStore(Path(self.temp_dir.name) / "state.db")
        self.calls = 0
        self.registry = ActionRegistry()

        def change_handler(_):
            self.calls += 1
            return ActionResult.of(restarted=True)

        self.registry.register("restart_service", RiskLevel.CHANGE, change_handler)
        self.dispatcher = ActionDispatcher(
            self.registry, ApprovalPolicy(), self.store, FixedClock()
        )

    def tearDown(self):
        self.store.close()
        self.temp_dir.cleanup()

    def execute(self, item, request_id="1" * 32, body_hash="a" * 64):
        return self.dispatcher.execute(
            item,
            device_id="eye-001",
            request_id=request_id,
            body_sha256=body_hash,
        )

    def test_change_requires_approval_before_claim(self):
        with self.assertRaises(ActionError) as caught:
            self.execute(request(False))
        self.assertEqual(caught.exception.code, ErrorCode.APPROVAL_REQUIRED)
        self.assertEqual(self.calls, 0)

    def test_successful_change_is_executed_once_and_cached(self):
        first = self.execute(request())
        second = self.execute(request())
        self.assertTrue(first.data["restarted"])
        self.assertEqual(second.data, first.data)
        self.assertEqual(self.calls, 1)

    def test_cached_success_can_be_read_after_approval_expires(self):
        clock = MutableClock(NOW)
        dispatcher = ActionDispatcher(
            self.registry, ApprovalPolicy(), self.store, clock
        )
        item = request()
        arguments = dict(
            device_id="eye-001",
            request_id="8" * 32,
            body_sha256="a" * 64,
        )
        dispatcher.execute(item, **arguments)
        clock.now = NOW + 120
        result = dispatcher.execute(item, **arguments)
        self.assertTrue(result.data["restarted"])
        self.assertEqual(self.calls, 1)

    def test_same_request_id_cannot_change_body(self):
        self.execute(request())
        with self.assertRaises(ActionError) as caught:
            self.execute(request(), body_hash="b" * 64)
        self.assertEqual(caught.exception.code, ErrorCode.IDEMPOTENCY_CONFLICT)
        self.assertEqual(self.calls, 1)

    def test_same_physical_approval_cannot_authorize_second_request(self):
        self.execute(request(), request_id="3" * 32, body_hash="a" * 64)
        with self.assertRaises(ActionError) as caught:
            self.execute(request(), request_id="4" * 32, body_hash="b" * 64)
        self.assertEqual(caught.exception.code, ErrorCode.APPROVAL_REUSED)
        self.assertEqual(self.calls, 1)

    def test_handler_failure_is_not_replayed(self):
        registry = ActionRegistry()
        registry.register(
            "restart_service",
            RiskLevel.CHANGE,
            lambda _: (_ for _ in ()).throw(
                ActionError(ErrorCode.ACTION_VERIFICATION_FAILED, "failed")
            ),
        )
        dispatcher = ActionDispatcher(
            registry, ApprovalPolicy(), self.store, FixedClock()
        )
        with self.assertRaises(ActionError):
            dispatcher.execute(
                request(),
                device_id="eye-001",
                request_id="2" * 32,
                body_sha256="a" * 64,
            )
        with self.assertRaises(ActionError) as replay:
            dispatcher.execute(
                request(),
                device_id="eye-001",
                request_id="2" * 32,
                body_sha256="a" * 64,
            )
        self.assertEqual(replay.exception.code, ErrorCode.ACTION_STATE_UNCERTAIN)


if __name__ == "__main__":
    unittest.main()
