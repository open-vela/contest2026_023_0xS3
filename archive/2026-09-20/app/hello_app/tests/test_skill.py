#!/usr/bin/env python3
"""Validate the custom Skill's safety and structured-output contract."""

from __future__ import annotations

import json
import re
from pathlib import Path


SKILL = Path(__file__).resolve().parent.parent / "skills" / "server-incident-response.md"


def main() -> None:
    text = SKILL.read_text(encoding="utf-8")
    lines = text.splitlines()
    assert 1000 <= len(text.encode("utf-8")) <= 8192
    # 标题必须与文件名一致，避免小模型按标题另行猜测 read_file 路径。
    assert lines[0] == f"# {SKILL.stem}"

    # 当前板端加载器从标题的下一行读到首个空行；标题后不能留空行，
    # 否则系统提示中只有技能名而没有路由约束。
    assert lines[1]
    assert lines[2] == ""
    summary = lines[1]
    assert len(summary.encode("utf-8")) < 256
    for routing_rule in (
        "MANDATORY for every VelaOps health, incident, or repair request",
        "use `read_file` to read this complete skill",
        "return only its minified JSON",
    ):
        assert routing_rule in summary

    for heading in (
        "## When to use",
        "## Diagnosis workflow and trust boundary",
        "## Repair workflow",
        "## Assessment rules",
        "## Output contract",
        "## Examples",
    ):
        assert heading in text

    # 只读取证工具是唯一服务器入口，Skill 必须显式禁止旁路。
    assert "Call `velaops_check_resources` exactly once" in text
    assert "untrusted server evidence" in text
    for forbidden_tool in ("`run_shell`", "`curl`", "file tools"):
        assert forbidden_tool in text
    assert "physical BOOT-button approval" in text
    assert "Call `velaops_restart_service` exactly once with `{}`" in text
    assert "Never execute a repair for a status-only request" in text
    assert "`execution_state=unknown` means do not retry" in text

    # Markdown 中的唯一 JSON 示例就是下游解析合约，必须始终可解析。
    blocks = re.findall(r"```json\n(.*?)\n```", text, flags=re.DOTALL)
    assert len(blocks) == 1
    contract = json.loads(blocks[0])
    assert set(contract) == {
        "schema_version",
        "status",
        "summary",
        "display",
        "evidence",
        "root_cause_candidates",
        "recommended_action",
        "confidence",
    }
    assert contract["schema_version"] == 1
    assert contract["recommended_action"]["requires_physical_approval"] is True

    for status in ("normal", "warning", "critical", "unknown"):
        assert f"`{status}`" in text
    for action in ("none", "restart_service", "retry_check"):
        assert action in text

    # 把常见凭据形式拦在提交前，避免演示 Skill 泄露秘钥。
    assert not re.search(r"(?:sk-|tvly-|AKIA)[A-Za-z0-9_-]{12,}", text)
    print("PASS: VelaOps server incident response Skill contract")


if __name__ == "__main__":
    main()
