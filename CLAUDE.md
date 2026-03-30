# KXKM Batterie Parallelator — Conventions

## Projet
- Client : KompleX KapharnaüM (Villeurbanne) — contact : nicolas.guichard@lehoop.fr
- BMU (Battery Management Unit) ESP32 — gestion sécurisée de batteries en parallèle 24–30 V
- Intégré au workflow Kill_LIFE (spec-first, gates S0–S3)

## Structure
- `specs/` — intake (S0), spec (S1), arch (S2), plan (S3) — Kill_LIFE gates
- `src/` — firmware Arduino/PlatformIO (main.cpp + headers)
- `test/test_protection/` — tests Unity natifs (logique protection V/I/déséquilibre)
- `PCB/BMU v1/` — schéma KiCad v1, gerbers JLCPCB (fabriqué)
- `pcb-bmu-v2/` — schéma KiCad v2 (KiCad 8.0), ERC 0 violations, BOM 107 composants
- `lib/INA237/` — driver INA237 local
- `data/` — interface web embarquée (AsyncWebServer)

## Firmware
- Framework : Arduino / PlatformIO
- Board cible : `kxkm-s3-16MB` (ESP32-S3, 16MB flash)
- Board legacy : `kxkm-v3-16MB` (esp-wrover-kit)
- Build ESP32 : `pio run -e kxkm-s3-16MB`
- Env `[arduino_base]` dans platformio.ini — ne pas mettre `framework=arduino` dans `[env]` global (casse le native)

## Hardware (KiCad)
- Schématics KiCad 8.0 (version 20231120)
- kicad-cli : Docker direct (pas le wrapper Kill_LIFE — repo hors Kill_LIFE root) :
  docker run --rm --user $(id -u):$(id -g) -e HOME=/tmp \
    -v "/home/clems/KXKM_Batterie_Parallelator:/project" \
    -w /project kicad/kicad:10.0 kicad-cli sch erc \
    --format json --output "/project/pcb-bmu-v2/erc_report.json" \
    "/project/pcb-bmu-v2/BMU v2.kicad_sch"
```raw

## ICs clés
- INA237 (VSSOP-10) — mesure V/I/P via I²C @ 0x40–0x4F
- TCA9535PWR (TSSOP-24) — GPIO expander relais via I²C @ 0x20–0x27
- ISO1540 (SOIC-8) — isolateur I²C galvanique
- Adressage via solder jumpers JP2–JP20

## Gates Kill_LIFE
- S0 ✅ intake + specs
- S1 ✅ tests natifs 10/10 + bug fix `find_max_voltage`
- S2 ✅ KiCad BMU v2 review (ERC 0 violations)
- S3 ⬜ validation terrain (batteries réelles)

```