from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import tempfile
import unittest

from velaops_proxy.actions import ActionError, ActionResult
from velaops_proxy.idempotency import ClaimState, SqliteExecutionStore
from velaops_proxy.protocol import ErrorCode


class IdempotencyTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.path = Path(self.temp_dir.name) / "state.db"
        self.store = SqliteExecutionStore(self.path)

    def tearDown(self):
        self.store.close()
        self.temp_dir.cleanup()

    def test_success_is_persistent_and_returns_cached_result(self):
        claim = self.store.begin("eye-001", "1" * 32, "a" * 64, 1000)
        self.assertEqual(claim.state, ClaimState.NEW)
        self.store.succeed(
            "eye-001", "1" * 32, ActionResult.of(restarted=True), 1001
        )
        cached = self.store.begin("eye-001", "1" * 32, "a" * 64, 1002)
        self.assertEqual(cached.state, ClaimState.CACHED)
        self.assertEqual(cached.cached_result.data["restarted"], True)

        self.store.close()
        self.store = SqliteExecutionStore(self.path)
        reopened = self.store.begin("eye-001", "1" * 32, "a" * 64, 1003)
        self.assertEqual(reopened.state, ClaimState.CACHED)

    def test_same_request_id_with_different_body_is_rejected(self):
        self.store.begin("eye-001", "2" * 32, "a" * 64, 1000)
        with self.assertRaises(ActionError) as caught:
            self.store.begin("eye-001", "2" * 32, "b" * 64, 1001)
        self.assertEqual(caught.exception.code, ErrorCode.IDEMPOTENCY_CONFLICT)

    def test_lookup_does_not_create_record(self):
        self.assertIsNone(self.store.lookup("eye-001", "9" * 32, "a" * 64))
        claim = self.store.begin("eye-001", "9" * 32, "a" * 64, 1000)
        self.assertEqual(claim.state, ClaimState.NEW)

    def test_same_approval_cannot_start_two_request_ids(self):
        self.store.begin(
            "eye-001", "5" * 32, "a" * 64, 1000, approval_id="c" * 32
        )
        with self.assertRaises(ActionError) as caught:
            self.store.begin(
                "eye-001", "6" * 32, "b" * 64, 1001, approval_id="c" * 32
            )
        self.assertEqual(caught.exception.code, ErrorCode.APPROVAL_REUSED)

    def test_running_and_failed_changes_are_never_replayed(self):
        self.store.begin("eye-001", "3" * 32, "a" * 64, 1000)
        with self.assertRaises(ActionError) as running:
            self.store.begin("eye-001", "3" * 32, "a" * 64, 1001)
        self.assertEqual(running.exception.code, ErrorCode.ACTION_STATE_UNCERTAIN)

        self.store.fail("eye-001", "3" * 32, ErrorCode.INTERNAL_ERROR, 1002)
        with self.assertRaises(ActionError) as failed:
            self.store.begin("eye-001", "3" * 32, "a" * 64, 1003)
        self.assertEqual(failed.exception.code, ErrorCode.ACTION_STATE_UNCERTAIN)

    def test_concurrent_claim_has_exactly_one_winner(self):
        def claim():
            try:
                return self.store.begin("eye-001", "4" * 32, "a" * 64, 1000).state
            except ActionError as exc:
                return exc.code

        with ThreadPoolExecutor(max_workers=16) as pool:
            outcomes = list(pool.map(lambda _: claim(), range(32)))
        self.assertEqual(outcomes.count(ClaimState.NEW), 1)
        self.assertEqual(outcomes.count(ErrorCode.ACTION_STATE_UNCERTAIN), 31)

    def test_rejects_insecure_or_symlink_database(self):
        self.store.close()
        os.chmod(self.path, 0o644)
        with self.assertRaises(ValueError):
            SqliteExecutionStore(self.path)

        target = Path(self.temp_dir.name) / "target.db"
        target.touch(mode=0o600)
        link = Path(self.temp_dir.name) / "link.db"
        link.symlink_to(target)
        with self.assertRaises(ValueError):
            SqliteExecutionStore(link)
        insecure_dir = Path(self.temp_dir.name) / "world-writable"
        insecure_dir.mkdir(mode=0o777)
        insecure_dir.chmod(0o777)
        with self.assertRaises(ValueError):
            SqliteExecutionStore(insecure_dir / "state.db")
        self.store = SqliteExecutionStore(Path(self.temp_dir.name) / "replacement.db")


if __name__ == "__main__":
    unittest.main()
