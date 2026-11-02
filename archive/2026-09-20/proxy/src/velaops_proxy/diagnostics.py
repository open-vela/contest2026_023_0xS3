"""P0 只读诊断 Action 用例与系统检查端口。"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Mapping, Protocol

from .actions import ActionError, ActionRegistry, ActionRequest, ActionResult, RiskLevel
from .config import ServiceConfig, TargetConfig
from .protocol import ErrorCode


@dataclass(frozen=True, slots=True)
class ServiceSnapshot:
    load_state: str
    active_state: str
    sub_state: str
    result: str
    main_exit_status: int | None


@dataclass(frozen=True, slots=True)
class PortSnapshot:
    reachable: bool
    latency_ms: int


@dataclass(frozen=True, slots=True)
class DiskSnapshot:
    total_bytes: int
    used_bytes: int
    free_bytes: int
    used_percent: float


@dataclass(frozen=True, slots=True)
class MemorySnapshot:
    total_bytes: int
    available_bytes: int
    used_bytes: int
    used_percent: float


@dataclass(frozen=True, slots=True)
class CpuSnapshot:
    """一次采样的 CPU 概要：整体使用率、核数与 1/5/15 分钟平均负载。"""

    used_percent: float
    cores: int
    load1: float
    load5: float
    load15: float


@dataclass(frozen=True, slots=True)
class ProcessSnapshot:
    """CPU 占用最高的进程（用于让 LLM 指出"是谁在吃 CPU"）。"""

    name: str
    cpu_percent: float


@dataclass(frozen=True, slots=True)
class LogSnapshot:
    text: str
    truncated: bool


@dataclass(frozen=True, slots=True)
class ResourceSnapshot:
    """一次只读巡检返回的服务器资源摘要。"""

    memory: MemorySnapshot
    disk: DiskSnapshot
    service: ServiceSnapshot
    port: PortSnapshot


class SystemInspector(Protocol):
    """隔离 Linux 系统调用，使 Action 用例可独立测试。"""

    def service_status(self, service: ServiceConfig) -> ServiceSnapshot: ...

    def check_port(self, host: str, port: int) -> PortSnapshot: ...

    def disk_usage(self, path: str) -> DiskSnapshot: ...

    def memory_usage(self) -> MemorySnapshot: ...

    def cpu_usage(self) -> CpuSnapshot: ...

    def top_process(self) -> ProcessSnapshot: ...

    def service_log(self, service: ServiceConfig, lines: int) -> LogSnapshot: ...


class ReadOnlyDiagnostics:
    """将配置别名解析为受信系统资源，不接受任意路径或 unit。"""

    ACTIONS = (
        "check_service",
        "check_port",
        "check_disk",
        "check_memory",
        "read_service_log",
    )

    def __init__(
        self,
        targets: Mapping[str, TargetConfig],
        inspector: SystemInspector,
    ) -> None:
        self._targets = targets
        self._inspector = inspector

    def register(self, registry: ActionRegistry) -> None:
        registry.register("check_service", RiskLevel.READ_ONLY, self.check_service)
        registry.register("check_port", RiskLevel.READ_ONLY, self.check_port)
        registry.register("check_disk", RiskLevel.READ_ONLY, self.check_disk)
        registry.register("check_memory", RiskLevel.READ_ONLY, self.check_memory)
        registry.register("check_resources", RiskLevel.READ_ONLY, self.check_resources)
        registry.register(
            "read_service_log", RiskLevel.READ_ONLY, self.read_service_log
        )

    def check_service(self, request: ActionRequest) -> ActionResult:
        target = self._authorize(request)
        service = self._required_alias(request, "service", target.services)
        snapshot = self._inspector.service_status(target.services[service])
        return ActionResult.of(service=service, **asdict(snapshot))

    def check_port(self, request: ActionRequest) -> ActionResult:
        target = self._authorize(request)
        port_alias = self._required_alias(request, "port", target.ports)
        endpoint = target.ports[port_alias]
        snapshot = self._inspector.check_port(endpoint.host, endpoint.port)
        return ActionResult.of(port=port_alias, **asdict(snapshot))

    def check_disk(self, request: ActionRequest) -> ActionResult:
        target = self._authorize(request)
        disk = self._required_alias(request, "disk", target.disks)
        snapshot = self._inspector.disk_usage(target.disks[disk])
        return ActionResult.of(disk=disk, **asdict(snapshot))

    def check_memory(self, request: ActionRequest) -> ActionResult:
        self._authorize(request)
        self._require_exact_parameters(request, set())
        return ActionResult.of(**asdict(self._inspector.memory_usage()))

    def check_resources(self, request: ActionRequest) -> ActionResult:
        target = self._authorize(request)
        self._require_exact_parameters(request, set())
        if not target.disks or not target.services or not target.ports:
            raise ActionError(ErrorCode.ACTION_NOT_ALLOWED, "资源白名单不完整")
        disk = self._alias_value(next(iter(target.disks)), target.disks)
        service = self._alias_value(next(iter(target.services)), target.services)
        port = self._alias_value(next(iter(target.ports)), target.ports)
        disk_snapshot = self._inspector.disk_usage(target.disks[disk])
        service_snapshot = self._inspector.service_status(target.services[service])
        endpoint = target.ports[port]
        port_snapshot = self._inspector.check_port(endpoint.host, endpoint.port)
        return ActionResult.of(
            cpu=asdict(self._inspector.cpu_usage()),
            top_process=asdict(self._inspector.top_process()),
            memory=asdict(self._inspector.memory_usage()),
            disk={"alias": disk, **asdict(disk_snapshot)},
            service={"alias": service, **asdict(service_snapshot)},
            port={"alias": port, **asdict(port_snapshot)},
        )

    def read_service_log(self, request: ActionRequest) -> ActionResult:
        target = self._authorize(request)
        self._require_exact_parameters(request, {"service", "lines"})
        service = self._alias_value(request.parameters["service"], target.services)
        lines = request.parameters["lines"]
        if type(lines) is not int or lines < 1 or lines > 200:
            raise ActionError(
                ErrorCode.INVALID_ACTION_PARAMETERS,
                "lines 必须是 1 到 200 的整数",
            )
        snapshot = self._inspector.service_log(target.services[service], lines)
        return ActionResult.of(service=service, **asdict(snapshot))

    def _authorize(self, request: ActionRequest) -> TargetConfig:
        try:
            target = self._targets[request.target]
        except KeyError as exc:
            raise ActionError(ErrorCode.ACTION_NOT_ALLOWED, "目标不在白名单中") from exc
        if request.action not in target.allowed_actions:
            raise ActionError(ErrorCode.ACTION_NOT_ALLOWED, "目标未授权该 Action")
        return target

    def _required_alias(
        self,
        request: ActionRequest,
        parameter: str,
        resources: Mapping[str, object],
    ) -> str:
        self._require_exact_parameters(request, {parameter})
        return self._alias_value(request.parameters[parameter], resources)

    @staticmethod
    def _alias_value(value: object, resources: Mapping[str, object]) -> str:
        if not isinstance(value, str) or value not in resources:
            raise ActionError(ErrorCode.INVALID_ACTION_PARAMETERS, "资源别名不在白名单中")
        return value

    @staticmethod
    def _require_exact_parameters(request: ActionRequest, expected: set[str]) -> None:
        if set(request.parameters) != expected:
            raise ActionError(
                ErrorCode.INVALID_ACTION_PARAMETERS,
                "Action 参数不完整或包含未知字段",
            )
