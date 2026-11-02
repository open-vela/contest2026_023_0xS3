from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import unittest

from velaops_proxy.adapters import InMemoryDeviceSecretStore, InMemoryReplayStore
from velaops_proxy.auth import AuthenticationError, RequestAuthenticator
from velaops_proxy.ports import ReplayStoreError
from velaops_proxy.protocol import AuthMetadata, ErrorCode, calculate_signature


VECTOR_PATH = Path(__file__).parent / "vectors" / "hmac_v1.json"


class FixedClock:
    def __init__(self, now: int):
        self.now = now

    def now_seconds(self) -> int:
        return self.now


class BrokenReplayStore:
    def claim(self, device_id, nonce, *, expires_at, now):
        raise ReplayStoreError("test failure")


class AuthenticationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vector = json.loads(VECTOR_PATH.read_text(encoding="utf-8"))
        cls.secret = cls.vector["secret_utf8"].encode()
        cls.body = cls.vector["body_utf8"].encode()

    def setUp(self):
        self.metadata = AuthMetadata(
            version=self.vector["version"],
            device_id=self.vector["device_id"],
            request_id=self.vector["request_id"],
            timestamp=self.vector["timestamp"],
            nonce=self.vector["nonce"],
        )
        self.headers = self.metadata.to_headers(self.vector["signature_hex"])
        self.secrets = InMemoryDeviceSecretStore(
            {self.metadata.device_id: self.secret}
        )
        self.replay = InMemoryReplayStore()
        self.clock = FixedClock(self.metadata.timestamp)
        self.authenticator = RequestAuthenticator(
            self.secrets, self.replay, self.clock
        )

    def authenticate(self):
        return self.authenticator.authenticate(
            self.headers,
            self.vector["method"],
            self.vector["target"],
            self.body,
        )

    def assert_audit_code(self, expected, callback):
        with self.assertRaises(AuthenticationError) as caught:
            callback()
        self.assertEqual(caught.exception.public_code, ErrorCode.INVALID_AUTH)
        self.assertEqual(caught.exception.audit_code, expected)

    def test_accepts_valid_request(self):
        result = self.authenticate()
        self.assertEqual(result.metadata, self.metadata)

    def test_rejects_unknown_device_without_public_detail(self):
        self.secrets.revoke(self.metadata.device_id)
        self.assert_audit_code(ErrorCode.UNKNOWN_DEVICE, self.authenticate)

    def test_rejects_tampered_body(self):
        self.assert_audit_code(
            ErrorCode.SIGNATURE_MISMATCH,
            lambda: self.authenticator.authenticate(
                self.headers,
                self.vector["method"],
                self.vector["target"],
                self.body + b" ",
            ),
        )

    def test_rejects_past_and_future_timestamps(self):
        for offset in (-301, 301):
            with self.subTest(offset=offset):
                metadata = AuthMetadata(
                    device_id=self.metadata.device_id,
                    request_id=self.metadata.request_id,
                    timestamp=self.clock.now + offset,
                    nonce=("0" if offset < 0 else "1") * 32,
                )
                signature = calculate_signature(
                    self.secret,
                    self.vector["method"],
                    self.vector["target"],
                    self.body,
                    metadata,
                )
                headers = metadata.to_headers(signature)
                self.assert_audit_code(
                    ErrorCode.STALE_TIMESTAMP,
                    lambda: self.authenticator.authenticate(
                        headers,
                        self.vector["method"],
                        self.vector["target"],
                        self.body,
                    ),
                )

    def test_accepts_timestamp_window_boundaries(self):
        for index, offset in enumerate((-300, 300), start=2):
            with self.subTest(offset=offset):
                metadata = AuthMetadata(
                    device_id=self.metadata.device_id,
                    request_id=f"{index}" * 32,
                    timestamp=self.clock.now + offset,
                    nonce=f"{index}" * 32,
                )
                signature = calculate_signature(
                    self.secret,
                    self.vector["method"],
                    self.vector["target"],
                    self.body,
                    metadata,
                )
                result = self.authenticator.authenticate(
                    metadata.to_headers(signature),
                    self.vector["method"],
                    self.vector["target"],
                    self.body,
                )
                self.assertEqual(result.metadata, metadata)

    def test_retransmit_within_tolerance_is_accepted(self):
        # 串口隧道在 ACK 丢失时会重发同一请求（同 nonce）。短时间内视为重传放行，
        # 实际执行由 Action 层的 request_id 幂等兜底，不会重复执行。
        self.authenticate()
        result = self.authenticate()
        self.assertEqual(result.metadata, self.metadata)

    def test_replay_after_tolerance_is_rejected(self):
        self.replay.claim(
            self.metadata.device_id,
            self.metadata.nonce,
            expires_at=self.clock.now + 600,
            now=self.clock.now,
        )
        allowed = self.replay.claim(
            self.metadata.device_id,
            self.metadata.nonce,
            expires_at=self.clock.now + 600,
            now=self.clock.now + InMemoryReplayStore.RETRANSMIT_TOLERANCE_SECONDS + 1,
        )
        self.assertFalse(allowed)

    def test_nonce_retransmit_is_thread_safe(self):
        def attempt(_):
            try:
                self.authenticate()
                return "ok"
            except AuthenticationError as exc:
                return exc.audit_code

        with ThreadPoolExecutor(max_workers=16) as pool:
            results = list(pool.map(attempt, range(32)))
        # 同一请求的并发重传均被容忍（不再因重传被判 replay）。
        self.assertEqual(results.count("ok"), 32)

    def test_replay_store_capacity_fails_closed(self):
        replay = InMemoryReplayStore(max_entries=1)
        replay.claim("other", "f" * 32, expires_at=self.clock.now + 10, now=self.clock.now)
        authenticator = RequestAuthenticator(self.secrets, replay, self.clock)
        self.assert_audit_code(
            ErrorCode.INTERNAL_ERROR,
            lambda: authenticator.authenticate(
                self.headers,
                self.vector["method"],
                self.vector["target"],
                self.body,
            ),
        )

    def test_replay_store_failure_fails_closed(self):
        authenticator = RequestAuthenticator(
            self.secrets, BrokenReplayStore(), self.clock
        )
        self.assert_audit_code(
            ErrorCode.INTERNAL_ERROR,
            lambda: authenticator.authenticate(
                self.headers,
                self.vector["method"],
                self.vector["target"],
                self.body,
            ),
        )


if __name__ == "__main__":
    unittest.main()
