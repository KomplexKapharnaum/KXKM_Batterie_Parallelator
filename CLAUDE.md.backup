# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Projet
- Client : KompleX KapharnaüM (Villeurbanne) — contact : nicolas.guichard@lehoop.fr
- BMU (Battery Management Unit) ESP32 — gestion sécurisée de batteries en parallèle 24–30 V
- Intégré au workflow Kill_LIFE (spec-first, gates S0–S3)

## Build & Test Commands

```bash
# === ESP-IDF firmware (production, firmware-idf/) ===
source ~/esp/esp-idf/export.sh
cd firmware-idf && idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor

# === PlatformIO (legacy Arduino, firmware/) ===
pio run -e kxkm-s3-16MB                      # build
pio run -e kxkm-s3-16MB --target upload       # flash
pio device monitor -e kxkm-s3-16MB --baud 115200

# === Tests (no hardware needed) ===
pio test -e sim-host                          # 13 PlatformIO tests
pio test -e sim-host -f test_protection       # single suite
pio test -e sim-host -vv                      # verbose
cd firmware-idf/test && make all              # 13 ESP-IDF host tests (Unity, g++)
cd firmware-idf/test && make test_protection  # single ESP-IDF test suite

# === Rust firmware (firmware-rs/) ===
cd firmware-rs && cargo build
cargo test                                    # unit tests (no hardware)

# === ML / LLM tests ===
uv run python -m pytest tests/llm/

# === Memory budget (required before large changes) ===
scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85

# === Cloud stack (kxkm-api/) ===
cd kxkm-api && cp .env.example .env && docker compose up -d

# === SOH services (services/) ===
cd services/soh-scoring && uv run uvicorn soh_api:app  # scoring API
cd services/soh-llm && uv run uvicorn app:app          # LLM diagnostic

# === ML pipeline (scripts/ml/) ===
uv run python scripts/ml/extract_features.py
uv run python scripts/ml/train_fpnn.py
uv run python scripts/ml/quantize_tflite.py
```

**PlatformIO gotchas:**
- `[arduino_base]` in platformio.ini holds `framework=arduino`. Do NOT put `framework=arduino` in the global `[env]` — it breaks the `native`/`sim-host` test environments.
- `[env:sim-host]` has `build_src_filter` listing which `.cpp` files are compiled for host tests. When adding a new testable module, you must add its `.cpp` to this filter or it won't link.

## Architecture

### Three Firmware Tracks

| Track | Directory | Framework | Status |
|-------|-----------|-----------|--------|
| ESP-IDF | `firmware-idf/` | ESP-IDF 5.4, 25 components | **Production** (ESP32-S3-BOX-3) |
| Arduino | `firmware/` | PlatformIO/Arduino | Legacy (sim-host tests still active) |
| Rust | `firmware-rs/` | Cargo workspace, 5 crates | Experimental (FFI bridge to ESP-IDF) |

### ESP-IDF Execution Flow (firmware-idf/)
Boot sequence (14 stages): NVS → SPIFFS → Display → WiFi → BLE → FAT → SNTP → I2C scan → topology → protection → cloud → web → VE.Direct → OTA

Key components: `bmu_protection` (state machine), `bmu_balancer` (duty-cycling), `bmu_ble` (NimBLE 4 GATT services), `bmu_display` (LVGL), `bmu_influx` + `bmu_influx_store` (offline persistence), `bmu_web` + `bmu_web_security`. See component READMEs and Kconfig files for details.

### Arduino Execution Flow (firmware/)
1. `main.cpp` setup: Serial → Wire (SDA=32, SCL=33, 50kHz) → i2cMutexInit → INAHandler.begin → TCAHandler.begin → topology check → BattParallelator config
2. `main.cpp` loop (500ms): iterates all INAs via `BattParallelator.check_battery_connected_status(i)`. If topology invalid, forces all batteries OFF (fail-safe).

### Rust Firmware (firmware-rs/)
Cargo workspace with 5 crates: `bmu-i2c` (bus abstraction), `bmu-ina237` (sensor driver), `bmu-tca9535` (GPIO expander), `bmu-protection` (state machine), `bmu-ffi` (C FFI bridge with cbindgen). Hybrid C/Rust: I2C initialized by ESP-IDF C, passed to Rust via FFI. `no_std` compatible.

### Protection Thresholds (configurable via Kconfig + NVS)

| Threshold | Default |
|-----------|---------|
| Min voltage | 24,000 mV |
| Max voltage | 30,000 mV |
| Max current | 10,000 mA |
| Imbalance | 1,000 mV |
| Reconnect delay | 10,000 ms |
| Switch limit | 5 (>5 = permanent lock until reboot) |

### Topology Constraint
Hard validation: `Nb_TCA * 4 == Nb_INA`. Mismatch → fail-safe (all batteries OFF). Typical: 4 TCA + 16 INA.

### Thread Safety
- `I2CLockGuard` (RAII) wraps ALL I2C operations — INAHandler, TCAHandler, WebServerHandler
- `stateMutex` in BatteryParallelator and BatteryManager for shared state across FreeRTOS tasks
- `reconnect_time[]` access is mutex-protected
- `i2cBusRecovery()` triggered on 5+ consecutive I2C failures

### Offline Data Persistence
When WiFi/InfluxDB is unreachable, telemetry persists to FAT internal flash:
- Primary: `/fatfs/influx/current.lp` → rotates to `rotated.lp` at 512 KB (max 1 MB)
- Fallback: `/sdcard/influx/` if SD card present
- Replay: automatic on WiFi reconnection (cloud_telemetry_task, every 10s)

## ML Pipeline

SOH (State of Health) prediction deployed on ESP32 as TFLite INT8 (12.2 KB):
- **Pipeline**: `train_fpnn.py` → `quantize_tflite.py` → INT8 model (MAPE ~10%)
- **Models**: `models/` (`.pt`, `.onnx`, `.tflite`), features in `.parquet`
- **Remote training**: `scripts/ml/remote_kxkm_ai_pipeline.sh` on kxkm-ai (RTX 4090)
- **Services**: `services/soh-scoring/` (PyTorch API) + `services/soh-llm/` (Qwen2.5-7B diagnostics, port 8401)

## Test Suites (26 total, no hardware needed)

### PlatformIO sim-host (`pio test -e sim-host`)
| Suite | Covers |
|-------|--------|
| `test_protection` (10) | V/I/imbalance/lock state machine |
| `test_battery_route_validation` | Index bounds + state checks |
| `test_influx_buffer_codec` | InfluxDB line-protocol encoding |
| `test_web_mutation_rate_limit` (3) | Sliding window + multi-IP |
| `test_web_route_security` (9) | Constant-time token + Bearer |
| `test_ws_auth_flow` (7) | Combined auth + rate limit |
| `test_mqtt_influx_codec` (3) | JSON MQTT + topic parsing |

### ESP-IDF host (`cd firmware-idf/test && make all`)
| Suite | Covers |
|-------|--------|
| `test_protection` (13) | Full state machine (ESP-IDF) |
| `test_victron_gatt` (8) | GATT encoding (V, SOC, alarm, TTG, T) |
| `test_victron_scan` (5) | Payload parsing, expiry, MAC, CID |
| `test_ble_victron` | Battery/solar payload encoding |
| `test_vedirect_parser` | VE.Direct frame parsing |
| `test_rint` | R_int calculation |
| `test_config_labels` | Battery label management |
| `test_vrm_topics` | VRM topic generation |

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
- Python projects use `uv` (not pip/venv)

## iOS App (iosApp/)

See `iosApp/CLAUDE.md` for full details. Key points:
- SwiftUI + CoreBluetooth, no external dependencies
- BLE polls every 2s (firmware notification bug workaround)
- Custom 128-bit UUID GATT protocol (battery/system/control/Victron services)
- 3 roles: Admin, Technician, Viewer (PIN + Face ID)
- Source of truth: `project.yml` → regenerate after target changes: `xcodegen generate`
- **Never** edit `project.pbxproj` manually
- Build: `xcodebuild build -scheme KXKMBmu -destination "platform=iOS,id=<DEVICE_ID>"`

## KMP Shared (kxkm-bmu-app/shared/)

Kotlin Multiplatform module — compiles for iOS arm64.
- Build requires JDK 17: `export JAVA_HOME=/opt/homebrew/opt/openjdk@17`
- Build framework: `./gradlew :shared:linkDebugFrameworkIosArm64`
- Gradle wrapper: 8.5 (not 9.x — AGP 8.2.0 incompatible)
- SKIE disabled (Gradle 9.x incompatible) — use manual callback bridges

## CI/CD

- `ci.yml`: PlatformIO `pio test -e sim-host` on push/PR (13 tests)
- `sim-host-tests.yml`: sim-host + S3 build + memory budget (RAM ≤75%, Flash ≤85%)
- Current flash usage: 81% (19% headroom on 2MB OTA)

## Gates Kill_LIFE
- S0 ✅ intake + specs
- S1 ✅ tests natifs 13/13 + bug fix `find_max_voltage`
- S2 ✅ KiCad BMU v2 review (ERC 0 violations)
- S3 ⬜ validation terrain (batteries réelles)

## Known Technical Debt
- Web UI assets are embedded as string literals in headers — consider SPIFFS/LittleFS migration
- KMP Shared framework not yet integrated in Xcode project (types prefixed `Shared*` need bridge layer)
- NVS config overrides sdkconfig defaults — erase NVS partition to reset: `esptool erase_region 0x9000 0x6000`
