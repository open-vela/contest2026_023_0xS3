import os
from pathlib import Path
import tempfile
import unittest

from velaops_proxy.executor import BoundedCommandExecutor, CommandSpec


PYTHON = "/usr/bin/python3"


class ExecutorTest(unittest.TestCase):
    def setUp(self):
        self.executor = BoundedCommandExecutor()

    def test_captures_stdout_stderr_and_exit_code(self):
        result = self.executor.run(
            CommandSpec(
                (PYTHON, "-c", "import sys; print('out'); print('err', file=sys.stderr); sys.exit(7)")
            )
        )
        self.assertEqual(result.exit_code, 7)
        self.assertEqual(result.stdout, "out\n")
        self.assertEqual(result.stderr, "err\n")
        self.assertFalse(result.timed_out)

    def test_arguments_are_not_interpreted_by_shell(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "must-not-exist"
            literal = f"$(touch {marker})"
            result = self.executor.run(CommandSpec(("/usr/bin/printf", "%s", literal)))
            self.assertEqual(result.stdout, literal)
            self.assertFalse(marker.exists())

    def test_limits_both_output_streams_without_deadlock(self):
        result = self.executor.run(
            CommandSpec(
                (
                    PYTHON,
                    "-c",
                    "import os; os.write(1,b'a'*20000); os.write(2,b'b'*20000)",
                ),
                max_stream_bytes=1024,
            )
        )
        self.assertEqual(len(result.stdout), 1024)
        self.assertEqual(len(result.stderr), 1024)
        self.assertTrue(result.stdout_truncated)
        self.assertTrue(result.stderr_truncated)

    def test_timeout_kills_process_group(self):
        result = self.executor.run(
            CommandSpec(
                (PYTHON, "-c", "import time; time.sleep(5)"),
                timeout_seconds=0.1,
            )
        )
        self.assertTrue(result.timed_out)
        self.assertLess(result.duration_ms, 2000)
        self.assertNotEqual(result.exit_code, 0)

    def test_rejects_relative_executable_and_invalid_values(self):
        invalid = (
            CommandSpec(("echo", "value")),
            CommandSpec(("/bin/echo", "bad\0value")),
            CommandSpec(("/bin/echo",), timeout_seconds=0),
            CommandSpec(("/bin/echo",), timeout_seconds=float("nan")),
            CommandSpec(("/bin/echo",), max_stream_bytes=True),
            CommandSpec(("/bin/echo",), cwd="relative"),
            CommandSpec(("/bin/echo",), env={"VALID": 1}),
        )
        for spec in invalid:
            with self.subTest(spec=spec):
                with self.assertRaises(ValueError):
                    spec.validate()


if __name__ == "__main__":
    unittest.main()
