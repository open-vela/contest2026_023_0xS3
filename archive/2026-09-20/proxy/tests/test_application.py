import http.client
import json
from pathlib import Path
import socket
import tempfile
from threading import Thread
import unittest

from velaops_proxy.application import build_runtime


class ApplicationTest(unittest.TestCase):
    def test_builds_runnable_proxy_and_closes_resources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with socket.socket() as probe:
                probe.bind(("127.0.0.1", 0))
                port = probe.getsockname()[1]
            config = {
                "listen": {"host": "127.0.0.1", "port": port},
                "devices": {"eye-test": {"secret_hex": "01" * 32}},
                "targets": {
                    "local-dev": {
                        "allowed_actions": ["check_memory", "restart_service"],
                        "services": {
                            "proxy-test": {
                                "unit": "velaops-test.service",
                                "manager": "user",
                                "restart_via_sudo": False,
                            }
                        },
                        "ports": {},
                        "disks": {},
                    }
                },
                "storage": {
                    "state_database": str(root / "state.db"),
                    "audit_log": str(root / "audit.jsonl"),
                },
            }
            path = root / "proxy.json"
            path.write_text(json.dumps(config), encoding="utf-8")
            path.chmod(0o600)
            runtime = build_runtime(path)
            thread = Thread(target=runtime.server.serve_forever, daemon=True)
            thread.start()
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
            connection.request("GET", "/healthz")
            response = connection.getresponse()
            payload = json.loads(response.read())
            connection.close()
            self.assertEqual(response.status, 200)
            self.assertTrue(payload["ok"])
            runtime.server.shutdown()
            thread.join(timeout=2)
            runtime.close()
            self.assertTrue((root / "state.db").is_file())
            self.assertTrue((root / "audit.jsonl").is_file())


if __name__ == "__main__":
    unittest.main()
