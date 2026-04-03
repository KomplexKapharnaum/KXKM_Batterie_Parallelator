#!/usr/bin/env bash
# collect-evidence.sh — Assemble les preuves de qualité BMU pour une PR ou release.
# Usage: .github/skills/release-evidence/scripts/collect-evidence.sh [version-label]
# Exemple: .github/skills/release-evidence/scripts/collect-evidence.sh v1.2.0
set -euo pipefail

VERSION_LABEL="${1:-snapshot}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
COMMIT_SHA="$(git rev-parse --short HEAD 2>/dev/null || echo 'unknown')"
REPORT="release-evidence-${TIMESTAMP}.md"

# Résoudre pio (compatibilité macOS + CI)
if command -v pio &>/dev/null; then
  PIO="pio"
elif [ -x "/Users/electron/.local/bin/pio" ]; then
  PIO="/Users/electron/.local/bin/pio"
elif python3 -m platformio --version &>/dev/null 2>&1; then
  PIO="python3 -m platformio"
else
  echo "ERROR: pio introuvable. Installe PlatformIO CLI avant de continuer." >&2
  exit 1
fi

RUN_OK=0
RUN_FAIL=1

# ── Helpers ─────────────────────────────────────────────────────────────────

run_gate() {
  local name="$1"
  shift
  local cmd="$*"
  local tmpout
  tmpout="$(mktemp)"

  echo "→ [${name}] ${cmd}"
  if eval "${cmd}" >"${tmpout}" 2>&1; then
    echo "  ✅ PASS"
    echo "${tmpout}"
    return ${RUN_OK}
  else
    echo "  ❌ FAIL"
    echo "${tmpout}"
    return ${RUN_FAIL}
  fi
}

tail_lines() {
  tail -n 20 "$1" 2>/dev/null || true
}

extract_memory() {
  # Extrait RAM% et Flash% du rapport check_memory_budget
  grep -E 'RAM:|Flash:' "$1" | head -4 || echo "(non disponible)"
}

# ── Exécution des gates ──────────────────────────────────────────────────────

GATE_SIM_RESULT="FAIL"
GATE_BUILD_RESULT="FAIL"
GATE_MEM_RESULT="FAIL"

TMP_SIM="$(mktemp)"
TMP_BUILD="$(mktemp)"
TMP_MEM="$(mktemp)"

echo "=== BMU Release Evidence: ${VERSION_LABEL} (${COMMIT_SHA}) ==="
echo ""

# Gate 1: sim-host tests
if ${PIO} test -e sim-host >"${TMP_SIM}" 2>&1; then
  GATE_SIM_RESULT="PASS"
  echo "✅ sim-host tests: PASS"
else
  echo "❌ sim-host tests: FAIL"
fi

# Gate 2: build S3
if ${PIO} run -e kxkm-s3-16MB >"${TMP_BUILD}" 2>&1; then
  GATE_BUILD_RESULT="PASS"
  echo "✅ S3 build: PASS"
else
  echo "❌ S3 build: FAIL"
fi

# Gate 3: memory budget
if bash scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85 >"${TMP_MEM}" 2>&1; then
  GATE_MEM_RESULT="PASS"
  echo "✅ Memory budget: PASS"
else
  echo "❌ Memory budget: FAIL"
fi

# ── Verdict ──────────────────────────────────────────────────────────────────

if [ "${GATE_SIM_RESULT}" = "PASS" ] && [ "${GATE_BUILD_RESULT}" = "PASS" ] && [ "${GATE_MEM_RESULT}" = "PASS" ]; then
  VERDICT="✅ PASS — Prêt pour merge"
else
  VERDICT="❌ FAIL — Gates rouges, merge bloqué"
fi

# ── Génération du rapport Markdown ───────────────────────────────────────────

cat >"${REPORT}" <<MDEOF
## Release Evidence — ${VERSION_LABEL} (commit \`${COMMIT_SHA}\`)
Date: $(date +%Y-%m-%d\ %H:%M:%S)

| Gate | Résultat | Commande |
|------|----------|----------|
| sim-host tests | ${GATE_SIM_RESULT} | \`pio test -e sim-host\` |
| S3 build | ${GATE_BUILD_RESULT} | \`pio run -e kxkm-s3-16MB\` |
| Memory budget | ${GATE_MEM_RESULT} | \`scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85\` |

**Verdict: ${VERDICT}**

---

### sim-host tests (dernières lignes)
\`\`\`
$(tail_lines "${TMP_SIM}")
\`\`\`

### S3 build (dernières lignes)
\`\`\`
$(tail_lines "${TMP_BUILD}")
\`\`\`

### Memory budget
\`\`\`
$(extract_memory "${TMP_MEM}")
\`\`\`
MDEOF

echo ""
echo "Rapport généré: ${REPORT}"
echo "Verdict: ${VERDICT}"

# Nettoyage des fichiers temporaires
rm -f "${TMP_SIM}" "${TMP_BUILD}" "${TMP_MEM}"

# Code de sortie: 0 si tout PASS, 1 sinon
if [ "${VERDICT#*PASS}" != "${VERDICT}" ] && [ "${GATE_SIM_RESULT}" = "PASS" ] && [ "${GATE_BUILD_RESULT}" = "PASS" ] && [ "${GATE_MEM_RESULT}" = "PASS" ]; then
  exit 0
else
  exit 1
fi
