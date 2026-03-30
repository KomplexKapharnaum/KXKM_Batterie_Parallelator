#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
output_file="$repo_root/docs/PROJECT_CONTEXT_SNAPSHOT.md"

cd "$repo_root"

branch="$(git branch --show-current)"
head_sha="$(git rev-parse --short HEAD)"
origin_sha="$(git rev-parse --short origin/main 2>/dev/null || echo "n/a")"
status_short="$(git status --short --branch)"
date_utc="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

cat > "$output_file" <<EOF
# Project Context Snapshot

- generated_at_utc: $date_utc
- branch: $branch
- head: $head_sha
- origin_main: $origin_sha

## Git Status
\`\`\`
$status_short
\`\`\`

## Plans
- plan/process-cicd-environments-1.md
- plan/refactor-safety-core-web-remote-1.md

## Governance Docs
- docs/GOVERNANCE_INTEGRATION.md
- docs/ARCHITECTURE_DIAGRAMS.md
- docs/FEATURE_MAP.md
- docs/AGENT_TASK_ASSIGNMENT.md
EOF

echo "Snapshot written to $output_file"
