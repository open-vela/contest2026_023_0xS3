#!/usr/bin/env python3
"""演示环境配置生成器测试。"""

from __future__ import annotations

import json
import os
from pathlib import Path
import stat
import tempfile
import unittest

from prepare_live_demo import DEFAULT_TARGET_PORT, TARGET_UNIT, prepare


class PrepareLiveDemoTest(unittest.TestCase):
    def test_creates_matching_private_configs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "live"
            prepare(directory, "192.0.2.10", 28790)

            proxy_path = directory / "proxy.json"
            device_path = directory / "device-config.json"
            unit_path = directory / TARGET_UNIT
            proxy = json.loads(proxy_path.read_text(encoding="utf-8"))
            device = json.loads(device_path.read_text(encoding="utf-8"))

            self.assertEqual(stat.S_IMODE(directory.stat().st_mode), 0o700)
            self.assertEqual(stat.S_IMODE(proxy_path.stat().st_mode), 0o600)
            self.assertEqual(stat.S_IMODE(device_path.stat().st_mode), 0o600)
            self.assertEqual(stat.S_IMODE(unit_path.stat().st_mode), 0o600)
            self.assertEqual(len(device["secret"]), 32)
            self.assertEqual(
                bytes.fromhex(proxy["devices"]["eye-001"]["secret_hex"]).decode(),
                device["secret"],
            )
            self.assertEqual(proxy["listen"], {"host": "192.0.2.10", "port": 28790})
            self.assertEqual(device["port"], "28790")
            self.assertTrue(proxy["allow_insecure_http"])
            target = proxy["targets"]["local-dev"]
            self.assertIn("restart_service", target["allowed_actions"])
            self.assertEqual(target["services"]["demo"]["unit"], TARGET_UNIT)
            self.assertEqual(
                target["ports"]["demo-http"],
                {"host": "127.0.0.1", "port": DEFAULT_TARGET_PORT},
            )
            self.assertIn(
                f"http.server {DEFAULT_TARGET_PORT} --bind 127.0.0.1",
                unit_path.read_text(encoding="utf-8"),
            )

    def test_refuses_implicit_secret_rotation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "live"
            prepare(directory, "192.0.2.10", 28790)
            before = (directory / "device-config.json").read_bytes()
            with self.assertRaises(FileExistsError):
                prepare(directory, "192.0.2.10", 28790)
            self.assertEqual((directory / "device-config.json").read_bytes(), before)

    def test_rejects_invalid_target_and_symlink_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(ValueError):
                prepare(root / "bad", "bad host", 28790)
            target = root / "target"
            target.mkdir()
            link = root / "link"
            os.symlink(target, link)
            with self.assertRaises(ValueError):
                prepare(link, "192.0.2.10", 28790)


if __name__ == "__main__":
    unittest.main(verbosity=2)
