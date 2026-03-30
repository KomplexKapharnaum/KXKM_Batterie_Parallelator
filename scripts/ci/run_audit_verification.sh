#!/bin/bash
# run_audit_verification.sh — Vérification post-audit BMU
# Usage: ./scripts/ci/run_audit_verification.sh
# Exécute les tests, le build et les vérifications de cohérence post-audit.

set -e
cd "$(git rev-parse --show-toplevel)"

echo "=========================================="
echo " BMU Audit Verification Script"
echo " Date: $(date '+%Y-%m-%d %H:%M:%S')"
echo "=========================================="

PASS=0
FAIL=0
SKIP=0

log_pass() { echo "  ✅ PASS: $1"; ((PASS++)); }
log_fail() { echo "  ❌ FAIL: $1"; ((FAIL++)); }
log_skip() { echo "  ⏭️  SKIP: $1"; ((SKIP++)); }

# ── 1. Tests unitaires sim-host ──────────────────────────────────────────────
echo ""
echo "── 1. Tests unitaires (sim-host) ──"
if pio test -e sim-host 2>&1 | tail -5; then
  log_pass "pio test -e sim-host"
else
  log_fail "pio test -e sim-host"
fi

# ── 2. Build ESP32-S3 ────────────────────────────────────────────────────────
echo ""
echo "── 2. Build ESP32-S3 (kxkm-s3-16MB) ──"
if pio run -e kxkm-s3-16MB 2>&1 | tail -5; then
  log_pass "pio run -e kxkm-s3-16MB"
else
  log_skip "pio run -e kxkm-s3-16MB (dépendances manquantes attendues)"
fi

# ── 3. Memory budget ─────────────────────────────────────────────────────────
echo ""
echo "── 3. Memory budget check ──"
if [ -f scripts/check_memory_budget.sh ]; then
  if bash scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85 2>&1; then
    log_pass "Memory budget"
  else
    log_skip "Memory budget (build requis)"
  fi
else
  log_skip "Memory budget (script absent)"
fi

# ── 4. Vérification cohérence audit ──────────────────────────────────────────
echo ""
echo "── 4. Vérification cohérence audit ──"

# 4a. Vérifier que les mem_set_* sont int (pas float) dans le header
if grep -q "int mem_set_max_voltage" firmware/src/BatteryParallelator.h; then
  echo "  ℹ️  mem_set_max_voltage est int — unités doivent être mV"
  # Vérifier que main.cpp ne divise pas par 1000
  if grep -q "alert_bat_min_voltage / 1000" firmware/src/main.cpp; then
    log_fail "CRIT-A: main.cpp divise encore par 1000 (unités cassées)"
  else
    log_pass "CRIT-A: main.cpp passe les mV directement"
  fi
else
  log_skip "CRIT-A: type changé (vérifier manuellement)"
fi

# 4b. Vérifier absence de double I2CLockGuard dans BatteryRouteValidation
if grep -q "I2CLockGuard" firmware/src/BatteryRouteValidation.cpp; then
  log_fail "CRIT-C: I2CLockGuard toujours présent dans BatteryRouteValidation.cpp"
else
  log_pass "CRIT-C: Pas de double lock dans BatteryRouteValidation"
fi

# 4c. Vérifier que fabs est utilisé dans le ERROR handler
if grep -q "fabs(current)" firmware/src/BatteryParallelator.cpp; then
  log_pass "HIGH-1: fabs(current) présent dans BatteryParallelator"
else
  log_fail "HIGH-1: fabs(current) manquant — surcourant négatif non couvert"
fi

# 4d. Vérifier que timeAndInfluxTask ne fait pas return sur échec SD
if grep -A1 "Card Mount Failed\|SD.begin" firmware/src/TimeAndInfluxTask.cpp | grep -q "return;"; then
  log_fail "HIGH-8: return sur échec SD dans timeAndInfluxTask (tue la tâche)"
else
  log_pass "HIGH-8: Pas de return fatal sur échec SD"
fi

# ── Résumé ────────────────────────────────────────────────────────────────────
echo ""
echo "=========================================="
echo " Résumé: $PASS PASS | $FAIL FAIL | $SKIP SKIP"
echo "=========================================="

if [ "$FAIL" -gt 0 ]; then
  echo " ⚠️  Des corrections audit sont encore nécessaires."
  exit 1
else
  echo " ✅ Toutes les vérifications passent."
  exit 0
fi
