#!/usr/bin/env python3
"""串口调试脚本共享的命令脱敏工具。"""

from collections.abc import Sequence


_SENSITIVE_COMMANDS = {
    "mcp_add",
    "router_set",
    "set_exa_key",
    "set_feishu_app",
    "set_feishu_user_token",
    "set_llm",
    "set_news_key",
    "set_search_key",
    "set_tavily_key",
    "set_volc_asr",
    "set_volc_key",
    "set_weixin_token",
    "set_wifi",
}


def command_is_sensitive(arguments: Sequence[str]) -> bool:
    """判断命令是否可能携带凭据。"""
    if not arguments:
        return False

    name = arguments[0].lower()
    if name == "wapi" and len(arguments) > 1 and arguments[1].lower() == "psk":
        return True
    return name in _SENSITIVE_COMMANDS or any(
        marker in name for marker in ("key", "password", "secret", "token")
    )


def command_notice(prefix: str, arguments: Sequence[str]) -> str:
    """生成可记录到日志的发送提示。"""
    if command_is_sensitive(arguments):
        name = arguments[0] if arguments else "command"
        return f"{prefix}: [{name} <redacted>]"
    return f"{prefix}: [{' '.join(arguments)}]"


def sanitize_serial_output(output: str, arguments: Sequence[str]) -> str:
    """从设备回显中移除完整命令及其凭据参数。"""
    if not command_is_sensitive(arguments):
        return output

    sanitized = output
    command = " ".join(arguments)
    if command:
        sanitized = sanitized.replace(command, f"{arguments[0]} <redacted>")

    # 最长参数优先，避免短参数先替换后破坏长凭据匹配。
    values = sorted(set(arguments[1:]), key=len, reverse=True)
    for value in values:
        if value:
            sanitized = sanitized.replace(value, "<redacted>")
    return sanitized
