# Handoff

## State
Firmware v0.5.0 on `main` — display OK, BLE "KXKM-BMU" advertising, WiFi coex, `/api/system` + `/api/solar` routes, BLE WiFi Config/Status chars (0x0034/0x0035). iOS app deployed to iPhone 16 Pro with mock stubs (no real BLE yet). KMP shared module scaffolded in `kxkm-bmu-app/shared/` (43 Kotlin files, not build-verified — Gradle/Kotlin version issues). Android Compose scaffolded in `kxkm-bmu-app/androidApp/`. Docker stack running on kxkm-ai: Mosquitto(:1883), InfluxDB(:8086), Telegraf, FastAPI(:8400), Grafana(:3001).

## Next
1. **Fix KMP Gradle build** — `kxkm-bmu-app/shared/build.gradle.kts` has cocoapods/version issues with Gradle 9.x + Kotlin 2.1. Replace cocoapods block with XCFramework export. Then build iOS framework and replace `iosApp/KXKMBmu/Stubs/SharedStubs.swift` with real Shared module.
2. **Wire real BLE** in iOS app — replace mock `MonitoringUseCase` with Kable BLE transport connecting to KXKM-BMU device.
3. **Connect firmware to kxkm-ai MQTT** — needs WiFi AP available. MQTT broker URI already set to `mqtt://kxkm-ai:1883` in sdkconfig.

## Context
- BSP fix in `external/esp-bsp/bsp/esp-box-3/esp-box-3.c` — dock I2C failure is non-fatal (committed in submodule but `external/` is gitignored).
- INA237 at 0x40 has Device ID 0x2381 (INA237A variant) — accepted via family mask 0x23xx.
- WiFi buffers reduced for BLE coex: `static_rx_buf_num=4, dynamic=16`. WiFi init is non-fatal.
- iOS signing: Team JRLQM7V3V5 (c.saillant@gmail.com). iPhone needs "Trust Developer" in Settings > General > VPN & Device Management.
- Infra creds: InfluxDB/Grafana = kxkm/kxkm-bmu-2026, token = kxkm-influx-token-2026, API key in `kxkm-api/.env` on server.
