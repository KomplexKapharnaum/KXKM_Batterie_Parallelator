#!/usr/bin/env bash
set -euo pipefail

workflow="${1:-qa-cicd-environments}"
branch="${2:-$(git rev-parse --abbrev-ref HEAD)}"
limit="${3:-10}"
repo="${GITHUB_REPOSITORY:-KomplexKapharnaum/KXKM_Batterie_Parallelator}"

json=$(gh run list \
  --repo "$repo" \
  --workflow "$workflow" \
  --branch "$branch" \
  --limit "$limit" \
  --json databaseId,url,status,conclusion,displayTitle,createdAt,name,event 2>/dev/null || true)

if [[ -z "$json" || "$json" == "[]" ]]; then
  echo "[evidence] Aucun run trouve pour workflow='$workflow' branch='$branch' repo='$repo'."
  echo "[evidence] Action: declencher un push/PR (ou workflow_dispatch si active), puis relancer ce script."
  exit 4
fi

echo "## Remote CI Evidence"
echo ""
echo "Workflow: $workflow"
echo "Branch: $branch"
echo "Repo: $repo"
echo ""
echo "| Run ID | Created (UTC) | Event | Status | Conclusion | URL |"
echo "|---|---|---|---|---|---|"

echo "$json" | jq -r '.[] | "| \(.databaseId) | \(.createdAt) | \(.event) | \(.status) | \(.conclusion // "-") | \(.url) |"'
