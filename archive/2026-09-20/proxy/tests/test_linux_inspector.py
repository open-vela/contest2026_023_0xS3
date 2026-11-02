import unittest

from velaops_proxy.actions import ActionError
from velaops_proxy.config import ServiceConfig
from velaops_proxy.adapters.linux import LinuxSystemInspector
from velaops_proxy.executor import CommandResult
from velaops_proxy.redaction import REDACTED, redact_text


class FakeExecutor:
    def __init__(self, result):
        self.result = result
        self.spec = None
        self.specs = []

    def run(self, spec):
        self.spec = spec
        self.specs.append(spec)
        return self.result


class LinuxInspectorTest(unittest.TestCase):
    def test_service_status_uses_fixed_systemctl_argv(self):
        executor = FakeExecutor(
            CommandResult(
                0,
                "LoadState=loaded\nActiveState=active\nSubState=running\nResult=success\nExecMainStatus=0\n",
                "",
                False,
                False,
                False,
                1,
            )
        )
        snapshot = LinuxSystemInspector(executor).service_status(
            ServiceConfig("velaops-test.service", "system", False)
        )
        self.assertEqual(snapshot.active_state, "active")
        self.assertEqual(snapshot.main_exit_status, 0)
        self.assertEqual(executor.spec.argv[-1], "velaops-test.service")
        self.assertEqual(executor.spec.argv[0], "/usr/bin/systemctl")

    def test_meminfo_parser(self):
        values = LinuxSystemInspector._meminfo(
            "MemTotal: 1000 kB\nMemAvailable: 250 kB\n"
        )
        self.assertEqual(values, {"MemTotal": 1000, "MemAvailable": 250})
        with self.assertRaises(RuntimeError):
            LinuxSystemInspector._meminfo("MemTotal: invalid kB\n")

    def test_cpu_stat_parser_sums_total_and_idle(self):
        total, idle = LinuxSystemInspector._cpu_stat(
            "cpu  10 20 30 400 50 0 0 0 0 0\ncpu0 1 2 3 4 5 0 0 0 0 0\n"
        )
        self.assertEqual(total, 10 + 20 + 30 + 400 + 50)
        self.assertEqual(idle, 400 + 50)
        with self.assertRaises(RuntimeError):
            LinuxSystemInspector._cpu_stat("cpu0 1 2 3 4\n")

    def test_cpu_usage_smoke(self):
        snapshot = LinuxSystemInspector().cpu_usage()
        self.assertGreaterEqual(snapshot.cores, 1)
        self.assertGreaterEqual(snapshot.used_percent, 0.0)
        self.assertLessEqual(snapshot.used_percent, 100.0)
        self.assertGreaterEqual(snapshot.load1, 0.0)

    def test_command_failure_is_not_reported_as_valid_snapshot(self):
        executor = FakeExecutor(
            CommandResult(1, "", "denied", False, False, False, 1)
        )
        with self.assertRaises(ActionError):
            LinuxSystemInspector(executor).service_status(
                ServiceConfig("velaops-test.service", "system", False)
            )

    def test_user_manager_has_fixed_flag_and_runtime_directory(self):
        executor = FakeExecutor(
            CommandResult(
                0,
                "LoadState=loaded\nActiveState=inactive\n",
                "",
                False,
                False,
                False,
                1,
            )
        )
        LinuxSystemInspector(executor).service_status(
            ServiceConfig("velaops-test.service", "user", False)
        )
        self.assertEqual(executor.spec.argv[:2], ("/usr/bin/systemctl", "--user"))
        self.assertTrue(executor.spec.env["XDG_RUNTIME_DIR"].startswith("/run/user/"))

    def test_restart_uses_fixed_argv_then_verifies_status(self):
        executor = FakeExecutor(
            CommandResult(
                0,
                "LoadState=loaded\nActiveState=active\nSubState=running\nResult=success\nExecMainStatus=0\n",
                "",
                False,
                False,
                False,
                1,
            )
        )
        snapshot = LinuxSystemInspector(executor).restart_service(
            ServiceConfig("velaops-test.service", "user", False)
        )
        self.assertEqual(executor.specs[0].argv[2], "restart")
        self.assertEqual(executor.specs[0].argv[-1], "velaops-test.service")
        # 重启命令后必须再发起独立 show 复核。
        self.assertEqual(executor.spec.argv[2], "show")
        self.assertEqual(snapshot.active_state, "active")

    def test_system_restart_can_use_noninteractive_sudo_wrapper(self):
        executor = FakeExecutor(
            CommandResult(
                0,
                "LoadState=loaded\nActiveState=active\n",
                "",
                False,
                False,
                False,
                1,
            )
        )
        LinuxSystemInspector(executor).restart_service(
            ServiceConfig("nginx.service", "system", True)
        )
        self.assertEqual(
            executor.specs[0].argv,
            (
                "/usr/bin/sudo",
                "-n",
                "--",
                "/usr/bin/systemctl",
                "restart",
                "--",
                "nginx.service",
            ),
        )

    def test_redacts_common_credentials_and_private_keys(self):
        text = (
            "password=hunter2 token:abc api_key=xyz\n"
            "Authorization: Bearer bearer-value\n"
            "-----BEGIN PRIVATE KEY-----\nsecret\n-----END PRIVATE KEY-----"
        )
        redacted = redact_text(text)
        for secret in ("hunter2", "abc", "xyz", "bearer-value", "PRIVATE KEY"):
            self.assertNotIn(secret, redacted)
        self.assertGreaterEqual(redacted.count(REDACTED), 5)


if __name__ == "__main__":
    unittest.main()
