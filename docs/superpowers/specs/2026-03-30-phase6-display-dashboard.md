# Phase 6: Display Dashboard — ESP32-S3-BOX-3 Screen

**Projet:** KXKM Batterie Parallelator (BMU)
**Date:** 2026-03-30
**Status:** Planifie

---

## 1. Objectif

Utiliser l'ecran tactile 2.4" ILI9342C (320x240) du ESP32-S3-BOX-3 pour afficher un dashboard batterie en temps reel avec controle tactile.

## 2. Hardware disponible (BOX-3 interne)

| Peripherique | GPIO | Bus | Notes |
|---|---|---|---|
| Display ILI9342C | DC=4, CS=5, SDA=6, SCK=7, RST=48, BL=47 | SPI2 | 320x240, 16-bit color |
| Touch TT21100 | INT=3 | I2C_NUM_0 (GPIO8/18) | Capacitif, meme bus que codec |
| Backlight | GPIO47 | PWM/LEDC | Dimming possible |

**Pas de conflit avec BMU** : le display SPI et le touch I2C_NUM_0 sont sur des bus differents de l'I2C_NUM_1 BMU (GPIO40/41).

## 3. Stack UI/UX

**LVGL v9** (Light and Versatile Graphics Library) — standard de l'industrie pour embedded UI.

- **BSP esp-box-3** : Espressif fournit un Board Support Package avec LVGL pre-integre
- **esp_lcd** : driver display ESP-IDF natif (SPI + ILI9342C)
- **esp_lcd_touch** : driver touch TT21100
- **LVGL v9** : widgets, animations, themes, responsive layout

## 4. Ecrans du Dashboard

### 4.1 Ecran principal — Vue grille batteries

```
┌──────────────────────────────────────┐
│  KXKM BMU v0.4.0        12:34:56    │
├────────┬────────┬────────┬──────────┤
│ BAT 1  │ BAT 2  │ BAT 3  │ BAT 4   │
│ 27.2V  │ 26.8V  │ 27.0V  │ ---.--  │
│  0.3A  │  0.5A  │  0.2A  │  0.0A   │
│  🟢    │  🟢    │  🟢    │  🔴     │
├────────┼────────┼────────┼──────────┤
│ BAT 5  │ BAT 6  │ BAT 7  │ BAT 8   │
│ 26.5V  │ 27.1V  │ 26.9V  │ 27.3V   │
│  0.4A  │  0.1A  │  0.3A  │  0.2A   │
│  🟢    │  🟢    │  🟢    │  🟢     │
├────────┴────────┴────────┴──────────┤
│ Total: 54.2A  Avg: 27.0V  WiFi: ✓  │
└──────────────────────────────────────┘
```

- Grille 4x4 (max 16 batteries)
- Code couleur : vert=connected, rouge=fault, gris=locked, jaune=reconnecting
- Tap sur une batterie → ecran detail

### 4.2 Ecran detail batterie (tap)

```
┌──────────────────────────────────────┐
│  ← Retour          BAT 3            │
├──────────────────────────────────────┤
│                                      │
│  Tension:    27.012 V                │
│  Courant:     0.234 A                │
│  Puissance:   6.320 W                │
│  Temperature: 32.5 °C                │
│                                      │
│  Ah decharge: 12.450 Ah              │
│  Ah charge:    3.200 Ah              │
│  Nb coupures:  2 / 5                 │
│  Etat: CONNECTED                     │
│                                      │
│  [  SWITCH OFF  ]  [  RESET  ]       │
└──────────────────────────────────────┘
```

- Graphique temps reel V/I (optionnel — LVGL chart widget)
- Boutons switch ON/OFF et reset switch count
- Confirmation dialog pour les actions critiques

### 4.3 Ecran systeme (swipe gauche)

```
┌──────────────────────────────────────┐
│  Système                             │
├──────────────────────────────────────┤
│  Firmware: 0.4.0 (2026-03-30)       │
│  Free heap: 245 KB                   │
│  Uptime: 2h 34m                      │
│  WiFi: 192.168.1.42 (KXKM-BMU)      │
│  MQTT: Connected                     │
│  InfluxDB: OK (last 10s ago)         │
│  SD Card: Mounted (2.3 GB free)      │
│  NTP: Synced                         │
│                                      │
│  INA237: 16/16  TCA9535: 4/4         │
│  Topologie: ✓ VALIDE                 │
│                                      │
│  [  OTA UPDATE  ]  [  REBOOT  ]      │
└──────────────────────────────────────┘
```

### 4.4 Ecran alertes (swipe droit)

- Historique des 20 dernieres alertes (horodatees)
- Filtrable par type : OV, UV, OC, IMBALANCE, LOCK

## 5. Architecture composant

```
firmware-idf/components/
└── bmu_display/
    ├── CMakeLists.txt
    ├── Kconfig                     # Backlight PWM, refresh rate, theme
    ├── include/bmu_display.h       # Init + update API
    ├── bmu_display.cpp             # LVGL setup + tick + flush
    ├── bmu_ui_main.cpp             # Ecran grille batteries
    ├── bmu_ui_detail.cpp           # Ecran detail batterie
    ├── bmu_ui_system.cpp           # Ecran systeme
    └── bmu_ui_alerts.cpp           # Ecran alertes
```

Dependances ESP-IDF :
- `esp_lcd` (ILI9342C SPI driver)
- `esp_lcd_touch_tt21100` (capacitive touch)
- `lvgl` (via ESP Component Registry)
- `esp_lvgl_port` (LVGL ↔ ESP-IDF integration)

## 6. Integration avec la protection

Le display lit l'etat via les API existantes (pas de couplage direct) :
- `bmu_protection_get_state(ctx, idx)` → couleur de la cellule
- `bmu_protection_get_voltage(ctx, idx)` → valeur affichee
- `bmu_battery_manager_get_ah_discharge(mgr, idx)` → Ah
- `bmu_ota_get_info()` → version firmware

Le display tourne dans sa propre FreeRTOS task (priorite basse) avec un timer LVGL de 5ms.

## 7. Design UX

- **Theme sombre** (meilleur contraste en exterieur/nuit, economie backlight)
- **Font: Montserrat 14/20** (incluse dans LVGL)
- **Couleurs :**
  - Vert `#00C853` : nominal
  - Rouge `#FF1744` : fault/error
  - Orange `#FF9100` : reconnecting/warning
  - Gris `#9E9E9E` : locked/disabled
  - Bleu `#2979FF` : info/system
- **Animations :** fade transitions entre ecrans (200ms), pulse sur alertes actives
- **Backlight :** dimming automatique apres 30s sans touch (economie batterie)

## 8. Estimation

| Tache | Complexite | Fichiers |
|-------|-----------|----------|
| BSP init (display + touch) | Moyenne | bmu_display.cpp |
| Ecran grille (LVGL grid) | Moyenne | bmu_ui_main.cpp |
| Ecran detail (LVGL widgets) | Faible | bmu_ui_detail.cpp |
| Ecran systeme | Faible | bmu_ui_system.cpp |
| Ecran alertes | Faible | bmu_ui_alerts.cpp |
| Navigation swipe/tap | Moyenne | bmu_display.cpp |
| Theme + animations | Faible | bmu_display.cpp |

Sources:
- [ESP32_Display_Panel (Espressif)](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL Supported Boards](https://lvgl.io/boards)
- [ESP-BOX BSP (Espressif)](https://github.com/espressif/esp-box)
