---
description: "Use when implementing roadmap plans, daily TODO lists, or execution tracking in plan/** and docs/plans/**. Enforce execution-first behavior, status/evidence synchronization, and completion discipline."
name: "Plan & TODO Implementation Rules"
applyTo: ["plan/**/*.md", "docs/plans/**/*.md", "**/*todo*.md"]
---

# Plan & TODO Implementation Rules

- If the user asks to implement, start execution immediately on the highest-priority actionable task.
- Do not stop at planning when code or file changes are expected.
- Keep TODO status synchronized with real work: not-started, in-progress, blocked, completed.
- For each completed TODO, record evidence (test/build command, artifact, or file diff).
- If a task is blocked, document blocker + next concrete action in the same update.
- Update plan checkboxes or status lines when implementation state changes.
- Prefer small, verifiable increments over large unverified batches.
- Run relevant validations before closing (tests, build, budget/security gates when available).
- Do not mark work complete while open blockers remain.
- Before task completion, provide a short summary of what changed and why.
- Never claim success without verifiable outputs (passing command results or produced artifacts).

## Output Expectations

- Execution updates should include: changed files, validation run, and remaining work.
- Final update should include: implemented items, evidence, and any residual risk.