# PlatformIO/Arduino Firmware (Legacy)

Firmware Arduino legacy, conservé pour les **tests sim-host** (host-based, sans hardware). La production tourne sur `firmware-idf/`.

## Build & Test

```bash
pio run -e kxkm-s3-16MB                     # build prod (S3, 16MB flash)
pio run -e kxkm-s3-16MB --target upload     # flash
pio device monitor -e kxkm-s3-16MB --baud 115200

pio test -e sim-host                        # 13 tests host-based
pio test -e sim-host -f test_protection     # une suite
pio test -e sim-host -vv                    # verbose

scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85
```

## PlatformIO Gotchas

- `[arduino_base]` dans `platformio.ini` contient `framework=arduino`. **Ne pas** mettre `framework=arduino` dans le `[env]` global — ça casse `native`/`sim-host`.
- `[env:sim-host]` a un `build_src_filter` qui liste explicitement les `.cpp` compilés pour les tests host. **Ajouter un nouveau module testable nécessite de l'ajouter au filter** sinon le linker échoue.

## Architecture

- `BatteryParallelator` : state machine protection (5 critères, reconnect logic)
- `BatteryManager` : agrégation, énergie (Ah via task FreeRTOS)
- `INAHandler` : 16 INA237 (I2C 0x40-0x4F)
- `TCAHandler` : 8 TCA9535 (I2C 0x20-0x27, pins 0-3 = MOSFET, 8-15 = LEDs)
- `I2CMutex.h` : `I2CLockGuard` RAII obligatoire pour toute I2C
- `WebServerHandler` : AsyncWebServer port 80 + WS port 81, mutation routes auth + rate limit

## Topology

`Nb_TCA × 4 == Nb_INA`, mismatch → fail-safe all OFF.

## Anti-Patterns

- Pas de `Serial.print` direct → `DebugLogger` (KxLogger.h) avec catégories
- CSV séparateur `;` (jamais `,` — historique terrain)
- Pas de credentials hardcodés, utiliser `credentials.h` (template `.example`)
