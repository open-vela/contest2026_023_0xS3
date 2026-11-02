import json
import os
from pathlib import Path
import tempfile
import unittest

from velaops_proxy.config import ConfigError, load_config


SECRET_HEX = "01" * 32


def valid_document():
    return {
        "listen": {"host": "127.0.0.1", "port": 28790},
        "devices": {"eye-001": {"secret_hex": SECRET_HEX}},
        "allowed_clock_skew_seconds": 300,
        "max_replay_entries": 4096,
        "targets": {
            "local-dev": {
                "allowed_actions": ["check_service", "check_port"],
                "services": {
                    "proxy-test": {
                        "unit": "velaops-test.service",
                        "manager": "user",
                        "restart_via_sudo": False,
                    }
                },
                "ports": {"proxy-http": {"host": "127.0.0.1", "port": 28790}},
                "disks": {"workspace": "/srv/velaops-test"},
            }
        },
        "storage": {
            "state_database": "/var/lib/velaops-proxy/state.db",
            "audit_log": "/var/log/velaops-proxy/audit.jsonl",
        },
    }


class ConfigTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.path = Path(self.temp_dir.name) / "proxy.json"

    def tearDown(self):
        self.temp_dir.cleanup()

    def write(self, value, mode=0o600):
        self.path.write_text(json.dumps(value), encoding="utf-8")
        self.path.chmod(mode)

    def assert_rejected(self, value):
        self.write(value)
        with self.assertRaises(ConfigError):
            load_config(self.path)

    def test_loads_valid_config(self):
        self.write(valid_document(), mode=0o400)
        config = load_config(self.path)
        self.assertEqual(config.listen.host, "127.0.0.1")
        self.assertEqual(config.listen.port, 28790)
        self.assertEqual(config.device_secrets["eye-001"], bytes.fromhex(SECRET_HEX))
        self.assertEqual(config.allowed_clock_skew_seconds, 300)
        self.assertEqual(config.max_replay_entries, 4096)
        self.assertFalse(config.allow_insecure_http)
        target = config.targets["local-dev"]
        self.assertEqual(target.services["proxy-test"].unit, "velaops-test.service")
        self.assertEqual(target.services["proxy-test"].manager, "user")
        self.assertFalse(target.services["proxy-test"].restart_via_sudo)
        self.assertEqual(target.ports["proxy-http"].port, 28790)
        self.assertEqual(target.disks["workspace"], "/srv/velaops-test")
        self.assertEqual(
            config.storage.state_database, "/var/lib/velaops-proxy/state.db"
        )
        with self.assertRaises(TypeError):
            target.services["other"] = {
                "unit": "other.service",
                "manager": "system",
                "restart_via_sudo": True,
            }

    def test_rejects_group_or_other_permissions(self):
        self.write(valid_document(), mode=0o640)
        with self.assertRaises(ConfigError):
            load_config(self.path)

    def test_rejects_symbolic_link(self):
        target = Path(self.temp_dir.name) / "target.json"
        target.write_text(json.dumps(valid_document()), encoding="utf-8")
        target.chmod(0o600)
        self.path.symlink_to(target)
        with self.assertRaises(ConfigError):
            load_config(self.path)

    def test_rejects_duplicate_keys(self):
        self.path.write_text(
            '{"listen":{},"listen":{},"devices":{}}', encoding="utf-8"
        )
        self.path.chmod(0o600)
        with self.assertRaises(ConfigError):
            load_config(self.path)

    def test_rejects_unknown_root_field(self):
        document = valid_document()
        document["unexpected"] = True
        self.assert_rejected(document)

    def test_rejects_invalid_device_id_and_secret(self):
        document = valid_document()
        document["devices"] = {"bad/device": {"secret_hex": SECRET_HEX}}
        self.assert_rejected(document)

        document = valid_document()
        document["devices"]["eye-001"]["secret_hex"] = "ab" * 31
        self.assert_rejected(document)

        document = valid_document()
        document["devices"]["eye-001"]["secret_hex"] = "0" * 64
        self.assert_rejected(document)

    def test_rejects_invalid_listen_values(self):
        for host, port in (("", 28790), ("127.0.0.1", 0), ("bad host", 80)):
            with self.subTest(host=host, port=port):
                document = valid_document()
                document["listen"] = {"host": host, "port": port}
                self.assert_rejected(document)

    def test_rejects_boolean_numeric_fields(self):
        document = valid_document()
        document["max_replay_entries"] = True
        self.assert_rejected(document)

    def test_rejects_oversized_file(self):
        self.path.write_bytes(b" " * (64 * 1024 + 1))
        os.chmod(self.path, 0o600)
        with self.assertRaises(ConfigError):
            load_config(self.path)

    def test_rejects_invalid_target_schema(self):
        invalid_targets = (
            {},
            {
                "bad/target": {
                    "allowed_actions": ["check_service"],
                    "services": {},
                    "ports": {},
                    "disks": {},
                }
            },
        )
        for targets in invalid_targets:
            with self.subTest(targets=targets):
                document = valid_document()
                document["targets"] = targets
                self.assert_rejected(document)

    def test_rejects_unbounded_target_resources(self):
        mutations = (
            lambda target: target.update(allowed_actions=["bad-action"]),
            lambda target: target.update(
                services={
                    "proxy": {
                        "unit": "../../evil",
                        "manager": "system",
                        "restart_via_sudo": True,
                    }
                }
            ),
            lambda target: target.update(
                services={
                    "proxy": {
                        "unit": "safe.service",
                        "manager": "remote",
                        "restart_via_sudo": False,
                    }
                }
            ),
            lambda target: target.update(
                services={
                    "proxy": {
                        "unit": "safe.service",
                        "manager": "user",
                        "restart_via_sudo": True,
                    }
                }
            ),
            lambda target: target.update(ports={"web": {"host": "example.com", "port": 80}}),
            lambda target: target.update(disks={"root": "relative/path"}),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                document = valid_document()
                mutate(document["targets"]["local-dev"])
                self.assert_rejected(document)

    def test_rejects_invalid_storage_paths(self):
        for storage in (
            {"state_database": "relative.db", "audit_log": "/tmp/audit"},
            {"state_database": "/tmp/shared", "audit_log": "/tmp/shared"},
        ):
            with self.subTest(storage=storage):
                document = valid_document()
                document["storage"] = storage
                self.assert_rejected(document)

    def test_requires_explicit_opt_in_for_lan_http(self):
        document = valid_document()
        document["listen"]["host"] = "0.0.0.0"
        self.assert_rejected(document)
        document["allow_insecure_http"] = True
        self.write(document)
        config = load_config(self.path)
        self.assertTrue(config.allow_insecure_http)
        self.assertIsNone(config.tls)

    def test_accepts_tls_for_non_loopback_listener(self):
        document = valid_document()
        document["listen"]["host"] = "0.0.0.0"
        document["tls"] = {
            "certificate": "/etc/velaops-proxy/server.crt",
            "private_key": "/etc/velaops-proxy/server.key",
        }
        self.write(document)
        config = load_config(self.path)
        self.assertEqual(config.tls.private_key, "/etc/velaops-proxy/server.key")

    def test_rejects_ambiguous_or_invalid_http_opt_in(self):
        document = valid_document()
        document["allow_insecure_http"] = "true"
        self.assert_rejected(document)

        document = valid_document()
        document["allow_insecure_http"] = True
        document["tls"] = {
            "certificate": "/etc/velaops-proxy/server.crt",
            "private_key": "/etc/velaops-proxy/server.key",
        }
        self.assert_rejected(document)


if __name__ == "__main__":
    unittest.main()
