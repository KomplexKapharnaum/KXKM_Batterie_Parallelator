# Handoff

## State
Firmware v0.5.0 on `main`. WiFi "Les cils"/mascarade connecté, BLE+WiFi coex OK. MQTT → photon:1883 → bridge Tailscale → kxkm-ai:1883 → Telegraf → InfluxDB. Données temps réel arrivent dans Grafana. iOS app deployée sur iPhone 16 Pro avec BLE CoreBluetooth (UUID fixé). Import historique SD card (48M entries, 4 BMU: gocab/k-led1/k-led2/tender) en cours vers InfluxDB. Dashboard "BMU Fleet — Historical" créé dans Grafana.

## Next
1. **Vérifier import terminé** — `tail -f /tmp/influx_import.log`, puis checker le dashboard Fleet dans Grafana
2. **Tester BLE iPhone↔BOX-3** — ouvrir l'app, vérifier que la barre passe au vert "BLE", données réelles s'affichent
3. **Fix KMP Gradle** — `kxkm-bmu-app/shared/build.gradle.kts` cocoapods issues, remplacer par XCFramework export pour brancher le vrai shared module dans l'app iOS

## Context
- Mosquitto local sur photon: `/opt/homebrew/etc/mosquitto/mosquitto.conf` avec bridge vers 100.87.54.119:1883 (kxkm-ai Tailscale)
- SSH tunnel aussi actif: `0.0.0.0:1884 → kxkm-ai:1883` (PID en background)
- BOX-3 IP: 192.168.0.250 sur "Les cils", web server: `http://192.168.0.250/api/batteries`
- SOH component stubbed (`bmu_soh_stub.c`) — TFLite disabled
- iOS app: stubs in `iosApp/KXKMBmu/Stubs/`, BleManager.swift = real CoreBluetooth, SharedStubs.swift = mock models+use cases
- Grafana: kxkm/kxkm-bmu-2026, dashboards: "BMU" (live) + "BMU Fleet — Historical" (logs)
- CSV logs format: `;` separator, 8 batteries, Volts en V, Current en A, measurement=battery_log
