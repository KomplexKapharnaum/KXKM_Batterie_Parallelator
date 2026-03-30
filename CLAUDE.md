# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Projet
- Client : KompleX KapharnaüM (Villeurbanne) — contact : nicolas.guichard@lehoop.fr
- BMU (Battery Management Unit) ESP32 — gestion sécurisée de batteries en parallèle 24–30 V
- Intégré au workflow Kill_LIFE (spec-first, gates S0–S3)

## Build & Test Commands

```bash
# Production build (ESP32-S3)
pio run -e kxkm-s3-16MB

# Upload to board
pio run -e kxkm-s3-16MB --target upload

# Serial monitor
pio device monitor -e kxkm-s3-16MB --baud 115200

# Run all host-based unit tests (no hardware needed)
pio test -e sim-host

# Verbose test output
pio test -e sim-host -vv

# Memory budget check (required before large changes)
scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85

# Legacy build (deprecated)
pio run -e kxkm-v3-16MB
```

**PlatformIO gotcha:** `[arduino_base]` in platformio.ini holds `framework=arduino`. Do NOT put `framework=arduino` in the global `[env]` — it breaks the `native`/`sim-host` test environments.

## Architecture

### Execution Flow
1. `main.cpp` setup: Serial → Wire (SDA=32, SCL=33, 50kHz) → i2cMutexInit → INAHandler.begin → TCAHandler.begin → topology check → BattParallelator config
2. `main.cpp` loop (500ms): iterates all INAs via `BattParallelator.check_battery_connected_status(i)`. If topology invalid, forces all batteries OFF (fail-safe).

### Core Modules
- **BatteryParallelator** — protection state machine: 5 criteria (under/over-voltage, over-current, voltage imbalance, reconnect logic with switch counting). Reconnect: immediate if nb_switch<5, 10s delay at 5, permanent lock >5.
- **BatteryManager** — aggregate battery state, energy accounting (Ah via FreeRTOS task), min/max/avg voltage.
- **INAHandler** — manages up to 16 INA237 sensors (I2C 0x40–0x4F). `read_voltage_current()` is the atomic dual-read method.
- **TCAHandler** — manages up to 8 TCA9535 GPIO expanders (I2C 0x20–0x27). Pin mapping per TCA: pins 0–3 = battery MOSFET switches, pins 8–15 = LED pairs (red/green).
- **I2CMutex.h** — global FreeRTOS semaphore + RAII `I2CLockGuard`. ALL I2C access must go through this. `i2cBusRecovery()` on 5+ consecutive failures.
- **WebServerHandler** — AsyncWebServer on port 80 + WebSocket on 81. Mutation routes (`/api/battery/:id/switch_on|off`) require auth token + rate limiting (10 reqs/10s per IP).

### Topology Constraint
Hard validation: `Nb_TCA * 4 == Nb_INA`. Mismatch → fail-safe (all batteries OFF). Typical: 4 TCA + 16 INA.

### Thread Safety
- `I2CLockGuard` wraps all I2C operations (INAHandler, TCAHandler, WebServerHandler)
- `stateMutex` in BatteryParallelator and BatteryManager for shared state access across FreeRTOS tasks
- `reconnect_time[]` access is mutex-protected

## Structure
- `specs/` — Kill_LIFE gates: intake (S0), spec (S1), arch (S2), plan (S3)
- `firmware/src/` — firmware Arduino/PlatformIO (main.cpp + headers)
- `firmware/test/` — Unity native tests (sim-host): test_protection, test_battery_route_validation, test_influx_buffer_codec, test_web_mutation_rate_limit, test_web_route_security, test_emulation_bench
- `firmware/lib/INA237/` — local INA237 driver fork
- `firmware/data/` — web UI assets (served via embedded headers: WebServerFilesHtml.h, etc.)
- `hardware/PCB/` — BMU v1 (manufactured, legacy)
- `hardware/pcb-bmu-v2/` — BMU v2 (KiCad 8.0, ERC 0 violations, BOM 107 components)

## Hardware (KiCad)

KiCad-cli via Docker (repo is outside Kill_LIFE root, use direct Docker):
```bash
docker run --rm --user $(id -u):$(id -g) -e HOME=/tmp \
  -v "/home/clems/KXKM_Batterie_Parallelator:/project" \
  -w /project kicad/kicad:10.0 kicad-cli sch erc \
  --format json --output "/project/hardware/pcb-bmu-v2/erc_report.json" \
  "/project/hardware/pcb-bmu-v2/BMU v2.kicad_sch"
```

### ICs clés
- INA237 (VSSOP-10) — mesure V/I/P via I²C @ 0x40–0x4F (16 max)
- TCA9535PWR (TSSOP-24) — GPIO expander relais via I²C @ 0x20–0x27 (8 max)
- ISO1540 (SOIC-8) — isolateur I²C galvanique
- Adressage via solder jumpers JP2–JP20

## Safety-Critical Rules
- **Never** weaken voltage/current thresholds, reconnect delays, or topology guards
- **Never** bypass INA/TCA topology validation in main.cpp
- **All** I2C access must use `I2CLockGuard` from I2CMutex.h
- Validate sensor/battery indexes before array or driver access
- Preserve NAN/early-return fault semantics for sensor failures
- Keep FreeRTOS tasks non-blocking — no long blocking calls in periodic tasks
- Web mutation routes require auth token — no unauthenticated ON/OFF paths

## Conventions
- French comments and identifiers throughout — maintain consistency with surrounding code
- Arduino/ESP32 C++ style, fixed-size arrays where already used
- Use `DebugLogger` categories (KxLogger.h) instead of ad-hoc `Serial.print`
- CSV logging uses `;` separator — keep unchanged
- Config thresholds in `firmware/src/config.h` (values in mV/mA)
- WiFi/MQTT secrets go in `firmware/src/credentials.h` (template: `credentials.h.example`)

## CI/CD
- GitHub Actions: `ci.yml` runs `pio test -e native` on push/PR
- `sim-host-tests.yml`: matrix of sim-host tests + S3 build + memory budget gate
- ESP32 hardware build is local-only (not in CI)

## Gates Kill_LIFE
- S0 ✅ intake + specs
- S1 ✅ tests natifs 10/10 + bug fix `find_max_voltage`
- S2 ✅ KiCad BMU v2 review (ERC 0 violations)
- S3 ⬜ validation terrain (batteries réelles)

## Known Technical Debt
- MED-010: mV/V unit inconsistency between config.h and documentation — standardize to mV throughout
- Legacy headers (INA_Func.h, BatterySwitchCtrl.h) contain duplicated logic — refactoring candidates
- Web UI assets are embedded as string literals in headers — consider SPIFFS/LittleFS migration
