#!/usr/bin/env python3
"""演示目标服务管理器测试。"""

from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from manage_demo_target import manage
from prepare_live_demo import TARGET_UNIT


class ManageDemoTargetTest(unittest.TestCase):
    def test_start_links_reloads_and_starts_fixed_unit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            unit = directory / TARGET_UNIT
            unit.write_text("[Service]\nExecStart=/bin/true\n", encoding="utf-8")
            calls: list[list[str]] = []

            def fake_runner(arguments, **kwargs):
                self.assertTrue(kwargs["check"])
                self.assertTrue(kwargs["text"])
                calls.append(arguments)
                return subprocess.CompletedProcess(arguments, 0)

            with patch("manage_demo_target._wait_for_target") as wait:
                manage("start", directory, runner=fake_runner)
            wait.assert_called_once_with()
            self.assertEqual(
                calls,
                [
                    [
                        "systemctl",
                        "--user",
                        "link",
                        "--force",
                        str(unit.resolve()),
                    ],
                    ["systemctl", "--user", "daemon-reload"],
                    ["systemctl", "--user", "start", TARGET_UNIT],
                ],
            )

    def test_stop_and_status_use_only_fixed_unit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / TARGET_UNIT).write_text("unit", encoding="utf-8")
            calls: list[list[str]] = []

            def fake_runner(arguments, **kwargs):
                calls.append(arguments)
                return subprocess.CompletedProcess(arguments, 0)

            manage("stop", directory, runner=fake_runner)
            manage("status", directory, runner=fake_runner)
            self.assertEqual(
                calls,
                [
                    ["systemctl", "--user", "stop", TARGET_UNIT],
                    ["systemctl", "--user", "status", "--no-pager", TARGET_UNIT],
                ],
            )

    def test_rejects_missing_or_symlink_unit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with self.assertRaises(ValueError):
                manage("start", directory)
            target = directory / "target.service"
            target.write_text("unit", encoding="utf-8")
            (directory / TARGET_UNIT).symlink_to(target)
            with self.assertRaises(ValueError):
                manage("start", directory)


if __name__ == "__main__":
    unittest.main(verbosity=2)
