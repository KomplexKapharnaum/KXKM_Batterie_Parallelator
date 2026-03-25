# Plan — KXKM Batterie Parallelator

> Kill_LIFE gate: S0 → S1
> Version: 1.0 (2026-03-25)

## Gates

| Gate | Condition | État |
|------|-----------|------|
| S0 | Intake validé, specs rédigées | ✅ 2026-03-25 |
| S1 | Build PlatformIO OK + tests natifs OK | ✅ 2026-03-25 |
| S2 | Hardware review BMU v2 (KiCad schops) | ✅ 2026-03-25 |
| S3 | Validation terrain (batteries réelles) | ⬜ |

## S1 — Firmware consolidé

- [x] Ajouter env `native` dans `platformio.ini` ✅ 2026-03-25
- [x] Créer `test/test_protection/` — 10 tests Unity ✅ 2026-03-25
  - test_undervoltage_disconnects, test_nominal_voltage_connects
  - test_overvoltage_disconnects
  - test_overcurrent_positive/negative_disconnects, test_nominal_current_connects
  - test_voltage_imbalance_disconnects/within_threshold_connects
  - test_permanent_lock_above_max, test_no_permanent_lock_at_max
- [x] CI GitHub Actions : `pio test -e native` + build `kxkm-v3-16MB` ✅ 2026-03-25
- [x] Corriger bug `battery_voltage_max=(battery_voltage, Nb_INA)` → `find_max_voltage()` ✅ 2026-03-25
- [ ] Unifier les seuils en `#define` dans un header de config dédié

## S2 — Hardware review BMU v2

- [x] `schops snapshot` → `PCB BMU v2/snapshot_before.json` (55 symboles) ✅ 2026-03-25
- [x] `schops erc` → `PCB BMU v2/erc_report.json` — **0 violations** ✅ 2026-03-25
- [x] `schops bom` → `PCB BMU v2/bom.csv` — 107 composants ✅ 2026-03-25
  - U1–U4: INA237 (VSSOP-10) ×4 | U6: TCA9535PWR (TSSOP-24) | U5: ISO1540 (isolateur I²C)
  - U4: TCA9517 (level shifter I²C) | U8: VPS8505 | U9: MCP16331 (buck)
  - JP2–JP20: solder jumpers adressage I²C (×19) — cohérent avec firmware (A0/A1/A2)
- [ ] Vérifier cohérence adressage JP vs defines firmware (à documenter)

## S3 — Validation terrain

- [x] Simulation logique S3 (`test/test_s3_sim/sim_s3.cpp`) — 3 scénarios ✅ 2026-03-25
  - S3-A : déséquilibre 25V/27V → coupure BAT2 (sw inchangé) → reconnexion dès remontée
  - S3-B : sous-tension 22V → coupure → reconnexion immédiate (nb_switch < 5)
  - S3-C : 5 coupures → temporisation 10s → 6e coupure → verrouillage permanent
- [ ] Test avec 2 batteries réelles (vérification coupure sur déséquilibre)
- [ ] Test reconnexion automatique
- [ ] Test verrouillage permanent
