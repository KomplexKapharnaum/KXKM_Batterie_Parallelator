# Hardware (KiCad)

Schémas et PCB du BMU. **2 révisions** :

| Version | Status | Path |
|---------|--------|------|
| BMU v1 | Manufactured | `PCB/` |
| BMU v2 | Production-ready (ERC 0, BOM 107 composants, Gerbers JLCPCB) | `pcb-bmu-v2/` |

## KiCad CLI via Docker

Le repo est en dehors de Kill_LIFE root, donc Docker direct :

```bash
docker run --rm --user $(id -u):$(id -g) -e HOME=/tmp \
  -v "$(pwd):/project" \
  -w /project kicad/kicad:10.0 kicad-cli sch erc \
  --format json --output "/project/hardware/pcb-bmu-v2/erc_report.json" \
  "/project/hardware/pcb-bmu-v2/BMU v2.kicad_sch"
```

## ICs clés

| IC | Package | Rôle | I2C |
|----|---------|------|-----|
| INA237 | VSSOP-10 | Power monitor (V/I/P) | 0x40-0x4F (16 max) |
| TCA9535PWR | TSSOP-24 | GPIO expander (MOSFETs + LEDs) | 0x20-0x27 (8 max) |
| ISO1540 | SOIC-8 | Isolateur I²C galvanique | - |
| IRF4905 | D2PAK | P-channel MOSFET (55V, 74A) | - |

## I2C Mapping (typique)

```
TCA_0 (0x20) → INA 0x40-0x43 → Batteries 1-4
TCA_1 (0x21) → INA 0x44-0x47 → Batteries 5-8
TCA_2 (0x22) → INA 0x48-0x4B → Batteries 9-12
TCA_3 (0x23) → INA 0x4C-0x4F → Batteries 13-16
```

Adressage via solder jumpers JP2-JP20.

## TCA9535 Pinout

Pins 0-3 = MOSFET switches (1 par batterie), pins 8-15 = LEDs paires rouge/verte (1 par batterie).

## Anti-Patterns

- Ne jamais modifier les fichiers KiCad sans le checker ERC après
- Ne pas changer les jumpers d'adressage sans documenter dans `adresse_TCA_INA.xlsx`
- BOM v2 : 107 composants, ne pas ajouter sans vérifier les substituts JLCPCB
