# Architecture — KXKM Batterie Parallelator

> Kill_LIFE gate: S0 → S1
> Version: 1.0 (2026-03-25)

## Vue d'ensemble

```
┌─────────────────────────────────────────────────────────┐
│  ESP32 (K32 board)                                      │
│                                                         │
│  setup()                                                │
│   ├── Wire.begin(SDA=32, SCL=33) @ 50 kHz              │
│   ├── I²C scan (0x08–0x78)                              │
│   ├── setup_tca()  ← TCA9535 GPIO expander init        │
│   └── setup_ina()  ← INA237 courant/tension init       │
│                                                         │
│  loop() [500 ms]                                        │
│   └── for each INA (0..Nb_INA-1)                        │
│        ├── read_volt / read_current / read_power        │
│        ├── check_protection()  [compute.h]              │
│        │    ├── under-voltage → switch_off              │
│        │    ├── over-voltage  → switch_off              │
│        │    ├── over-current  → switch_off              │
│        │    ├── voltage imbalance → switch_off          │
│        │    ├── Nb_switch < max → switch_on             │
│        │    ├── Nb_switch == max → timer reconnect      │
│        │    └── Nb_switch > max → permanent lock        │
│        └── serial log                                   │
└─────────────────────────────────────────────────────────┘

I²C bus (50 kHz)
 ├── INA237 @ 0x40–0x4F (jusqu'à 16, adresse via A0/A1)
 └── TCA9535 @ 0x20–0x27 (adresse via A0/A1/A2)
      └── outputs: relais MOSFET + LED état
```

## Modules source

| Fichier | Rôle |
|---------|------|
| `firmware/src/main.cpp` | Setup/loop principal, orchestration |
| `firmware/src/INA_Func.h` | Init INA237, lecture V/I/P |
| `firmware/src/TCA_Func.h` | Init TCA9535, lecture/écriture GPIO |
| `firmware/src/compute.h` | Logique protection (switch_on/off, compteurs) |
| `firmware/src/pin_mappings.h` | Mapping GPIO → INA ALERT pins |
| `firmware/src/data_log.h` | Logging série |
| `firmware/lib/INA237/` | Driver INA237 local (fork) |
| `firmware/data/` | Interface web (HTML/JS/CSS) |

## PCB

| Version | Fichier | État |
|---------|---------|------|
| BMU v1 | `hardware/PCB/BMU v1/BMU v1.kicad_sch` | Fabriqué JLCPCB |
| BMU v2 | `hardware/pcb-bmu-v2/BMU_switch_mosfet.kicad_sch` | En cours |

## Adressage I²C

- INA237 : A0/A1 configurables → 4 adresses max par bus (extensible via TCA multiplexeur)
- TCA9535 : A0/A1/A2 → 8 adresses max
- Tableau de correspondance : `adresse TCA_INA.xlsx`

## Contraintes d'implémentation

- I²C à 50 kHz (lent) pour fiabilité sur câblage terrain long
- `delay(500)` en fin de loop — acceptable pour application de protection batterie (pas temps-réel strict)
- Pas de RTOS / pas de tâches concurrentes
