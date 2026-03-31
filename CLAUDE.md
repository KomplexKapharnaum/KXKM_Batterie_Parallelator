# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Projet
- Client : KompleX KapharnaüM (Villeurbanne) — contact : nicolas.guichard@lehoop.fr
- BMU (Battery Management Unit) — gestion sécurisée de batteries en parallèle 24–30 V
- Deux stacks firmware : Arduino/PlatformIO (legacy) + ESP-IDF v5.4 (BOX-3, actif)
- Firmware Rust no_std hybride (state machine protection + FFI bridge C)
- Intégré au workflow Kill_LIFE (spec-first, gates S0–S3)

## Build & Test Commands

### ESP-IDF (firmware actif — ESP32-S3-BOX-3)
```bash
cd firmware-idf
export IDF_PATH=$HOME/esp/esp-idf && . $IDF_PATH/export.sh

idf.py build                                       # Build standard
idf.py -v build                                    # Build verbose
idf.py -p /dev/cu.usbmodem* flash                  # Flash (BOOT+EN si "No serial data received")
idf.py -p /dev/cu.usbmodem* monitor                # Monitor (Ctrl+] pour quitter)
idf.py -p /dev/cu.usbmodem* flash monitor          # Flash + monitor enchainé
idf.py menuconfig                                  # Config WiFi, MQTT, seuils, fonts LVGL
rm -rf build && idf.py set-target esp32s3 && idf.py build  # Clean rebuild (lent — re-télécharge managed components)
```

### PlatformIO (legacy Arduino)
```bash
pio run -e kxkm-s3-16MB                            # Build ESP32-S3
pio test -e sim-host                                # Tests unitaires host (sans hardware)
pio test -e sim-host -vv                            # Tests verbose
scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85
```

### Rust (hybrid no_std)
```bash
cd firmware-rs/crates/bmu-protection && cargo test  # 10 tests protection logic (host)
```

**PlatformIO gotcha:** `[arduino_base]` dans platformio.ini contient `framework=arduino`. Ne PAS mettre `framework=arduino` dans `[env]` global — ça casse les envs `native`/`sim-host`.

## Architecture

### Trois firmware stacks

**`firmware-idf/`** (ESP-IDF v5.4, actif) — ESP32-S3-BOX-3 (16MB flash, 8MB PSRAM, écran tactile 2.4")
- 14 composants dans `components/` : bmu_i2c, bmu_ina237, bmu_tca9535, bmu_config, bmu_protection, bmu_display, bmu_wifi, bmu_web, bmu_storage, bmu_mqtt, bmu_influx, bmu_sntp, bmu_ota, bmu_vedirect
- LVGL v9 display avec tabview 4 onglets (Batt/Sys/Alert/I2C)
- Binary ~1.52 MB, partition OTA 2 MB (27% libre)

**`firmware/`** (Arduino/PlatformIO, legacy) — ESP32-S3 générique
- Sources dans `firmware/src/`, tests Unity dans `firmware/test/`

**`firmware-rs/`** (Rust no_std) — state machine protection pure + FFI bridge
- 5 crates : bmu-i2c, bmu-ina237, bmu-tca9535, bmu-protection (10 tests), bmu-ffi (staticlib)

### I2C Architecture (double bus BOX-3)
- **I2C_NUM_0** (GPIO8/18, 400kHz) : BOX-3 interne (écran, codec, IMU) — géré par BSP, ne pas toucher
- **I2C_NUM_1** (GPIO40/41, 50kHz) : BMU dédié (INA237 0x40-0x4F, TCA9535 0x20-0x27) — géré par `bmu_i2c`
- **Ne pas créer I2C_NUM_1 deux fois** : le BSP crée le bus DOCK, `bmu_i2c` récupère le handle via `i2c_master_get_bus_handle()`
- Pull-ups 4.7kΩ **externes requises** sur GPIO40/41 (non montées sur dock)

### Topology Constraint
`Nb_TCA * 4 == Nb_INA`. Mismatch → fail-safe (toutes batteries OFF). Typique : 4 TCA + 16 INA.

### Protection State Machine (F01-F08)
5 états : CONNECTED / DISCONNECTED / RECONNECTING / ERROR / LOCKED
- Seuils Kconfig : 24000mV–30000mV, 10000mA, 1000mV diff, 5 coupures max, 10s delay
- LOCKED = permanent (reboot requis)
- Web mutations passent par `bmu_protection_web_switch()` — jamais direct TCA

## Hardware — ESP32-S3-BOX-3

### GPIOs occupés (ne pas utiliser)
- Display SPI : 4, 5, 6, 7, 47, 48
- Audio : 2, 15, 16, 17, 45, 46
- Touch/Boutons : 0, 1, 3
- USB : 19, 20

### Flash BOX-3
Si `No serial data received` : maintenir BOOT, appuyer EN, relâcher BOOT, puis re-flasher.

## Safety-Critical Rules
- **Never** weaken voltage/current thresholds, reconnect delays, or topology guards
- **Never** bypass INA/TCA topology validation
- **All** I2C multi-register reads must use `bmu_i2c_lock()`/`bmu_i2c_unlock()` (ESP-IDF) ou `I2CLockGuard` (Arduino)
- Web mutation routes require auth token — passer par `bmu_protection_web_switch()`, jamais direct TCA
- Preserve NAN/early-return fault semantics for sensor failures
- Keep FreeRTOS tasks non-blocking — no long blocking calls in periodic tasks

## Conventions
- French comments and identifiers — maintain consistency
- ESP-IDF : `ESP_LOGx(TAG, ...)` avec TAG par fichier (BATT, I2C, INA, TCA, WEB, SD, MQTT, INFLUX, SNTP)
- Arduino legacy : `KxLogger` categories
- CSV logging : `;` separator — ne pas changer
- Config thresholds : tout en mV/mA (pas de mélange V/mV)
- Seule `lv_font_montserrat_14` activée dans LVGL — les autres (10/12/16/20) nécessitent `idf.py menuconfig` → Component config → LVGL → Font

## CI/CD
- `ci.yml` : PlatformIO `pio test -e native` on push/PR
- `esp-idf-ci.yml` : ESP-IDF build ESP32-S3 + host tests Unity
- `rust-tests.yml` : `cargo test` bmu-protection on host
- ESP32 hardware build/flash is local-only

## Gates Kill_LIFE
- S0 ✅ intake + specs
- S1 ✅ tests natifs 10/10 + bug fix `find_max_voltage`
- S2 ✅ KiCad BMU v2 review (ERC 0 violations)
- S3 ⬜ validation terrain (batteries réelles)

## Known Technical Debt
- MED-010: mV/V unit inconsistency in Arduino firmware — standardize to mV throughout
- Legacy headers (INA_Func.h, BatterySwitchCtrl.h) contain duplicated logic — refactoring candidates
- Arduino web UI assets embedded as string literals — migré vers SPIFFS dans ESP-IDF
- LVGL fonts : seule montserrat_14 activée, les écrans UI utilisent tous cette taille
