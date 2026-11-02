"""不经过 shell 的有界本地命令执行器。"""

from __future__ import annotations

from dataclasses import dataclass
import math
import os
import selectors
import signal
import subprocess
import time
from typing import Mapping


DEFAULT_MAX_STREAM_BYTES = 32 * 1024
MAX_ARGUMENTS = 32
MAX_ARGUMENT_BYTES = 4096
MAX_TIMEOUT_SECONDS = 60.0


@dataclass(frozen=True, slots=True)
class CommandSpec:
    argv: tuple[str, ...]
    timeout_seconds: float = 10.0
    max_stream_bytes: int = DEFAULT_MAX_STREAM_BYTES
    cwd: str = "/"
    env: Mapping[str, str] | None = None

    def validate(self) -> None:
        if not isinstance(self.argv, tuple) or not self.argv or len(self.argv) > MAX_ARGUMENTS:
            raise ValueError("命令参数数量不合法")
        if not isinstance(self.argv[0], str) or not os.path.isabs(self.argv[0]):
            raise ValueError("可执行文件必须使用绝对路径")
        total = 0
        for argument in self.argv:
            if not isinstance(argument, str) or not argument or "\0" in argument:
                raise ValueError("命令参数包含非法值")
            encoded = argument.encode("utf-8")
            total += len(encoded)
        if total > MAX_ARGUMENT_BYTES:
            raise ValueError("命令参数总长度超过限制")
        if (
            isinstance(self.timeout_seconds, bool)
            or not isinstance(self.timeout_seconds, (int, float))
            or not math.isfinite(self.timeout_seconds)
            or self.timeout_seconds <= 0
            or self.timeout_seconds > MAX_TIMEOUT_SECONDS
        ):
            raise ValueError("命令超时必须在 0 到 60 秒之间")
        if (
            type(self.max_stream_bytes) is not int
            or self.max_stream_bytes <= 0
            or self.max_stream_bytes > 1024 * 1024
        ):
            raise ValueError("单路输出上限不合法")
        if not isinstance(self.cwd, str) or not os.path.isabs(self.cwd):
            raise ValueError("工作目录必须使用绝对路径")
        for name, value in (self.env or {}).items():
            if (
                not isinstance(name, str)
                or not isinstance(value, str)
                or not name
                or "=" in name
                or "\0" in name
                or "\0" in value
            ):
                raise ValueError("环境变量包含非法值")


@dataclass(frozen=True, slots=True)
class CommandResult:
    exit_code: int
    stdout: str
    stderr: str
    stdout_truncated: bool
    stderr_truncated: bool
    timed_out: bool
    duration_ms: int


class BoundedCommandExecutor:
    """以参数数组执行固定程序，持续排空管道并限制内存占用。"""

    def run(self, spec: CommandSpec) -> CommandResult:
        spec.validate()
        environment = {"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C.UTF-8"}
        environment.update(spec.env or {})
        started = time.monotonic()
        process = subprocess.Popen(
            spec.argv,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=spec.cwd,
            env=environment,
            shell=False,
            close_fds=True,
            start_new_session=True,
            bufsize=0,
        )
        assert process.stdout is not None
        assert process.stderr is not None

        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ, "stdout")
        selector.register(process.stderr, selectors.EVENT_READ, "stderr")
        buffers = {"stdout": bytearray(), "stderr": bytearray()}
        truncated = {"stdout": False, "stderr": False}
        deadline = started + spec.timeout_seconds
        timed_out = False

        try:
            while selector.get_map():
                remaining = deadline - time.monotonic()
                if remaining <= 0 and not timed_out:
                    timed_out = True
                    self._kill_process_group(process)
                    remaining = 0

                events = selector.select(timeout=max(0.0, min(remaining, 0.1)))
                if not events and process.poll() is not None:
                    # 子进程已退出，继续非阻塞读取直到两个管道 EOF。
                    events = selector.select(timeout=0)
                for key, _ in events:
                    chunk = os.read(key.fileobj.fileno(), 4096)
                    if not chunk:
                        selector.unregister(key.fileobj)
                        key.fileobj.close()
                        continue
                    destination = buffers[key.data]
                    available = spec.max_stream_bytes - len(destination)
                    if available > 0:
                        destination.extend(chunk[:available])
                    if len(chunk) > available:
                        truncated[key.data] = True

                if timed_out and process.poll() is not None and not events:
                    # kill 后管道通常会立刻 EOF；循环继续排空，不提前丢数据。
                    continue
        finally:
            selector.close()
            if process.poll() is None:
                self._kill_process_group(process)
            exit_code = process.wait()

        duration_ms = int((time.monotonic() - started) * 1000)
        return CommandResult(
            exit_code=exit_code,
            stdout=buffers["stdout"].decode("utf-8", errors="replace"),
            stderr=buffers["stderr"].decode("utf-8", errors="replace"),
            stdout_truncated=truncated["stdout"],
            stderr_truncated=truncated["stderr"],
            timed_out=timed_out,
            duration_ms=duration_ms,
        )

    @staticmethod
    def _kill_process_group(process: subprocess.Popen[bytes]) -> None:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
