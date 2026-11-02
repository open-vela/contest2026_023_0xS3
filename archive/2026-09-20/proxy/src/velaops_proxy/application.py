"""Proxy 应用装配根，集中创建适配器并管理生命周期。"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .actions import ActionRegistry
from .adapters import (
    InMemoryDeviceSecretStore,
    InMemoryReplayStore,
    LinuxSystemInspector,
    SystemClock,
)
from .approval import ApprovalPolicy
from .audit import JsonlAuditSink
from .auth import RequestAuthenticator
from .changes import GuardedChanges
from .config import ProxyConfig, load_config
from .diagnostics import ReadOnlyDiagnostics
from .dispatch import ActionDispatcher
from .http_api import ProxyApi
from .http_server import VelaOpsHttpServer, create_server
from .idempotency import SqliteExecutionStore
from .tls import create_tls_context


@dataclass(slots=True)
class ProxyRuntime:
    config: ProxyConfig
    server: VelaOpsHttpServer
    execution_store: SqliteExecutionStore
    audit_sink: JsonlAuditSink

    def close(self) -> None:
        self.server.server_close()
        self.execution_store.close()
        self.audit_sink.close()


def build_runtime(config_path: str | Path) -> ProxyRuntime:
    """从已安全校验的配置构建完整 Proxy，不使用全局可变状态。"""
    config = load_config(config_path)
    clock = SystemClock()
    secrets = InMemoryDeviceSecretStore(config.device_secrets)
    replay = InMemoryReplayStore(config.max_replay_entries)
    authenticator = RequestAuthenticator(
        secrets,
        replay,
        clock,
        allowed_clock_skew_seconds=config.allowed_clock_skew_seconds,
    )

    inspector = LinuxSystemInspector()
    registry = ActionRegistry()
    ReadOnlyDiagnostics(config.targets, inspector).register(registry)
    GuardedChanges(config.targets, inspector).register(registry)

    execution_store = SqliteExecutionStore(config.storage.state_database)
    try:
        audit_sink = JsonlAuditSink(config.storage.audit_log)
        dispatcher = ActionDispatcher(
            registry,
            ApprovalPolicy(),
            execution_store,
            clock,
        )
        api = ProxyApi(
            authenticator,
            registry,
            dispatcher,
            audit_sink=audit_sink,
            clock=clock,
        )
        tls_context = None if config.tls is None else create_tls_context(config.tls)
        server = create_server(
            (config.listen.host, config.listen.port), api, tls_context
        )
    except Exception:
        execution_store.close()
        if "audit_sink" in locals():
            audit_sink.close()
        raise
    return ProxyRuntime(config, server, execution_store, audit_sink)
