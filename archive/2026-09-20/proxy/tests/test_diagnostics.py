import unittest

from velaops_proxy.actions import ActionError, ActionRegistry, ActionRequest
from velaops_proxy.config import PortConfig, ServiceConfig, TargetConfig
from velaops_proxy.diagnostics import (
    CpuSnapshot,
    DiskSnapshot,
    LogSnapshot,
    MemorySnapshot,
    PortSnapshot,
    ProcessSnapshot,
    ReadOnlyDiagnostics,
    ServiceSnapshot,
)
from velaops_proxy.protocol import ErrorCode


class FakeInspector:
    def service_status(self, service):
        self.last = ("service", service.unit, service.manager)
        return ServiceSnapshot("loaded", "active", "running", "success", 0)

    def check_port(self, host, port):
        self.last = ("port", host, port)
        return PortSnapshot(True, 2)

    def disk_usage(self, path):
        self.last = ("disk", path)
        return DiskSnapshot(100, 40, 60, 40.0)

    def memory_usage(self):
        self.last = ("memory",)
        return MemorySnapshot(100, 30, 70, 70.0)

    def cpu_usage(self):
        self.last = ("cpu",)
        return CpuSnapshot(12.5, 8, 0.5, 0.4, 0.3)

    def top_process(self):
        self.last = ("top_process",)
        return ProcessSnapshot("stress_cpu", 96.5)

    def service_log(self, service, lines):
        self.last = ("log", service.unit, service.manager, lines)
        return LogSnapshot("safe log", False)


def request(action, parameters, target="local-dev"):
    return ActionRequest.from_payload(
        {
            "schema_version": 1,
            "action": action,
            "target": target,
            "parameters": parameters,
        }
    )


class DiagnosticsTest(unittest.TestCase):
    def setUp(self):
        self.inspector = FakeInspector()
        self.target = TargetConfig(
            allowed_actions=frozenset(ReadOnlyDiagnostics.ACTIONS),
            services={
                "proxy-test": ServiceConfig("velaops-test.service", "user", False)
            },
            ports={"proxy-http": PortConfig("127.0.0.1", 28790)},
            disks={"workspace": "/srv/velaops-test"},
        )
        self.diagnostics = ReadOnlyDiagnostics(
            {"local-dev": self.target}, self.inspector
        )
        self.registry = ActionRegistry()
        self.diagnostics.register(self.registry)

    def test_all_read_only_actions_use_configured_resources(self):
        cases = (
            ("check_service", {"service": "proxy-test"}, ("service", "velaops-test.service", "user")),
            ("check_port", {"port": "proxy-http"}, ("port", "127.0.0.1", 28790)),
            ("check_disk", {"disk": "workspace"}, ("disk", "/srv/velaops-test")),
            ("check_memory", {}, ("memory",)),
            ("read_service_log", {"service": "proxy-test", "lines": 20}, ("log", "velaops-test.service", "user", 20)),
        )
        for action, parameters, expected_call in cases:
            with self.subTest(action=action):
                result = self.registry.execute(request(action, parameters))
                self.assertEqual(self.inspector.last, expected_call)
                self.assertTrue(result.data)

    def test_check_resources_includes_cpu_snapshot(self):
        target = TargetConfig(
            allowed_actions=frozenset({"check_resources"}),
            services=self.target.services,
            ports=self.target.ports,
            disks=self.target.disks,
        )
        diagnostics = ReadOnlyDiagnostics({"local-dev": target}, self.inspector)
        registry = ActionRegistry()
        diagnostics.register(registry)
        result = registry.execute(request("check_resources", {}))
        cpu = result.data["cpu"]
        self.assertEqual(cpu["used_percent"], 12.5)
        self.assertEqual(cpu["cores"], 8)
        self.assertEqual(cpu["load1"], 0.5)
        self.assertEqual(cpu["load5"], 0.4)
        self.assertEqual(cpu["load15"], 0.3)

    def test_rejects_unknown_target_resource_and_parameters(self):
        invalid = (
            request("check_service", {"service": "unknown"}),
            request("check_port", {"port": "proxy-http", "extra": True}),
            request("check_memory", {"unexpected": True}),
            request("read_service_log", {"service": "proxy-test", "lines": 0}),
            request("check_memory", {}, target="unknown"),
        )
        for item in invalid:
            with self.subTest(request=item):
                with self.assertRaises(ActionError):
                    self.registry.execute(item)

    def test_target_action_allowlist_is_enforced(self):
        restricted = TargetConfig(
            allowed_actions=frozenset({"check_memory"}),
            services=self.target.services,
            ports=self.target.ports,
            disks=self.target.disks,
        )
        diagnostics = ReadOnlyDiagnostics({"local-dev": restricted}, self.inspector)
        with self.assertRaises(ActionError) as caught:
            diagnostics.check_service(request("check_service", {"service": "proxy-test"}))
        self.assertEqual(caught.exception.code, ErrorCode.ACTION_NOT_ALLOWED)


if __name__ == "__main__":
    unittest.main()
