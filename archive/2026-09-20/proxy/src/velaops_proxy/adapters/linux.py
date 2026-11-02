"""Linux 本机只读诊断适配器。"""

from __future__ import annotations

from pathlib import Path
import os
import shutil
import socket
import time

from ..diagnostics import (
    CpuSnapshot,
    DiskSnapshot,
    LogSnapshot,
    MemorySnapshot,
    PortSnapshot,
    ProcessSnapshot,
    ServiceSnapshot,
)
from ..config import ServiceConfig
from ..executor import BoundedCommandExecutor, CommandResult, CommandSpec
from ..protocol import ErrorCode
from ..actions import ActionError
from ..redaction import redact_text


class LinuxSystemInspector:
    """仅使用固定绝对路径程序和配置中的白名单参数。"""

    def __init__(self, executor: BoundedCommandExecutor | None = None) -> None:
        self._executor = executor or BoundedCommandExecutor()
        # /proc/stat 是累计计数，需保存上次采样算区间使用率；进程内单例。
        self._cpu_previous: tuple[int, int] | None = None

    def service_status(self, service: ServiceConfig) -> ServiceSnapshot:
        result = self._run(
            self._systemctl_argv(
                service.manager,
                "show",
                "--no-pager",
                "--property=LoadState,ActiveState,SubState,Result,ExecMainStatus",
                "--",
                service.unit,
            ),
            max_bytes=8192,
            user_manager=service.manager == "user",
        )
        fields = self._key_values(result.stdout)
        status_text = fields.get("ExecMainStatus", "")
        return ServiceSnapshot(
            load_state=fields.get("LoadState", "unknown"),
            active_state=fields.get("ActiveState", "unknown"),
            sub_state=fields.get("SubState", "unknown"),
            result=fields.get("Result", "unknown"),
            main_exit_status=int(status_text) if status_text.isdecimal() else None,
        )

    def check_port(self, host: str, port: int) -> PortSnapshot:
        started = time.monotonic()
        try:
            with socket.create_connection((host, port), timeout=1.0):
                reachable = True
        except OSError:
            reachable = False
        return PortSnapshot(
            reachable=reachable,
            latency_ms=max(0, int((time.monotonic() - started) * 1000)),
        )

    def disk_usage(self, path: str) -> DiskSnapshot:
        usage = shutil.disk_usage(path)
        used = usage.total - usage.free
        percent = 0.0 if usage.total == 0 else round(used * 100 / usage.total, 2)
        return DiskSnapshot(usage.total, used, usage.free, percent)

    def memory_usage(self) -> MemorySnapshot:
        fields = self._meminfo(Path("/proc/meminfo").read_text(encoding="ascii"))
        total = fields["MemTotal"] * 1024
        available = fields["MemAvailable"] * 1024
        used = total - available
        percent = 0.0 if total == 0 else round(used * 100 / total, 2)
        return MemorySnapshot(total, available, used, percent)

    def cpu_usage(self) -> CpuSnapshot:
        cores = os.cpu_count() or 1
        total, idle = self._cpu_stat(Path("/proc/stat").read_text(encoding="ascii"))
        previous = self._cpu_previous
        self._cpu_previous = (total, idle)

        load1, load5, load15 = self._load_average()
        if previous is None or total <= previous[0]:
            # 首次采样没有区间可算，用平均负载估算，避免看板显示 0 误导。
            used_percent = min(100.0, round(load1 * 100 / cores, 2))
        else:
            delta_total = total - previous[0]
            delta_idle = idle - previous[1]
            used_percent = (
                0.0
                if delta_total <= 0
                else round((delta_total - delta_idle) * 100 / delta_total, 2)
            )
        return CpuSnapshot(
            used_percent=max(0.0, used_percent),
            cores=cores,
            load1=load1,
            load5=load5,
            load15=load15,
        )

    def top_process(self) -> ProcessSnapshot:
        ps_path = shutil.which("ps") or "/bin/ps"
        # 用完整命令行（args）便于 LLM 指出"是谁在吃 CPU"（如 stress_cpu.py）。
        result = self._run(
            (ps_path, "-eo", "args=,pcpu=", "--sort=-pcpu"), max_bytes=16 * 1024
        )
        name = "unknown"
        percent = 0.0
        for line in result.stdout.splitlines():
            fields = line.split()
            if len(fields) < 2:
                continue
            try:
                percent = round(float(fields[-1]), 1)
            except ValueError:
                continue
            name = " ".join(fields[:-1])[:31]
            break
        return ProcessSnapshot(name=name, cpu_percent=percent)

    def service_log(self, service: ServiceConfig, lines: int) -> LogSnapshot:
        unit_option = "--user-unit" if service.manager == "user" else "--unit"
        result = self._run(
            (
                "/usr/bin/journalctl",
                "--no-pager",
                "--quiet",
                "--output=short-iso",
                f"--lines={lines}",
                unit_option,
                service.unit,
            ),
            max_bytes=32 * 1024,
        )
        return LogSnapshot(redact_text(result.stdout), result.stdout_truncated)

    def restart_service(self, service: ServiceConfig) -> ServiceSnapshot:
        argv = self._systemctl_argv(
            service.manager,
            "restart",
            "--",
            service.unit,
        )
        if service.restart_via_sudo:
            argv = ("/usr/bin/sudo", "-n", "--", *argv)
        self._run(
            argv,
            max_bytes=8192,
            user_manager=service.manager == "user",
            timeout_seconds=20,
        )
        return self.service_status(service)

    def _run(
        self,
        argv: tuple[str, ...],
        max_bytes: int,
        user_manager: bool = False,
        timeout_seconds: float = 5,
    ) -> CommandResult:
        environment = None
        if user_manager:
            environment = {"XDG_RUNTIME_DIR": f"/run/user/{os.geteuid()}"}
        result = self._executor.run(
            CommandSpec(
                argv,
                timeout_seconds=timeout_seconds,
                max_stream_bytes=max_bytes,
                env=environment,
            )
        )
        if result.timed_out:
            raise ActionError(ErrorCode.EXECUTION_TIMEOUT, "系统诊断超时")
        if result.exit_code != 0:
            raise ActionError(ErrorCode.INTERNAL_ERROR, "系统诊断失败")
        return result

    @staticmethod
    def _systemctl_argv(manager: str, *arguments: str) -> tuple[str, ...]:
        if manager == "system":
            return ("/usr/bin/systemctl", *arguments)
        if manager == "user":
            return ("/usr/bin/systemctl", "--user", *arguments)
        raise ValueError("systemd manager 不合法")

    @staticmethod
    def _key_values(text: str) -> dict[str, str]:
        result: dict[str, str] = {}
        for line in text.splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                result[key] = value
        return result

    @staticmethod
    def _load_average() -> tuple[float, float, float]:
        try:
            load1, load5, load15 = os.getloadavg()
        except (OSError, AttributeError):
            return 0.0, 0.0, 0.0
        return round(load1, 2), round(load5, 2), round(load15, 2)

    @staticmethod
    def _cpu_stat(text: str) -> tuple[int, int]:
        for line in text.splitlines():
            if not line.startswith("cpu "):
                continue
            parts = line.split()[1:]
            if len(parts) < 4 or not all(part.isdecimal() for part in parts):
                raise RuntimeError("/proc/stat CPU 字段不合法")
            values = [int(part) for part in parts]
            idle = values[3] + (values[4] if len(values) > 4 else 0)
            return sum(values), idle
        raise RuntimeError("/proc/stat 缺少 CPU 汇总行")

    @staticmethod
    def _meminfo(text: str) -> dict[str, int]:
        values: dict[str, int] = {}
        for line in text.splitlines():
            parts = line.replace(":", "").split()
            if len(parts) >= 2 and parts[0] in {"MemTotal", "MemAvailable"}:
                if not parts[1].isdecimal():
                    raise RuntimeError("/proc/meminfo 数值不合法")
                values[parts[0]] = int(parts[1])
        if set(values) != {"MemTotal", "MemAvailable"}:
            raise RuntimeError("/proc/meminfo 缺少内存字段")
        return values
