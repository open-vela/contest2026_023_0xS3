import struct
import unittest

from velaops_proxy.lan_ntp import NTP_EPOCH_SECONDS, build_response


class LanNtpTest(unittest.TestCase):
    def test_builds_ntpv4_server_response(self):
        request = bytearray(48)
        request[40:48] = b"12345678"
        response = build_response(bytes(request), 1_700_000_000.5)

        self.assertEqual(len(response), 48)
        self.assertEqual(response[0], 0x24)
        self.assertEqual(response[1], 2)
        self.assertEqual(response[24:32], b"12345678")
        seconds, fraction = struct.unpack("!II", response[40:48])
        self.assertEqual(seconds, NTP_EPOCH_SECONDS + 1_700_000_000)
        self.assertEqual(fraction, 1 << 31)

    def test_rejects_short_request(self):
        with self.assertRaises(ValueError):
            build_response(b"short", 1_700_000_000.0)


if __name__ == "__main__":
    unittest.main()
