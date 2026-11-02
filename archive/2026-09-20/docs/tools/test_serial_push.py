#!/usr/bin/env python3
"""serial_push 纯函数主机单元测试。"""

import unittest

from serial_push import build_commands


class SerialPushTest(unittest.TestCase):
    def test_short_staging_path_reduces_serial_commands(self):
        data = b"x" * 1024
        direct = build_commands(data, "/data/ai_agent/skills/example.md")
        staged = build_commands(data, "/data/s")

        self.assertLess(len(staged), len(direct))
        self.assertTrue(all(len(command) <= 79 for command in staged))

    def test_chunks_round_trip_within_nsh_limit(self):
        data = bytes(range(256)) + b"secret-json\n"
        commands = build_commands(data, "/data/velaops/config.json")

        self.assertTrue(all(len(command) <= 79 for command in commands))
        rebuilt = bytearray()
        for command in commands[1:]:
            parts = command.split("'")[1::2]
            for encoded in parts:
                digits = encoded[2:]
                value = int(digits, 16)
                length = 4 if len(digits) >= 7 else 2 if len(digits) >= 3 else 1
                rebuilt.extend(value.to_bytes(length, "little"))
        self.assertEqual(bytes(rebuilt), data)

    def test_rejects_path_traversal(self):
        with self.assertRaises(ValueError):
            build_commands(b"data", "/data/../etc/config")

    def test_rejects_oversized_file(self):
        with self.assertRaises(ValueError):
            build_commands(b"x" * (16 * 1024 + 1), "/data/file")


if __name__ == "__main__":
    unittest.main()
