"""返回设备前的有限日志脱敏。"""

from __future__ import annotations

import re


REDACTED = "[REDACTED]"
_PATTERNS = (
    re.compile(r"(?i)(\b(?:password|passwd|token|api[_-]?key|secret)\s*[=:]\s*)([^\s,;]+)"),
    re.compile(r"(?i)(\bauthorization\s*:\s*bearer\s+)([^\s]+)"),
    re.compile(r"-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----.*?-----END [A-Z0-9 ]*PRIVATE KEY-----", re.DOTALL),
)


def redact_text(text: str) -> str:
    """对常见凭据形式做保守脱敏；运维账号仍应从源头避免记录密钥。"""
    result = text
    for index, pattern in enumerate(_PATTERNS):
        if index < 2:
            result = pattern.sub(lambda match: match.group(1) + REDACTED, result)
        else:
            result = pattern.sub(REDACTED, result)
    return result
