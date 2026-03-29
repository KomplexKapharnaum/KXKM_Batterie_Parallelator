#!/usr/bin/env bash
set -euo pipefail

repo="${1:-KomplexKapharnaum/KXKM_Batterie_Parallelator}"
branch="${2:-$(git rev-parse --abbrev-ref HEAD)}"
workflow="${3:-qa-cicd-environments}"
out_file="${4:-docs/QA_REMOTE_PROOF_LATEST.md}"

mkdir -p "$(dirname "$out_file")"

now_utc="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
workflows_raw="$(gh workflow list --repo "$repo" 2>/dev/null || true)"
runs_raw="$(gh run list --repo "$repo" --workflow "$workflow" --branch "$branch" --limit 5 2>/dev/null || true)"

dispatch_status="not-attempted"
dispatch_detail=""

if echo "$workflows_raw" | grep -q "$workflow"; then
  set +e
  dispatch_output="$(gh workflow run "$workflow" --ref "$branch" --repo "$repo" 2>&1)"
  dispatch_code=$?
  set -e
  if [[ $dispatch_code -eq 0 ]]; then
    dispatch_status="success"
    dispatch_detail="workflow_dispatch accepted"
  else
    dispatch_status="failed"
    dispatch_detail="$dispatch_output"
  fi
else
  dispatch_status="blocked"
  dispatch_detail="workflow '$workflow' absent from remote workflow list"
fi

{
  echo "# QA Remote Proof Snapshot"
  echo
  echo "- Timestamp (UTC): $now_utc"
  echo "- Repository: $repo"
  echo "- Branch: $branch"
  echo "- Target workflow: $workflow"
  echo "- Dispatch status: $dispatch_status"
  echo
  echo "## Workflow inventory"
  if [[ -n "$workflows_raw" ]]; then
    echo '```text'
    echo "$workflows_raw"
    echo '```'
  else
    echo "UNKNOWN"
  fi
  echo
  echo "## Recent runs (target workflow + branch)"
  if [[ -n "$runs_raw" ]]; then
    echo '```text'
    echo "$runs_raw"
    echo '```'
  else
    echo "UNKNOWN"
  fi
  echo
  echo "## Dispatch diagnostic"
  if [[ -n "$dispatch_detail" ]]; then
    echo '```text'
    echo "$dispatch_detail"
    echo '```'
  else
    echo "none"
  fi
  echo
  echo "## Next action"
  echo "- Ensure ".github/workflows/sim-host-tests.yml" is available on default branch, then re-run this script."
  echo "- If dispatch is unavailable, open/update a PR to trigger pull_request CI and collect run URLs."
} > "$out_file"

echo "[remote-proof] Wrote: $out_file"
