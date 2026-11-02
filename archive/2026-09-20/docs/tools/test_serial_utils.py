#!/usr/bin/env python3
"""串口命令脱敏工具的主机端单元测试。"""

import unittest

from serial_utils import (
    command_is_sensitive,
    command_notice,
    sanitize_serial_output,
)


class SerialUtilsTest(unittest.TestCase):
    def test_plain_command_remains_visible(self):
        arguments = ["net_status"]
        self.assertFalse(command_is_sensitive(arguments))
        self.assertEqual(command_notice("SENDING", arguments),
                         "SENDING: [net_status]")
        self.assertEqual(sanitize_serial_output("net_status\r\n", arguments),
                         "net_status\r\n")

    def test_known_secret_command_is_redacted(self):
        arguments = ["set_wifi", "private-ssid", "private-password"]
        output = (
            "set_wifi private-ssid private-password\r\n"
            "Connecting to private-ssid\r\n"
        )

        self.assertTrue(command_is_sensitive(arguments))
        self.assertEqual(command_notice("SENT", arguments),
                         "SENT: [set_wifi <redacted>]")
        sanitized = sanitize_serial_output(output, arguments)
        self.assertNotIn("private-ssid", sanitized)
        self.assertNotIn("private-password", sanitized)

    def test_secret_keyword_command_is_redacted(self):
        arguments = ["custom_token", "value-123"]
        self.assertTrue(command_is_sensitive(arguments))
        self.assertNotIn("value-123", command_notice("SENDING", arguments))

    def test_wapi_psk_is_redacted(self):
        arguments = ["wapi", "psk", "wlan0", "private-password", "3", "2"]
        output = "wapi psk wlan0 private-password 3 2\r\nnsh> "

        self.assertTrue(command_is_sensitive(arguments))
        self.assertNotIn("private-password", command_notice("SENDING", arguments))
        self.assertNotIn("private-password",
                         sanitize_serial_output(output, arguments))

    def test_longest_value_is_replaced_first(self):
        arguments = ["set_llm", "mimo", "mimo-secret"]
        sanitized = sanitize_serial_output(
            "set_llm mimo mimo-secret\r\nmodel=mimo\r\n", arguments)
        self.assertNotIn("mimo-secret", sanitized)
        self.assertNotIn("mimo", sanitized)


if __name__ == "__main__":
    unittest.main()
