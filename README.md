<p align="center">
  <img src="BMU.jpeg" alt="BMU PCB" width="480" />
</p>

<h1 align="center">KXKM Batterie Parallelator</h1>

<p align="center">
  <strong>Battery Management Unit for parallel 24-30V packs</strong><br>
  Up to 32 batteries &middot; ESP32-S3 &middot; BLE + MQTT + InfluxDB &middot; iOS companion app
</p>

<p align="center">
  <a href="https://github.com/KomplexKapharnaum/KXKM_Batterie_Parallelator/actions/workflows/ci.yml">
    <img src="https://github.com/KomplexKapharnaum/KXKM_Batterie_Parallelator/actions/workflows/ci.yml/badge.svg" alt="CI" />
  </a>
  <a href="https://github.com/KomplexKapharnaum/KXKM_Batterie_Parallelator/actions/workflows/sim-host-tests.yml">
    <img src="https://github.com/KomplexKapharnaum/KXKM_Batterie_Parallelator/actions/workflows/sim-host-tests.yml/badge.svg" alt="QA" />
  </a>
  <img src="https://img.shields.io/badge/ESP--IDF-v5.4-blue" alt="ESP-IDF" />
  <img src="https://img.shields.io/badge/license-GPLv3-green" alt="License" />
  <img src="https://img.shields.io/badge/tests-26%20passed-brightgreen" alt="Tests" />
</p>

---

## Overview

The **KXKM Batterie Parallelator** is an embedded Battery Management Unit (BMU) that safely parallels multiple battery packs (24-30V) for off-grid stage installations. Each battery is individually monitored (voltage, current, temperature) and can be disconnected in microseconds via MOSFET switches.

Built by [L'Electron Rare](https://lelectronrare.fr) for [KompleX KapharnaüM](https://komplex-kapharnaum.net) (Villeurbanne, France) — a live arts company deploying digital scenography in public spaces without grid power.

### Key Features

| Feature | Description |
|---------|-------------|
| Per-battery protection | Under/over-voltage, over-current, imbalance detection |
| Auto-reconnection | Exponential backoff with permanent lock after 5 faults |
| Soft-balancing | Duty-cycling with opportunistic R_int measurement |
| Victron integration | BLE Instant Readout + GATT SmartShunt emulation + device scanner |
| Touchscreen UI | 320x240 LVGL display with battery grid, SOH, charts |
| iOS companion app | BLE real-time monitoring, offline cache, role-based access |
| Cloud telemetry | MQTT + InfluxDB + Grafana with offline persistence |
| OTA updates | Dual-partition with automatic rollback |

---

## Quick Start

```bash
# ESP-IDF firmware (production)
cd firmware-idf
source ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor

# PlatformIO (legacy Arduino)
pio run -e kxkm-s3-16MB --target upload

# Run all tests (no hardware needed)
pio test -e sim-host          # 13 PlatformIO tests
cd firmware-idf && idf.py build  # 13 ESP-IDF host tests

# Cloud stack (kxkm-ai server)
cd kxkm-api && cp .env.example .env && docker compose up -d
```

---

## Architecture

```
                          ┌──────────────────────────┐
                          │    ESP32-S3-BOX-3         │
                          │    320x240 LVGL display   │
                          │    NimBLE + WiFi          │
                          └─────────┬────────────────┘
                                    │ I2C 50kHz
                    ┌───────────────┼───────────────┐
                    │               │               │
              ┌─────▼─────┐  ┌─────▼─────┐  ┌─────▼─────┐
              │ TCA9535    │  │ TCA9535   │  │  ...×8    │
              │ 4× MOSFET  │  │ 4× LED   │  │           │
              └─────┬─────┘  └───────────┘  └───────────┘
                    │
              ┌─────▼─────┐
              │ INA237 ×4  │  ← per TCA (voltage + current)
              └───────────┘

    ┌──────────────────────────────────────────────────────┐
    │                    Cloud Stack                        │
    │  BMU ──MQTT──► Mosquitto ──► Telegraf ──► InfluxDB   │
    │                                              │       │
    │  iPhone ◄──BLE──► BMU    Grafana ◄───────────┘       │
    │  iPhone ◄──REST──► FastAPI ◄─────────────────┘       │
    └──────────────────────────────────────────────────────┘
```

### Firmware Components (ESP-IDF, 25 modules)

| Component | Role |
|-----------|------|
| `bmu_protection` | State machine: 5 states, 5 criteria, permanent lock |
| `bmu_balancer` | Soft-balancing duty-cycling + R_int measurement |
| `bmu_ina237` | 16× INA237 power monitors (2mΩ shunt) |
| `bmu_tca9535` | 8× GPIO expanders (switches + LEDs) |
| `bmu_ble` | NimBLE 4 GATT services + Victron SmartShunt |
| `bmu_ble_victron` | Victron Instant Readout (AES-CTR encrypted ads) |
| `bmu_ble_victron_scan` | BLE central scanner for Victron devices |
| `bmu_display` | LVGL: battery grid, pack info, SOH, charts, swipe |
| `bmu_influx` + `_store` | InfluxDB client + offline FAT/SD persistence |
| `bmu_mqtt` | ESP-MQTT with auth credentials |
| `bmu_vedirect` | Victron VE.Direct UART parser (solar charger) |
| `bmu_rint` | Internal resistance pulse measurement |
| `bmu_web` | HTTP + WebSocket with token auth + rate limiting |
| `bmu_config` | NVS runtime config + Victron device keys |
| `bmu_storage` | NVS, FAT, SPIFFS, SD card, USB MSC |
| `bmu_ota` | OTA firmware update with rollback |

### Protection Logic

```
Nb_switch < 5   →  Reconnect immediately when condition clears
Nb_switch == 5  →  10-second delay before reconnection
Nb_switch > 5   →  Permanent lock (blinking red LED) until reboot
```

| Threshold | Default | Configurable |
|-----------|---------|:------------:|
| Min voltage | 24,000 mV | via Kconfig + NVS |
| Max voltage | 30,000 mV | via Kconfig + NVS |
| Max current | 10,000 mA | via Kconfig + NVS |
| Imbalance | 1,000 mV | via Kconfig + NVS |
| Reconnect delay | 10,000 ms | via Kconfig |
| Switch limit | 5 | via Kconfig |

### LED Behavior

| State | LED |
|-------|-----|
| Connected | Solid green |
| Disconnected | Solid red |
| Error | **Blinking red** (~1 Hz) |
| Locked | Solid red |

---

## iOS Companion App

<p align="center">
  <strong>KXKM BMU</strong> &mdash; SwiftUI + CoreBluetooth
</p>

| Feature | Transport |
|---------|-----------|
| Real-time battery dashboard | BLE (2s poll) |
| Battery detail + voltage chart | BLE |
| Switch ON/OFF + reset | BLE (role-gated) |
| Protection config (0.025V/A step) | BLE |
| WiFi config for BMU | BLE |
| Audit trail + filtering | Local SQLDelight |
| Offline mode with cache indicator | Auto-fallback |
| SOH dashboard + R_int | BLE + REST |
| Victron device scanning | BLE passive |

**3 roles**: Admin (full), Technician (control), Viewer (read-only) — PIN + Face ID.

**Code**: `iosApp/` (Xcode, CoreBluetooth) + `kxkm-bmu-app/` (KMP Shared Kotlin)

---

## Victron BLE Integration

The BMU interacts with the Victron ecosystem via three mechanisms:

| Mechanism | Direction | Protocol |
|-----------|-----------|----------|
| Instant Readout | BMU → VictronConnect | AES-CTR encrypted advertising (PID 0xA389) |
| GATT SmartShunt | BMU → any BLE client | 9 read-only characteristics (V, I, SOC, Ah, TTG, T, alarm) |
| Device Scanner | Victron devices → BMU | Passive BLE scan, AES decrypt, 8 device cache |

Supported Victron record types: Solar (0x01), Battery (0x02), Inverter (0x03), DC-DC (0x04).

---

## Cloud Infrastructure

Docker stack on `kxkm-ai` server:

| Service | Port | Role |
|---------|------|------|
| Mosquitto | 1883, 9001 | MQTT broker (authenticated) |
| InfluxDB 2.7 | 8086 | Time-series storage (org=kxkm, bucket=bmu) |
| Telegraf | - | MQTT → InfluxDB bridge |
| FastAPI | 8400 | REST API (sync, history, audit) |
| Grafana 11 | 3001 | 3 dashboards (live, fleet, solar) |

**Setup**: `cp .env.example .env` — fill all secrets (API key, InfluxDB token, MQTT credentials, Grafana password, CORS origins). All secrets via `${...}` env vars, `chmod 600 .env`.

**Offline resilience**: when WiFi/InfluxDB is unreachable, telemetry is persisted to internal FAT flash (`/fatfs/influx/`) with 2-file rotation (512KB each). Automatic replay on reconnection.

---

## Testing

**26 tests total** — no hardware required.

### PlatformIO sim-host (13 tests)

```bash
pio test -e sim-host
```

| Suite | Tests | Covers |
|-------|:-----:|--------|
| `test_protection` | 10 | V/I/imbalance/lock state machine |
| `test_battery_route_validation` | - | Index bounds + state checks |
| `test_influx_buffer_codec` | - | InfluxDB line-protocol encoding |
| `test_web_mutation_rate_limit` | 3 | Sliding window + multi-IP |
| `test_web_route_security` | 9 | Constant-time token + Bearer |
| `test_ws_auth_flow` | 7 | Combined auth + rate limit |
| `test_mqtt_influx_codec` | 3 | JSON MQTT + topic parsing |
| `test_emulation_bench` | - | Emulation benchmark |

### ESP-IDF Host Tests (13 tests)

```bash
cd firmware-idf && idf.py build
```

| Suite | Tests | Covers |
|-------|:-----:|--------|
| `test_protection` | 13 | Full state machine (ESP-IDF) |
| `test_victron_gatt` | 8 | GATT encoding (V, SOC, alarm, TTG, T) |
| `test_victron_scan` | 5 | Payload parsing, expiry, MAC, CID |
| `test_ble_victron` | - | Battery/solar payload encoding |
| `test_vedirect_parser` | - | VE.Direct frame parsing |
| `test_rint` | - | R_int calculation |
| `test_config_labels` | - | Battery label management |
| `test_vrm_topics` | - | VRM topic generation |

### CI/CD Pipelines

| Pipeline | Trigger | Gate |
|----------|---------|------|
| `ci.yml` | push/PR | sim-host 13 tests pass |
| `sim-host-tests.yml` | push/PR | + S3 build + RAM ≤75% + Flash ≤85% |
| `esp-idf-ci.yml` | push/PR | + ESP-IDF host tests + flash ≤85% of 2MB OTA |

**Current flash usage**: 81% (1.69 MB / 2 MB OTA partition) — 19% headroom.

---

## Security Audit

**All findings resolved** as of 2026-04-07.

<details>
<summary><strong>Phase 1 — Migration (March 2026): 7/7 resolved</strong></summary>

| ID | Finding | Status |
|----|---------|--------|
| CRIT-001 | Obsolete function calls | ✅ |
| CRIT-002 | Unreliable global Nb_INA | ✅ |
| CRIT-003 | I2C access without mutex | ✅ |
| CRIT-004 | Missing mutex init | ✅ |
| HIGH-005 | WebSocket V/I race | ✅ |
| HIGH-006 | reconnect_time race | ✅ |
| HIGH-007 | Uninitialized globals | ✅ |

</details>

<details>
<summary><strong>Phase 2 — Deep audit (March-April 2026): 13/13 resolved</strong></summary>

| ID | Severity | Finding | Fix |
|----|----------|---------|-----|
| CRIT-A | Critical | mV/V unit mismatch | ESP-IDF: mV everywhere |
| CRIT-B | Critical | Imbalance vs config instead of fleet max | bmu_protection |
| CRIT-C | Critical | Deadlock in web switch | bmu_web |
| CRIT-D | Critical | Unauthenticated routes | bmu_web_security |
| HIGH-1 | High | Negative overcurrent ignored | Bidirectional factor |
| HIGH-2 | High | Unauthenticated WebSocket | Token on first frame |
| HIGH-3 | High | XSS in /log | cJSON null check |
| HIGH-4 | High | MQTT plaintext | Mosquitto auth + .env |
| HIGH-5 | High | I2C speed change unguarded | I2CLockGuard |
| HIGH-7 | High | Public battery_voltages[] | state_mutex |
| HIGH-8 | High | Influx task crash on SD fail | influx_store fallback |
| MED-1 | Medium | No permanent lock (F08) | BMU_STATE_LOCKED |
| MED-010 | Medium | mV/V harmonization | mV throughout |

</details>

<details>
<summary><strong>Infrastructure audit (April 2026): 5/5 resolved</strong></summary>

| Finding | Fix |
|---------|-----|
| Mosquitto anonymous access | Auth + password file |
| Hardcoded secrets in docker-compose | All via .env (chmod 600) |
| Default InfluxDB token | Rotated (44-char random) |
| CORS allow_origins=["*"] | Restricted via env var |
| Offline data loss | FAT/SD persistence + replay |

</details>

---

## Hardware

### PCB Revisions

| Version | Status | Files |
|---------|--------|-------|
| BMU v1 | Manufactured | `hardware/PCB/` |
| BMU v2 | Production-ready (ERC 0, BOM 107) | `hardware/pcb-bmu-v2/` |

### Key ICs

| IC | Package | Role | I2C Address |
|----|---------|------|-------------|
| INA237 | VSSOP-10 | Power monitor (V/I/P) | 0x40-0x4F (16 max) |
| TCA9535 | TSSOP-24 | GPIO expander (switches + LEDs) | 0x20-0x27 (8 max) |
| ISO1540 | SOIC-8 | Galvanic I2C isolator | - |
| IRF4905 | D2PAK | P-channel MOSFET (55V, 74A) | - |

### I2C Mapping

```
TCA_0 (0x20) → INA 0x40-0x43 → Batteries 1-4
TCA_1 (0x21) → INA 0x44-0x47 → Batteries 5-8
TCA_2 (0x22) → INA 0x48-0x4B → Batteries 9-12
TCA_3 (0x23) → INA 0x4C-0x4F → Batteries 13-16
```

Topology constraint: `Nb_TCA × 4 == Nb_INA` — mismatch triggers fail-safe (all OFF).

---

## Project Structure

```
KXKM_Batterie_Parallelator/
├── firmware-idf/          # ESP-IDF 5.4 firmware (25 components)
│   ├── components/        # Modular ESP-IDF components
│   ├── main/              # app_main entry point
│   └── test/              # Unity host tests (13 suites)
├── firmware/              # Legacy PlatformIO/Arduino firmware
│   ├── src/               # Source modules
│   ├── test/              # sim-host tests (8 suites, 13 tests)
│   └── lib/INA237/        # Local INA237 driver fork
├── iosApp/                # iOS SwiftUI app (CoreBluetooth)
├── kxkm-bmu-app/          # KMP shared Kotlin module
│   └── shared/            # Cross-platform domain logic
├── kxkm-api/              # Docker stack (Mosquitto, InfluxDB, FastAPI, Grafana)
├── hardware/              # KiCad schematics + Gerbers (v1 + v2)
├── specs/                 # Kill_LIFE gates (S0-S3)
├── docs/                  # Specs, plans, governance, research
├── grafana/dashboards/    # 3 Grafana dashboard JSONs
└── scripts/               # CI, QA, ML pipeline
```

---

## About KompleX KapharnaüM

[KompleX KapharnaüM](https://komplex-kapharnaum.net) is a live arts company based in Villeurbanne (Lyon, France), active for 20+ years in street arts, live performance, and digital creation. The company develops its own embedded hardware platform (K32, ESP32-based) and maintains open-source tools on [GitHub](https://github.com/KomplexKapharnaum).

---

## License

[GNU General Public License v3.0](LICENSE)

---

<p align="center">
  <sub>Built with ESP-IDF, LVGL, NimBLE, SwiftUI, FastAPI, InfluxDB, and Grafana.</sub><br>
  <sub>Developed by <a href="https://lelectronrare.fr">L'Electron Rare</a> for <a href="https://komplex-kapharnaum.net">KompleX KapharnaüM</a></sub>
</p>
