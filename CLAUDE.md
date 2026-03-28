# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

KXKM Batterie Parallelator is an ESP32-based parallel battery management system (BMU) that monitors, protects, and logs up to 16 batteries. Built by Komplex Kapharnaum (Lyon-based art company) for stage/installation power systems. The codebase is primarily in French (comments, README, variable names).

## Build & Flash (PlatformIO)

```sh
# Build for production target
pio run -e kxkm-v3-16MB

# Build for debug target (FTDI)
pio run -e esp-wrover-kit

# Upload firmware
pio run -e kxkm-v3-16MB --target upload

# Serial monitor (115200 baud, with exception decoder)
pio device monitor -e kxkm-v3-16MB

# Memory budget guardrail (must stay under 75% RAM, 85% Flash)
scripts/check_memory_budget.sh --env kxkm-v3-16MB --ram-max 75 --flash-max 85
```

## Architecture

### Hardware Layer
- **INA226** current/voltage sensors at I2C addresses 0x40-0x4F (up to 16 channels)
- **TCA9555** GPIO expanders at 0x20-0x27 controlling MOSFET battery switches and LEDs
- **I2C bus**: GPIO 32 (SDA), GPIO 33 (SCL), 400 kHz
- **SD card** via SPI for CSV data logging
- Up to 4 motherboards chained, each with 4 extension cards (MOSFET-based, rated 40A)

### FreeRTOS Tasks (concurrent, pinned to cores)
1. **readINADataTask** - polls INA226 sensors continuously
2. **logDataTask** - writes CSV to SD card (default 1s interval)
3. **checkBatteryVoltagesTask** - monitors thresholds, manages reconnection logic (1s)
4. **sendStoredDataTask** - uploads buffered data to InfluxDB (5s)
5. **webServerTask** - AsyncWebServer for remote control (250ms cycle)

### Thread Safety
All I2C access is serialized through a FreeRTOS mutex (`I2CMutex.h`). Any new code touching the Wire bus must acquire this mutex.

### Key Classes
- **BatteryParallelator** (`src/BatteryParallelator.cpp/.h`) - core battery management: thresholds, switching logic, protection
- **BatteryManager** (`src/BatteryManager.cpp/.h`) - per-battery state aggregation (voltage min/max/avg, Ah tracking)
- **INAHandler** (`src/INA_NRJ_lib.cpp/.h`) - INA226 sensor abstraction
- **TCAHandler** (`src/TCA_NRJ_lib.cpp/.h`) - TCA9555 GPIO control for switches/LEDs
- **SDLogger** (`src/SD_Logger.cpp/.h`) - CSV logging to SD card
- **WebServerHandler** (`src/WebServerHandler.cpp/.h`) - async HTTP API
- **InfluxDBHandler** (`src/InfluxDBHandler.cpp/.h`) - time-series DB integration
- **DebugLogger** (`lib/DebugLogger/`) - custom multi-level logging library (local lib)

### Key Configuration (main.cpp)
- Voltage range: 24V-30V, shunt: 2000 micro-ohm
- Max charge/discharge current: 1000 mA
- Reconnect delay: 10s, max switch attempts: 5
- Voltage imbalance tolerance: 0.5V

### ML Component (offline, not embedded yet)
Scripts in `scripts/ml/` train an FPNN model for battery State-of-Health estimation:
- `parse_csv.py` -> `extract_features.py` -> `train_fpnn.py` -> `quantize_tflite.py`
- Trained model: `models/fpnn_soh.pt` (~5K params)

## Build Environments

| Environment | Board | Purpose | Upload Speed |
|---|---|---|---|
| `kxkm-v3-16MB` | esp-wrover-kit (16MB flash) | Production | 512000 |
| `esp-wrover-kit` | esp-wrover-kit | Debug (FTDI) | 921600 |

Platform pinned to espressif32 v6.9.0. Custom partition table: `partitions_16MB.csv`.

## Credentials

`src/credentials.h` contains InfluxDB tokens and is not committed. A template exists at `src/credentials.h.example`. WiFi SSID is configured in `main.cpp`.
