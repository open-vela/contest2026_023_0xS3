from concurrent.futures import ThreadPoolExecutor
import http.client
import json
from pathlib import Path
import tempfile
from threading import Thread
import unittest

from velaops_proxy.adapters import InMemoryDeviceSecretStore, InMemoryReplayStore
from velaops_proxy.actions import ActionError, ActionRegistry, ActionResult, RiskLevel
from velaops_proxy.approval import ApprovalPolicy
from velaops_proxy.auth import RequestAuthenticator
from velaops_proxy.http_api import ProxyApi
from velaops_proxy.http_server import MAX_REQUEST_BODY_BYTES, create_server
from velaops_proxy.dispatch import ActionDispatcher
from velaops_proxy.idempotency import SqliteExecutionStore
from velaops_proxy.protocol import AuthMetadata, calculate_signature
from velaops_proxy.protocol import ErrorCode


DEVICE_ID = "eye-http-test"
SECRET = b"http-test-secret-0123456789ABCDEF"
NOW = 1787582400


class FixedClock:
    def now_seconds(self):
        return NOW


class MemoryAudit:
    def __init__(self):
        self.events = []

    def record(self, event):
        event.validate()
        self.events.append(event)


class HttpServerTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.execution_store = SqliteExecutionStore(
            Path(self.temp_dir.name) / "state.db"
        )
        secrets = InMemoryDeviceSecretStore({DEVICE_ID: SECRET})
        replay = InMemoryReplayStore()
        authenticator = RequestAuthenticator(secrets, replay, FixedClock())
        actions = ActionRegistry()
        actions.register(
            "check_memory",
            RiskLevel.READ_ONLY,
            lambda request: ActionResult.of(target=request.target, used_percent=42.5),
        )
        actions.register(
            "failing_action",
            RiskLevel.READ_ONLY,
            lambda _: (_ for _ in ()).throw(
                ActionError(ErrorCode.EXECUTION_TIMEOUT, "诊断超时")
            ),
        )
        self.change_calls = 0

        def restart(_):
            self.change_calls += 1
            return ActionResult.of(service="proxy-test", verified=True)

        actions.register("restart_service", RiskLevel.CHANGE, restart)
        dispatcher = ActionDispatcher(
            actions,
            ApprovalPolicy(),
            self.execution_store,
            FixedClock(),
        )
        self.audit = MemoryAudit()
        self.server = create_server(
            ("127.0.0.1", 0),
            ProxyApi(
                authenticator,
                actions,
                dispatcher,
                audit_sink=self.audit,
                clock=FixedClock(),
            ),
        )
        self.thread = Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.host, self.port = self.server.server_address

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.execution_store.close()
        self.temp_dir.cleanup()

    def request(self, method, target, body=b"", headers=None):
        connection = http.client.HTTPConnection(self.host, self.port, timeout=3)
        connection.request(method, target, body=body, headers=headers or {})
        response = connection.getresponse()
        payload = json.loads(response.read())
        response_headers = dict(response.getheaders())
        connection.close()
        return response.status, response_headers, payload

    def signed_headers(
        self,
        body,
        nonce="1" * 32,
        request_id="2" * 32,
        target="/v1/auth/check",
    ):
        metadata = AuthMetadata(
            device_id=DEVICE_ID,
            request_id=request_id,
            timestamp=NOW,
            nonce=nonce,
        )
        signature = calculate_signature(
            SECRET, "POST", target, body, metadata
        )
        headers = metadata.to_headers(signature)
        headers["Content-Type"] = "application/json"
        return headers

    def test_public_health_check(self):
        status, headers, payload = self.request("GET", "/healthz")
        self.assertEqual(status, 200)
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["result"]["status"], "ok")
        self.assertEqual(headers["Cache-Control"], "no-store")

    def test_authenticated_check(self):
        body = b"{}"
        status, _, payload = self.request(
            "POST", "/v1/auth/check", body, self.signed_headers(body)
        )
        self.assertEqual(status, 200)
        self.assertEqual(payload["result"]["device_id"], DEVICE_ID)
        self.assertEqual(payload["request_id"], "2" * 32)

    def test_auth_failures_are_generic(self):
        body = b"{}"
        headers = self.signed_headers(body)
        headers["X-VelaOps-Signature"] = "0" * 64
        status, _, payload = self.request(
            "POST", "/v1/auth/check", body, headers
        )
        self.assertEqual(status, 401)
        self.assertEqual(payload["error"]["code"], "invalid_auth")
        self.assertNotIn("request_id", payload)
        self.assertEqual(self.audit.events[-1].event_type.value, "auth_failed")
        self.assertEqual(
            self.audit.events[-1].error_code.value, "signature_mismatch"
        )

    def test_replay_within_tolerance_is_accepted(self):
        # 串口隧道 ACK 丢失时的同请求重发在容忍窗口内被接受（由执行层幂等兜底），
        # 超出窗口的重复 nonce 才会被拒（见 test_auth）。
        body = b"{}"
        headers = self.signed_headers(body)
        self.assertEqual(
            self.request("POST", "/v1/auth/check", body, headers)[0], 200
        )
        self.assertEqual(
            self.request("POST", "/v1/auth/check", body, headers)[0], 200
        )

    def test_invalid_json_is_rejected_after_authentication(self):
        body = b"not-json"
        status, _, payload = self.request(
            "POST", "/v1/auth/check", body, self.signed_headers(body)
        )
        self.assertEqual(status, 400)
        self.assertEqual(payload["error"]["code"], "invalid_request")

    def test_unsupported_media_type(self):
        body = b"{}"
        headers = self.signed_headers(body)
        headers["Content-Type"] = "text/plain"
        status, _, payload = self.request(
            "POST", "/v1/auth/check", body, headers
        )
        self.assertEqual(status, 415)
        self.assertEqual(payload["error"]["code"], "unsupported_media_type")

    def test_header_names_are_case_insensitive(self):
        body = b"{}"
        headers = {
            name.lower(): value
            for name, value in self.signed_headers(body).items()
        }
        status, _, payload = self.request(
            "POST", "/v1/auth/check", body, headers
        )
        self.assertEqual(status, 200)
        self.assertTrue(payload["ok"])

    def test_payload_limit(self):
        body = b"x" * (MAX_REQUEST_BODY_BYTES + 1)
        status, _, payload = self.request(
            "POST", "/v1/auth/check", body, {"Content-Type": "application/json"}
        )
        self.assertEqual(status, 413)
        self.assertEqual(payload["error"]["code"], "payload_too_large")

    def test_duplicate_headers_are_rejected(self):
        connection = http.client.HTTPConnection(self.host, self.port, timeout=3)
        connection.putrequest("POST", "/v1/auth/check")
        connection.putheader("Content-Length", "2")
        connection.putheader("Content-Type", "application/json")
        connection.putheader("X-VelaOps-Version", "1")
        connection.putheader("X-VelaOps-Version", "1")
        connection.endheaders(b"{}")
        response = connection.getresponse()
        payload = json.loads(response.read())
        connection.close()
        self.assertEqual(response.status, 400)
        self.assertEqual(payload["error"]["code"], "invalid_request")

    def test_method_and_route_errors(self):
        status, headers, payload = self.request("POST", "/healthz")
        self.assertEqual(status, 405)
        self.assertEqual(headers["Allow"], "GET")
        self.assertEqual(payload["error"]["code"], "method_not_allowed")

        status, _, payload = self.request("GET", "/missing")
        self.assertEqual(status, 404)
        self.assertEqual(payload["error"]["code"], "not_found")

    def test_absolute_form_target_is_rejected(self):
        connection = http.client.HTTPConnection(self.host, self.port, timeout=3)
        connection.putrequest(
            "GET", f"http://{self.host}:{self.port}/healthz", skip_host=True
        )
        connection.putheader("Host", self.host)
        connection.endheaders()
        response = connection.getresponse()
        payload = json.loads(response.read())
        connection.close()
        self.assertEqual(response.status, 400)
        self.assertEqual(payload["error"]["code"], "invalid_request")

    def test_concurrent_unique_requests(self):
        body = b"{}"

        def perform(index):
            value = f"{index:032x}"
            return self.request(
                "POST",
                "/v1/auth/check",
                body,
                self.signed_headers(body, nonce=value, request_id=value),
            )[0]

        with ThreadPoolExecutor(max_workers=8) as pool:
            statuses = list(pool.map(perform, range(1, 17)))
        self.assertEqual(statuses, [200] * 16)

    def test_authenticated_action_execution(self):
        target = "/v1/actions/execute"
        body = json.dumps(
            {
                "schema_version": 1,
                "action": "check_memory",
                "target": "local-dev",
                "parameters": {},
            },
            separators=(",", ":"),
        ).encode()
        status, _, payload = self.request(
            "POST", target, body, self.signed_headers(body, target=target)
        )
        self.assertEqual(status, 200)
        self.assertEqual(payload["result"]["used_percent"], 42.5)
        self.assertEqual(payload["request_id"], "2" * 32)
        self.assertEqual(
            [event.event_type.value for event in self.audit.events],
            ["action_started", "action_succeeded"],
        )

    def test_action_errors_have_stable_status_and_request_id(self):
        target = "/v1/actions/execute"
        cases = (
            ("unknown_action", 403, "action_not_allowed"),
            ("failing_action", 504, "execution_timeout"),
        )
        for index, (action, expected_status, expected_code) in enumerate(cases, 3):
            with self.subTest(action=action):
                body = json.dumps(
                    {
                        "schema_version": 1,
                        "action": action,
                        "target": "local-dev",
                        "parameters": {},
                    },
                    separators=(",", ":"),
                ).encode()
                value = f"{index:032x}"
                status, _, payload = self.request(
                    "POST",
                    target,
                    body,
                    self.signed_headers(
                        body, nonce=value, request_id=value, target=target
                    ),
                )
                self.assertEqual(status, expected_status)
                self.assertEqual(payload["error"]["code"], expected_code)
                self.assertEqual(payload["request_id"], value)
                self.assertFalse(payload["error"]["retryable"])

    def test_duplicate_json_fields_are_rejected(self):
        target = "/v1/actions/execute"
        body = b'{"schema_version":1,"schema_version":1}'
        status, _, payload = self.request(
            "POST", target, body, self.signed_headers(body, target=target)
        )
        self.assertEqual(status, 400)
        self.assertEqual(payload["error"]["code"], "invalid_request")

    def test_change_requires_approval_and_reuses_persistent_result(self):
        target = "/v1/actions/execute"
        base = {
            "schema_version": 1,
            "action": "restart_service",
            "target": "local-dev",
            "parameters": {"service": "proxy-test"},
        }
        missing_body = json.dumps(base, separators=(",", ":")).encode()
        status, _, payload = self.request(
            "POST",
            target,
            missing_body,
            self.signed_headers(missing_body, target=target),
        )
        self.assertEqual(status, 403)
        self.assertEqual(payload["error"]["code"], "approval_required")
        self.assertEqual(self.change_calls, 0)

        approved = {
            **base,
            "approval": {
                "approval_id": "a" * 32,
                "approved_at": NOW - 10,
                "expires_at": NOW + 60,
                "source": "physical_button",
            },
        }
        body = json.dumps(approved, separators=(",", ":")).encode()
        first_id = "5" * 32
        first_headers = self.signed_headers(
            body, nonce="6" * 32, request_id=first_id, target=target
        )
        self.assertEqual(self.request("POST", target, body, first_headers)[0], 200)
        second_headers = self.signed_headers(
            body, nonce="7" * 32, request_id=first_id, target=target
        )
        status, _, payload = self.request("POST", target, body, second_headers)
        self.assertEqual(status, 200)
        self.assertTrue(payload["result"]["verified"])
        self.assertEqual(self.change_calls, 1)


if __name__ == "__main__":
    unittest.main()
