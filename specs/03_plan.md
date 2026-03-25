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
- [x] Unifier les seuils en `#define` dans un header de config dédié → `src/config.h` ✅ 2026-03-25

## S2 — Hardware review BMU v2

- [x] `schops snapshot` → `PCB BMU v2/snapshot_before.json` (55 symboles) ✅ 2026-03-25
- [x] `schops erc` → `PCB BMU v2/erc_report.json` — **0 violations** ✅ 2026-03-25
- [x] `schops bom` → `PCB BMU v2/bom.csv` — 107 composants ✅ 2026-03-25
  - U1–U4: INA237 (VSSOP-10) ×4 | U6: TCA9535PWR (TSSOP-24) | U5: ISO1540 (isolateur I²C)
  - U4: TCA9517 (level shifter I²C) | U8: VPS8505 | U9: MCP16331 (buck)
  - JP2–JP20: solder jumpers adressage I²C (×19) — cohérent avec firmware (A0/A1/A2)
- [x] Vérifier cohérence adressage JP vs defines firmware ✅ 2026-03-25
  - **TCA9535** (I2C.kicad_sch) : JP14=PCA_A1, JP15=PCA_A2, JP16=PCA_A0 — SolderJumper_3_Open → sélection adresse 0x20–0x27
  - **INA237** (batteries.kicad_sch) : JP17=INA#1_A0, JP18=INA#1_A1, JP19=INA#2_A0, JP20=INA#2_A1
  - Firmware : TCA_0(0x20)↔INA 0x40–0x43 / TCA_1(0x21)↔INA 0x44–0x47 / TCA_2(0x22)↔INA 0x48–0x4B / TCA_3(0x23)↔INA 0x4C–0x4F
  - Cohérence confirmée : INA237 A0/A1 combinaisons (0,0)/(1,0)/(0,1)/(1,1) → 0x40–0x43 par groupe de 4
  - Jumpers restants JP2–JP13 : adressage des batteries 5–16 (hors périmètre BMU v2 monoboard)
  - ⚠️  Vérification physique des solder jumpers recommandée avant assemblage terrain

## S3 — Validation terrain

- [x] Simulation logique S3 (`test/test_s3_sim/sim_s3.cpp`) — 3 scénarios ✅ 2026-03-25
- [x] Simulation étendue (`test/test_s3_sim/sim_validation.cpp`) — 32/32 cas TV01–TV08 ✅ 2026-03-25
- [x] Plan de banc de test (`specs/04_validation.md`) — L1/L2 ✅, L3 hardware ⬜ ✅ 2026-03-25
  - S3-A : déséquilibre 25V/27V → coupure BAT2 (sw inchangé) → reconnexion dès remontée
  - S3-B : sous-tension 22V → coupure → reconnexion immédiate (nb_switch < 5)
  - S3-C : 5 coupures → temporisation 10s → 6e coupure → verrouillage permanent
- [ ] Test avec 2 batteries réelles (vérification coupure sur déséquilibre)
- [ ] Test reconnexion automatique
- [ ] Test verrouillage permanent
