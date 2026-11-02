import json
from pathlib import Path
import unittest

from velaops_proxy.protocol import (
    AuthMetadata,
    ErrorCode,
    ProtocolValidationError,
    body_sha256,
    calculate_signature,
    canonical_request,
    verify_signature,
)


VECTOR_PATH = Path(__file__).parent / "vectors" / "hmac_v1.json"


class ProtocolTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vector = json.loads(VECTOR_PATH.read_text(encoding="utf-8"))
        cls.body = cls.vector["body_utf8"].encode()
        cls.secret = cls.vector["secret_utf8"].encode()
        cls.metadata = AuthMetadata(
            version=cls.vector["version"],
            device_id=cls.vector["device_id"],
            request_id=cls.vector["request_id"],
            timestamp=cls.vector["timestamp"],
            nonce=cls.vector["nonce"],
        )

    def test_fixed_hmac_vector(self):
        self.assertEqual(body_sha256(self.body), self.vector["body_sha256"])
        self.assertEqual(
            canonical_request(
                self.vector["method"],
                self.vector["target"],
                self.body,
                self.metadata,
            ).decode(),
            self.vector["canonical_utf8"],
        )
        self.assertEqual(
            calculate_signature(
                self.secret,
                self.vector["method"],
                self.vector["target"],
                self.body,
                self.metadata,
            ),
            self.vector["signature_hex"],
        )

    def test_tampered_body_fails_verification(self):
        self.assertFalse(
            verify_signature(
                self.vector["signature_hex"],
                self.secret,
                self.vector["method"],
                self.vector["target"],
                self.body + b" ",
                self.metadata,
            )
        )

    def test_headers_round_trip_case_insensitively(self):
        headers = self.metadata.to_headers(self.vector["signature_hex"])
        lowercase_headers = {name.lower(): value for name, value in headers.items()}
        parsed, signature = AuthMetadata.from_headers(lowercase_headers)
        self.assertEqual(parsed, self.metadata)
        self.assertEqual(signature, self.vector["signature_hex"])

    def test_rejects_unsupported_version(self):
        metadata = AuthMetadata(
            version="2",
            device_id=self.metadata.device_id,
            request_id=self.metadata.request_id,
            timestamp=self.metadata.timestamp,
            nonce=self.metadata.nonce,
        )
        with self.assertRaises(ProtocolValidationError) as caught:
            metadata.validate()
        self.assertEqual(caught.exception.code, ErrorCode.UNSUPPORTED_VERSION)

    def test_rejects_request_target_injection(self):
        with self.assertRaises(ProtocolValidationError):
            canonical_request("POST", "/v1/actions\nforged", b"", self.metadata)

    def test_rejects_non_ascii_request_target(self):
        with self.assertRaises(ProtocolValidationError):
            canonical_request("POST", "/v1/动作", b"", self.metadata)

    def test_rejects_short_secret(self):
        with self.assertRaises(ProtocolValidationError):
            calculate_signature(b"too-short", "POST", "/v1/actions", b"", self.metadata)


if __name__ == "__main__":
    unittest.main()
