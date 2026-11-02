"""Proxy 安全配置文件加载与严格 schema 校验。"""

from __future__ import annotations

from dataclasses import dataclass
import ipaddress
import json
import os
from pathlib import Path
import re
import stat
from types import MappingProxyType
from typing import Any, Mapping

from .protocol import ProtocolValidationError, validate_device_id


MAX_CONFIG_BYTES = 64 * 1024
_SECRET_PATTERN = re.compile(r"[0-9a-f]{64}\Z")
_ALIAS_PATTERN = re.compile(r"[a-z][a-z0-9_-]{0,63}\Z")
_ACTION_PATTERN = re.compile(r"[a-z][a-z0-9_]{0,63}\Z")
_UNIT_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.@-]{0,126}\.service\Z")


class ConfigError(ValueError):
    """配置不安全或不符合严格 schema。"""


@dataclass(frozen=True, slots=True)
class ListenConfig:
    host: str
    port: int


@dataclass(frozen=True, slots=True)
class PortConfig:
    host: str
    port: int


@dataclass(frozen=True, slots=True)
class ServiceConfig:
    unit: str
    manager: str
    restart_via_sudo: bool


@dataclass(frozen=True, slots=True)
class StorageConfig:
    state_database: str
    audit_log: str


@dataclass(frozen=True, slots=True)
class TlsConfig:
    certificate: str
    private_key: str


@dataclass(frozen=True, slots=True)
class TargetConfig:
    allowed_actions: frozenset[str]
    services: Mapping[str, ServiceConfig]
    ports: Mapping[str, PortConfig]
    disks: Mapping[str, str]


@dataclass(frozen=True, slots=True)
class ProxyConfig:
    listen: ListenConfig
    device_secrets: Mapping[str, bytes]
    allowed_clock_skew_seconds: int
    max_replay_entries: int
    targets: Mapping[str, TargetConfig]
    storage: StorageConfig
    tls: TlsConfig | None
    allow_insecure_http: bool


def load_config(path: str | os.PathLike[str]) -> ProxyConfig:
    """从仅服务用户可读的普通文件加载配置，禁止跟随符号链接。"""
    config_path = Path(path)
    flags = os.O_RDONLY | os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW

    try:
        fd = os.open(config_path, flags)
    except OSError as exc:
        raise ConfigError(f"无法安全打开配置文件: {config_path}") from exc

    try:
        info = os.fstat(fd)
        _validate_file_security(info)
        if info.st_size <= 0 or info.st_size > MAX_CONFIG_BYTES:
            raise ConfigError("配置文件大小必须在 1 到 65536 字节之间")
        with os.fdopen(fd, "rb", closefd=False) as stream:
            raw = stream.read(MAX_CONFIG_BYTES + 1)
    finally:
        os.close(fd)

    try:
        text = raw.decode("utf-8")
        document = json.loads(text, object_pairs_hook=_reject_duplicate_keys)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ConfigError("配置文件不是有效的 UTF-8 JSON") from exc

    root = _require_object(document, "根配置")
    _require_exact_keys(
        root,
        required={"listen", "devices", "targets", "storage"},
        optional={
            "allowed_clock_skew_seconds",
            "max_replay_entries",
            "tls",
            "allow_insecure_http",
        },
        context="根配置",
    )

    listen = _parse_listen(root["listen"])
    devices = _parse_devices(root["devices"])
    targets = _parse_targets(root["targets"])
    storage = _parse_storage(root["storage"])
    tls = None if "tls" not in root else _parse_tls(root["tls"])
    allow_insecure_http = root.get("allow_insecure_http", False)
    if not isinstance(allow_insecure_http, bool):
        raise ConfigError("allow_insecure_http 必须是布尔值")
    if tls is not None and allow_insecure_http:
        raise ConfigError("TLS 与 allow_insecure_http 不能同时启用")
    if (
        tls is None
        and not _is_loopback_host(listen.host)
        and not allow_insecure_http
    ):
        raise ConfigError("局域网 HTTP 监听必须显式设置 allow_insecure_http=true")
    allowed_skew = _bounded_integer(
        root.get("allowed_clock_skew_seconds", 300),
        "allowed_clock_skew_seconds",
        minimum=0,
        maximum=3600,
    )
    max_replay = _bounded_integer(
        root.get("max_replay_entries", 4096),
        "max_replay_entries",
        minimum=1,
        maximum=1_000_000,
    )
    return ProxyConfig(
        listen=listen,
        device_secrets=MappingProxyType(devices),
        allowed_clock_skew_seconds=allowed_skew,
        max_replay_entries=max_replay,
        targets=MappingProxyType(targets),
        storage=storage,
        tls=tls,
        allow_insecure_http=allow_insecure_http,
    )


def _validate_file_security(info: os.stat_result) -> None:
    if not stat.S_ISREG(info.st_mode):
        raise ConfigError("配置路径必须是普通文件")
    if info.st_uid != os.geteuid():
        raise ConfigError("配置文件必须由当前服务用户拥有")
    if stat.S_IMODE(info.st_mode) & 0o077:
        raise ConfigError("配置文件不能授予组用户或其他用户任何权限")


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ConfigError(f"配置包含重复字段: {key}")
        result[key] = value
    return result


def _require_object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ConfigError(f"{context}必须是 JSON 对象")
    return value


def _require_exact_keys(
    value: Mapping[str, Any],
    *,
    required: set[str],
    optional: set[str] | None = None,
    context: str,
) -> None:
    keys = set(value)
    missing = required - keys
    unknown = keys - required - (optional or set())
    if missing:
        raise ConfigError(f"{context}缺少字段: {', '.join(sorted(missing))}")
    if unknown:
        raise ConfigError(f"{context}包含未知字段: {', '.join(sorted(unknown))}")


def _parse_listen(value: Any) -> ListenConfig:
    item = _require_object(value, "listen")
    _require_exact_keys(item, required={"host", "port"}, context="listen")
    host = item["host"]
    if (
        not isinstance(host, str)
        or not host
        or len(host) > 253
        or not host.isascii()
        or any(character.isspace() for character in host)
    ):
        raise ConfigError("listen.host 格式不合法")
    port = _bounded_integer(item["port"], "listen.port", 1, 65535)
    return ListenConfig(host, port)


def _parse_devices(value: Any) -> dict[str, bytes]:
    devices = _require_object(value, "devices")
    if not devices or len(devices) > 64:
        raise ConfigError("devices 数量必须在 1 到 64 之间")

    result: dict[str, bytes] = {}
    for device_id, raw_device in devices.items():
        try:
            validate_device_id(device_id)
        except ProtocolValidationError as exc:
            raise ConfigError(f"设备 ID 格式不合法: {device_id}") from exc
        device = _require_object(raw_device, f"devices.{device_id}")
        _require_exact_keys(
            device, required={"secret_hex"}, context=f"devices.{device_id}"
        )
        secret_hex = device["secret_hex"]
        if not isinstance(secret_hex, str) or not _SECRET_PATTERN.fullmatch(secret_hex):
            raise ConfigError(f"devices.{device_id}.secret_hex 必须是 64 位小写十六进制")
        if secret_hex == "0" * 64:
            raise ConfigError(f"devices.{device_id}.secret_hex 不能使用全零密钥")
        result[device_id] = bytes.fromhex(secret_hex)
    return result


def _parse_targets(value: Any) -> dict[str, TargetConfig]:
    targets = _require_object(value, "targets")
    if not targets or len(targets) > 32:
        raise ConfigError("targets 数量必须在 1 到 32 之间")
    result: dict[str, TargetConfig] = {}
    for target_id, raw_target in targets.items():
        if not isinstance(target_id, str) or not _ALIAS_PATTERN.fullmatch(target_id):
            raise ConfigError(f"target ID 格式不合法: {target_id}")
        target = _require_object(raw_target, f"targets.{target_id}")
        _require_exact_keys(
            target,
            required={"allowed_actions", "services", "ports", "disks"},
            context=f"targets.{target_id}",
        )
        actions = _parse_allowed_actions(target["allowed_actions"], target_id)
        services = _parse_services(target["services"], target_id)
        ports = _parse_ports(target["ports"], target_id)
        disks = _parse_disks(target["disks"], target_id)
        result[target_id] = TargetConfig(
            allowed_actions=frozenset(actions),
            services=MappingProxyType(services),
            ports=MappingProxyType(ports),
            disks=MappingProxyType(disks),
        )
    return result


def _parse_storage(value: Any) -> StorageConfig:
    storage = _require_object(value, "storage")
    _require_exact_keys(
        storage,
        required={"state_database", "audit_log"},
        context="storage",
    )
    state_database = _absolute_path(storage["state_database"], "storage.state_database")
    audit_log = _absolute_path(storage["audit_log"], "storage.audit_log")
    if state_database == audit_log:
        raise ConfigError("state_database 与 audit_log 不能是同一路径")
    return StorageConfig(state_database, audit_log)


def _parse_tls(value: Any) -> TlsConfig:
    tls = _require_object(value, "tls")
    _require_exact_keys(
        tls,
        required={"certificate", "private_key"},
        context="tls",
    )
    certificate = _absolute_path(tls["certificate"], "tls.certificate")
    private_key = _absolute_path(tls["private_key"], "tls.private_key")
    if certificate == private_key:
        raise ConfigError("TLS 证书和私钥不能是同一路径")
    return TlsConfig(certificate, private_key)


def _is_loopback_host(host: str) -> bool:
    if host == "localhost":
        return True
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


def _parse_allowed_actions(value: Any, target_id: str) -> list[str]:
    if not isinstance(value, list) or not value or len(value) > 32:
        raise ConfigError(f"targets.{target_id}.allowed_actions 数量必须在 1 到 32 之间")
    if any(not isinstance(item, str) or not _ACTION_PATTERN.fullmatch(item) for item in value):
        raise ConfigError(f"targets.{target_id}.allowed_actions 包含非法 Action 名")
    if len(value) != len(set(value)):
        raise ConfigError(f"targets.{target_id}.allowed_actions 不允许重复")
    return value


def _parse_services(value: Any, target_id: str) -> dict[str, ServiceConfig]:
    services = _require_object(value, f"targets.{target_id}.services")
    if len(services) > 64:
        raise ConfigError(f"targets.{target_id}.services 超过 64 项")
    result: dict[str, ServiceConfig] = {}
    for alias, raw_service in services.items():
        _validate_alias(alias, f"targets.{target_id}.services")
        service = _require_object(raw_service, f"targets.{target_id}.services.{alias}")
        _require_exact_keys(
            service,
            required={"unit", "manager", "restart_via_sudo"},
            context=f"targets.{target_id}.services.{alias}",
        )
        unit = service["unit"]
        if not isinstance(unit, str) or not _UNIT_PATTERN.fullmatch(unit):
            raise ConfigError(f"targets.{target_id}.services.{alias} 必须是 .service 单元")
        manager = service["manager"]
        if manager not in {"system", "user"}:
            raise ConfigError(f"targets.{target_id}.services.{alias}.manager 不合法")
        restart_via_sudo = service["restart_via_sudo"]
        if type(restart_via_sudo) is not bool:
            raise ConfigError(
                f"targets.{target_id}.services.{alias}.restart_via_sudo 必须是布尔值"
            )
        if manager == "user" and restart_via_sudo:
            raise ConfigError("用户级 systemd 服务不得通过 sudo 重启")
        result[alias] = ServiceConfig(unit, manager, restart_via_sudo)
    return result


def _parse_ports(value: Any, target_id: str) -> dict[str, PortConfig]:
    ports = _require_object(value, f"targets.{target_id}.ports")
    if len(ports) > 64:
        raise ConfigError(f"targets.{target_id}.ports 超过 64 项")
    result: dict[str, PortConfig] = {}
    for alias, raw_port in ports.items():
        _validate_alias(alias, f"targets.{target_id}.ports")
        item = _require_object(raw_port, f"targets.{target_id}.ports.{alias}")
        _require_exact_keys(item, required={"host", "port"}, context=f"targets.{target_id}.ports.{alias}")
        host = item["host"]
        try:
            if not isinstance(host, str):
                raise ValueError
            ipaddress.ip_address(host)
        except ValueError as exc:
            raise ConfigError(f"targets.{target_id}.ports.{alias}.host 必须是 IP 字面量") from exc
        port = _bounded_integer(item["port"], f"targets.{target_id}.ports.{alias}.port", 1, 65535)
        result[alias] = PortConfig(host, port)
    return result


def _parse_disks(value: Any, target_id: str) -> dict[str, str]:
    disks = _require_object(value, f"targets.{target_id}.disks")
    if len(disks) > 64:
        raise ConfigError(f"targets.{target_id}.disks 超过 64 项")
    result: dict[str, str] = {}
    for alias, path in disks.items():
        _validate_alias(alias, f"targets.{target_id}.disks")
        result[alias] = _absolute_path(
            path, f"targets.{target_id}.disks.{alias}"
        )
    return result


def _validate_alias(value: Any, context: str) -> None:
    if not isinstance(value, str) or not _ALIAS_PATTERN.fullmatch(value):
        raise ConfigError(f"{context} 包含非法别名: {value}")


def _absolute_path(value: Any, context: str) -> str:
    if (
        not isinstance(value, str)
        or not os.path.isabs(value)
        or "\0" in value
        or len(value.encode("utf-8")) > 4096
    ):
        raise ConfigError(f"{context} 必须是受限绝对路径")
    return value


def _bounded_integer(
    value: Any,
    name: str,
    minimum: int,
    maximum: int,
) -> int:
    # bool 是 int 的子类，但配置中不能把 true/false 当作数值。
    if isinstance(value, bool) or not isinstance(value, int):
        raise ConfigError(f"{name} 必须是整数")
    if value < minimum or value > maximum:
        raise ConfigError(f"{name} 必须在 {minimum} 到 {maximum} 之间")
    return value
