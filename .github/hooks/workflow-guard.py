#!/usr/bin/env python3
import json
import sys
from typing import Any

CONFIRM_TOKEN = "CONFIRM_WORKFLOW_CHANGE"
WORKFLOW_PATH = ".github/workflows/"
MUTATING_TOOLS = {
    "apply_patch",
    "create_file",
    "edit_notebook_file",
    "run_in_terminal",
    "mcp_gitkraken_git_add_or_commit",
}


def _find_first_value(obj: Any, keys: set[str]) -> str:
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k in keys and isinstance(v, str):
                return v
            nested = _find_first_value(v, keys)
            if nested:
                return nested
    elif isinstance(obj, list):
        for item in obj:
            nested = _find_first_value(item, keys)
            if nested:
                return nested
    return ""


def _contains_mutation_hint(command: str) -> bool:
    lowered = command.lower()
    mutation_markers = [
        "mv ", "cp ", "rm ", "sed ", "perl ", "tee ", "cat >", "> .github/workflows/",
        "echo ", "python ", "touch ", "git add", "git commit"
    ]
    return any(marker in lowered for marker in mutation_markers)


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except Exception:
        # If payload is unreadable, don't break the workflow.
        print(json.dumps({"continue": True}))
        return 0

    payload_text = json.dumps(payload, ensure_ascii=False)
    tool_name = _find_first_value(payload, {"toolName", "tool_name", "tool", "name"})
    command = _find_first_value(payload, {"command", "input", "args", "parameters"})

    targets_workflow = WORKFLOW_PATH in payload_text
    has_confirmation = (
        CONFIRM_TOKEN in payload_text
        or "confirmation explicite" in payload_text.lower()
        or "workflow confirmé" in payload_text.lower()
    )

    mutating_attempt = False
    if tool_name in MUTATING_TOOLS:
        mutating_attempt = True
    if tool_name == "run_in_terminal" and not _contains_mutation_hint(command):
        mutating_attempt = False

    if targets_workflow and mutating_attempt and not has_confirmation:
        out = {
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "ask",
                "permissionDecisionReason": (
                    "Modification de .github/workflows détectée. Confirmation explicite requise. "
                    "Ajoutez le token CONFIRM_WORKFLOW_CHANGE dans la demande pour autoriser."
                ),
            }
        }
        print(json.dumps(out, ensure_ascii=False))
        return 0

    out = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "allow",
            "permissionDecisionReason": "Aucune modification workflow non confirmée détectée.",
        }
    }
    print(json.dumps(out, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
