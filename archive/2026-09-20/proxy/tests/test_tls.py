from pathlib import Path
import tempfile
import unittest

from velaops_proxy.config import TlsConfig
from velaops_proxy.tls import create_tls_context


class TlsTest(unittest.TestCase):
    def test_rejects_world_readable_key_and_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            certificate = root / "server.crt"
            key = root / "server.key"
            certificate.write_text("invalid certificate")
            key.write_text("private key")
            certificate.chmod(0o644)
            key.chmod(0o644)
            with self.assertRaises(ValueError):
                create_tls_context(TlsConfig(str(certificate), str(key)))

            key.chmod(0o600)
            link = root / "link.key"
            link.symlink_to(key)
            with self.assertRaises(ValueError):
                create_tls_context(TlsConfig(str(certificate), str(link)))


if __name__ == "__main__":
    unittest.main()
