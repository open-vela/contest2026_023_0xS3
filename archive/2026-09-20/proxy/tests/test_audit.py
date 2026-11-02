import json
import os
from pathlib import Path
import tempfile
import unittest

from velaops_proxy.audit import AuditEvent, AuditEventType, JsonlAuditSink
from velaops_proxy.actions import ActionRegistry, ActionResult, RiskLevel
from velaops_proxy.auth import AuthenticatedRequest
from velaops_proxy.http_api import ApiRequest, ProxyApi
from velaops_proxy.protocol import AuthMetadata
from velaops_proxy.protocol import ErrorCode


class AuditTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.path = Path(self.temp_dir.name) / "audit.jsonl"

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_appends_structured_events_with_private_permissions(self):
        sink = JsonlAuditSink(self.path)
        sink.record(
            AuditEvent(
                1000,
                AuditEventType.ACTION_STARTED,
                device_id="eye-001",
                request_id="a" * 32,
                target="local-dev",
                action="restart_service",
                risk="change",
            )
        )
        sink.record(
            AuditEvent(
                1001,
                AuditEventType.ACTION_SUCCEEDED,
                device_id="eye-001",
                request_id="a" * 32,
                target="local-dev",
                action="restart_service",
                risk="change",
            )
        )
        sink.close()
        records = [json.loads(line) for line in self.path.read_text().splitlines()]
        self.assertEqual([item["event_type"] for item in records], ["action_started", "action_succeeded"])
        self.assertEqual(os.stat(self.path).st_mode & 0o777, 0o600)
        self.assertNotIn("parameters", records[0])

    def test_rejects_insecure_file_symlink_and_untrusted_fields(self):
        self.path.touch(mode=0o644)
        with self.assertRaises(ValueError):
            JsonlAuditSink(self.path)

        target = Path(self.temp_dir.name) / "target"
        target.touch(mode=0o600)
        link = Path(self.temp_dir.name) / "link"
        link.symlink_to(target)
        with self.assertRaises(ValueError):
            JsonlAuditSink(link)

        insecure_dir = Path(self.temp_dir.name) / "world-writable"
        insecure_dir.mkdir(mode=0o777)
        insecure_dir.chmod(0o777)
        with self.assertRaises(ValueError):
            JsonlAuditSink(insecure_dir / "audit.jsonl")

        with self.assertRaises(ValueError):
            AuditEvent(
                1000,
                AuditEventType.ACTION_FAILED,
                action="bad/action\nforged",
                error_code=ErrorCode.INTERNAL_ERROR,
            ).to_json_bytes()

    def test_action_fails_closed_when_started_audit_cannot_persist(self):
        class Authenticator:
            def authenticate(self, *_):
                return AuthenticatedRequest(
                    AuthMetadata("eye-001", "a" * 32, 1000, "b" * 32)
                )

        class Clock:
            def now_seconds(self):
                return 1000

        class FailingAudit:
            def record(self, _):
                raise OSError("disk full")

        calls = []
        registry = ActionRegistry()
        registry.register(
            "check_memory",
            RiskLevel.READ_ONLY,
            lambda _: calls.append(True) or ActionResult.of(ok=True),
        )
        api = ProxyApi(
            Authenticator(),
            registry,
            audit_sink=FailingAudit(),
            clock=Clock(),
        )
        response = api.handle(
            ApiRequest(
                "POST",
                "/v1/actions/execute",
                {"content-type": "application/json"},
                b'{"schema_version":1,"action":"check_memory","target":"local-dev","parameters":{}}',
            )
        )
        self.assertEqual(response.status, 500)
        self.assertEqual(calls, [])


if __name__ == "__main__":
    unittest.main()
