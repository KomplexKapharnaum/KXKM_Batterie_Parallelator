# ESP-IDF Firmware (Production)

ESP-IDF 5.4 firmware pour ESP32-S3-BOX-3, **25 composants** modulaires, LVGL display, NimBLE, MQTT, OTA.

## Build & Flash

```bash
source ~/esp/esp-idf/export.sh
idf.py build                              # ~30-60s incremental
idf.py -p /dev/cu.usbmodem* flash monitor # flash + serial
idf.py menuconfig                          # configure sdkconfig
```

**Erase NVS** (si config corrompue ou override sdkconfig) :
```bash
python3 -m esptool --chip esp32s3 -p /dev/cu.usbmodem* erase_region 0x9000 0x6000
```

## Boot Sequence (14 étapes)

NVS → SPIFFS → Display → WiFi → BLE → FAT → SNTP → I2C scan → topology → protection → cloud → web → VE.Direct → OTA

## Architecture clé

- **bmu_protection** : state machine 5 états (CONNECTED/DISCONNECTED/RECONNECTING/ERROR/LOCKED), lock après 5 reconnexions
- **bmu_balancer** : soft-balancing duty-cycling (3 ON / 2 OFF) + R_int opportuniste
- **bmu_ble** : NimBLE, 4 services GATT (battery/system/control/Victron SmartShunt), `bmu_ble_set_nb_ina()` post-scan I2C
- **bmu_influx + _store** : InfluxDB line-protocol + persistence offline FAT/SD (rotation 512KB)
- **bmu_mqtt** : ESP-MQTT auth user/password (NVS override possible)
- **bmu_display** : LVGL tabview, swipe detail, pack info line, R_int column
- **bmu_storage** : NVS, FAT (`/fatfs`), SPIFFS (`/spiffs`), SD (`/sdcard` SPI PMOD2)

## Offline Data Persistence

Si WiFi/InfluxDB down, télémétrie persistée :
- Primary : `/fatfs/influx/current.lp` → rotation `rotated.lp` à 512KB (max 1MB)
- Fallback : `/sdcard/influx/`
- Replay automatique au reconnect WiFi (cloud_telemetry_task, 10s)

## Safety

- I2C : tout passe par `bmu_i2c_lock()` / `bmu_i2c_unlock()`
- `bmu_protection` skip les batteries où `bmu_balancer_is_off()` (évite incrémentation `nb_switch`)
- Tâches : protection (prio 5), balancer (prio 4), display (esp_timer), cloud (prio 2)

## Anti-Patterns

- Ne pas lire `s_nb_ina` figé — utiliser `prot->nb_ina` dynamique post-scan
- Ne pas oublier d'ajouter un nouveau composant aux REQUIRES de `main/CMakeLists.txt`
- Ne pas appeler `ble_gap_adv_set_fields()` sans `name` — macOS/iOS perd la pub
