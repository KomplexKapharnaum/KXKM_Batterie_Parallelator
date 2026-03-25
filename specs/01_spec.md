# Spec — KXKM Batterie Parallelator

> Kill_LIFE gate: S0 → S1
> Version: 1.0 (2026-03-25)

## Fonctions requises

| ID  | Fonction | Priorité |
|-----|----------|----------|
| F01 | Mesure tension chaque batterie via INA237 (I²C) | MUST |
| F02 | Mesure courant chaque batterie via INA237 (I²C) | MUST |
| F03 | Coupure relais via TCA9535 sur sur-tension (>30 V) | MUST |
| F04 | Coupure relais sur sous-tension (<24 V) | MUST |
| F05 | Coupure relais sur sur-courant (>1 A abs) | MUST |
| F06 | Coupure relais sur déséquilibre tension (>1 V vs max) | MUST |
| F07 | Reconnexion automatique après N coupures (délai 10 s) | MUST |
| F08 | Coupure permanente après dépassement Nb_switch_max (5) | MUST |
| F09 | Log série 115200 baud — état de chaque batterie | SHOULD |
| F10 | Interface web embarquée (AsyncWebServer) — supervision | COULD |
| F11 | Support jusqu'à 16 packs batterie | MUST |

## Paramètres configurables

```cpp
#define alert_bat_min_voltage  24000  // mV
#define alert_bat_max_voltage  30000  // mV
#define alert_bat_max_current  1      // A
#define voltage_diff           1      // V — seuil déséquilibre
#define Nb_switch_max          5      // coupures max avant verrouillage
#define reconnect_delay        10000  // ms
```

## Interfaces matérielles

| Interface | Composant | Rôle |
|-----------|-----------|------|
| I²C SDA/SCL (GPIO 32/33) | INA237 ×N | Mesure V/I/P |
| I²C | TCA9535PWR ×N | Pilotage relais MOSFET |
| UART0 115200 | — | Debug / log |
| WiFi | — | Accès interface web |

## Non-objectifs

- BMS chimique (gestion SOC, SOH, thermique) : hors périmètre
- Équilibrage actif des cellules : hors périmètre
- Connexion ArtNet / OSC (présent dans K32-lib mais non utilisé ici)
