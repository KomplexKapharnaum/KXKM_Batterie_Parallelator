# Migration Arduino → ESP-IDF v5.3 — Design Spec

**Projet:** KXKM Batterie Parallelator (BMU)
**Date:** 2026-03-30
**Auteur:** Clément Saillant / L'Électron Rare
**Status:** Draft

---

## 1. Objectif

Migrer l'intégralité du firmware BMU du framework Arduino/PlatformIO vers ESP-IDF v5.3 LTS natif (CMake). Migration Big Bang — un seul passage, pas de cohabitation Arduino/IDF.

**Cible hardware:** ESP32-S3-BOX-3 (Espressif, ESP32-S3 dual-core 240 MHz, 16 MB flash, 8 MB PSRAM, écran tactile 2.4" ILI9341, codec audio ES8311, I2C/SPI exposés)
**Legacy abandonnée:** ESP32 Wrover (`kxkm-v3-16MB`) — plus supporté post-migration.

**Note BOX-3 :** Le S3-BOX-3 dispose d'un écran tactile 320x240 + codec audio. La migration initiale n'utilise pas ces périphériques pour le BMU, mais l'architecture doit permettre leur ajout futur (dashboard batterie sur écran, alertes sonores). Les GPIOs I2C doivent être configurés sur les broches exposées du BOX-3 (vérifier pinout vs GPIO 32/33 actuels — le BOX-3 utilise des GPIOs différents).

## 2. Motivations

- **Performance/contrôle:** OTA sécurisée, partitions custom, deep sleep, Bluetooth
- **Stabilité:** Wire non thread-safe, AsyncWebServer instable → drivers ESP-IDF natifs
- **Mémoire:** Framework Arduino consomme ~200 KB flash inutilement
- **Maintenance:** Libs tierces Arduino (INA226, TCA9555) mal maintenues
- **Vision KXKM:** Alignement écosystème K32 qui migre vers IDF

## 3. Inventaire actuel → Cible

### 3.1 Dépendances externes

| Lib Arduino (PlatformIO) | Rôle | Remplacement ESP-IDF |
|---|---|---|
| `Wire` | I2C bus | `driver/i2c_master.h` (ESP-IDF v5.x new driver) |
| `INA226` (RobTillaart) | Sensor V/I | Driver custom sur `i2c_master` |
| `TCA9555` (RobTillaart) | GPIO expander | Driver custom sur `i2c_master` |
| `AsyncTCP` + `ESPAsyncWebServer` | HTTP server | `esp_http_server.h` (built-in) |
| `WebSocketsServer` | WebSocket | `esp_http_server` ws support |
| `ArduinoJson` | JSON | `cJSON` (built-in ESP-IDF) |
| `Arduino_JSON` | JSON | `cJSON` |
| `PubSubClient` | MQTT | `esp_mqtt` (built-in) |
| `NTPClient` | NTP | `esp_sntp` (built-in) |
| `InfluxDB-Client` | Cloud push | `esp_http_client` custom |
| `WiFi` | WiFi | `esp_wifi` + `esp_netif` |
| `SD` / `SPIFFS` | Stockage | `esp_vfs_fat` + `sdmmc_host` / `esp_spiffs` |
| `EEPROM` | Config persist | `nvs_flash` |
| `INA237` (lib locale) | INA237 driver | Intégré dans nouveau driver I2C |

### 3.2 APIs Arduino → ESP-IDF

| API Arduino | Occurrences (~) | Remplacement ESP-IDF |
|---|---|---|
| `Serial.print/println` | ~20 | `ESP_LOGx(TAG, ...)` |
| `KxLogger.println()` | ~174 | `ESP_LOGx(TAG, ...)` |
| `Wire.begin/setClock` | ~5 | `i2c_new_master_bus()` |
| `Wire.beginTransmission/endTransmission` | ~10 | `i2c_master_transmit/receive` |
| `String` (Arduino) | ~200+ | `std::string` ou `snprintf` |
| `delay()` | ~15 | `vTaskDelay(pdMS_TO_TICKS(ms))` |
| `millis()` | ~10 | `esp_timer_get_time() / 1000` ou `xTaskGetTickCount()` |
| `WiFi.begin/status` | ~10 | `esp_wifi_start()` + event handler |
| `SD.begin/open` | ~10 | `esp_vfs_fat_sdmmc_mount()` + `fopen()` |
| `server.on()` | ~6 | `httpd_register_uri_handler()` |

### 3.3 Fichiers source (migration exhaustive)

| Fichier | LOC | Complexité migration |
|---|---|---|
| `main.cpp` → `main.c` ou `main.cpp` | 110 | Faible — setup/loop → `app_main()` |
| `config.h` | 29 | Aucune — pure C |
| `I2CMutex.h` | 90 | Faible — déjà FreeRTOS natif |
| `INAHandler.h/cpp` | ~450 | Haute — réécriture driver I2C |
| `TCAHandler.h/cpp` | ~250 | Haute — réécriture driver I2C |
| `BatteryParallelator.h/cpp` | ~630 | Moyenne — purge String, millis |
| `BatteryManager.h/cpp` | ~400 | Moyenne — purge String |
| `WebServerHandler.h/cpp` | ~400 | Haute — réécriture HTTP/WS |
| `SD_Logger.h/cpp` | ~400 | Moyenne — VFS + fopen |
| `InfluxDBHandler.h/cpp` | ~500 | Haute — esp_http_client |
| `MQTTHandler.h/cpp` | ~100 | Faible — esp_mqtt |
| `TimeAndInfluxTask.h/cpp` | ~100 | Faible — esp_sntp |
| `KxLogger.h` | 160 | Supprimé — esp_log |
| `WebServerFiles*.h` | ~5000 | Migré SPIFFS/LittleFS embedded |
| `pin_mappings.h` | 30 | Aucune — pure C |
| `BatteryRouteValidation.h/cpp` | ~100 | Faible |
| `WebMutationRateLimit.h/cpp` | ~50 | Faible |
| `WebRouteSecurity.h/cpp` | ~50 | Faible |

## 4. Architecture cible

### 4.1 Structure projet ESP-IDF

```
firmware/
├── CMakeLists.txt                  # Projet top-level
├── sdkconfig.defaults              # Config ESP-IDF (flash 16MB, PSRAM 8MB, log level, etc.)
├── partitions.csv                  # Table partitions custom
├── main/
│   ├── CMakeLists.txt
│   ├── main.cpp                    # app_main() → init + task creation
│   └── Kconfig.projbuild           # Config menus custom (seuils, WiFi, etc.)
├── components/
│   ├── bmu_i2c/                    # Bus I2C + mutex
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_i2c.h
│   │   └── bmu_i2c.cpp
│   ├── bmu_ina237/                 # Driver INA237
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_ina237.h
│   │   └── bmu_ina237.cpp
│   ├── bmu_tca9535/                # Driver TCA9535
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_tca9535.h
│   │   └── bmu_tca9535.cpp
│   ├── bmu_protection/             # BatteryParallelator + BatteryManager
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_protection.h
│   │   ├── include/bmu_battery_manager.h
│   │   └── bmu_protection.cpp
│   │   └── bmu_battery_manager.cpp
│   ├── bmu_web/                    # HTTP server + WebSocket + routes
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_web.h
│   │   └── bmu_web.cpp
│   ├── bmu_storage/                # SD + NVS
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_storage.h
│   │   └── bmu_storage.cpp
│   ├── bmu_cloud/                  # MQTT + InfluxDB + SNTP
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_cloud.h
│   │   └── bmu_cloud.cpp
│   └── bmu_config/                 # Config (seuils, Kconfig bridge)
│       ├── CMakeLists.txt
│       ├── include/bmu_config.h
│       └── bmu_config.cpp
├── data/                           # Web assets (SPIFFS partition)
│   ├── index.html
│   ├── style.css
│   └── script.js
└── test/                           # Tests host (CMock/Unity)
    └── test_protection/
        └── test_protection.cpp
```

### 4.2 Logging (remplacement KxLogger)

Chaque fichier source definit son TAG :

```cpp
static const char *TAG = "BATT";  // dans bmu_protection.cpp
// ...
ESP_LOGE(TAG, "Battery %d overvoltage: %.1f mV", idx, voltage);
ESP_LOGW(TAG, "Battery %d imbalance: %.1f V diff", idx, diff);
ESP_LOGI(TAG, "Battery %d connected", idx);
ESP_LOGD(TAG, "Battery %d read: V=%.3f I=%.3f", idx, v, i);
```

Tags par composant : `MAIN`, `I2C`, `INA`, `TCA`, `BATT`, `BMGR`, `WEB`, `SD`, `MQTT`, `INFLUX`, `SNTP`.

Filtrage runtime : `esp_log_level_set("BATT", ESP_LOG_WARN);`

### 4.3 GPIO Mapping ESP32-S3-BOX-3

**Bus I2C interne BOX-3 (I2C_NUM_0) — NE PAS utiliser pour BMU :**
- SDA → GPIO8 / SCL → GPIO18
- Partagé avec : ES8311 (codec), ICM-42607-P (IMU), ATECC608A (crypto), touch (GPIO3)
- Fréquence : 400 kHz (incompatible avec BMU qui demande 50 kHz câblage long)

**Bus I2C dédié BMU (I2C_NUM_1) — via PMOD1 du dock :**
- **SDA → GPIO41** (`BSP_PMOD1_IO8`)
- **SCL → GPIO40** (`BSP_PMOD1_IO4`)
- ⚠️ Pull-ups NON montées sur le dock → **pull-ups 4.7kΩ externes requises** sur le PCB BMU
- Fréquence : 50 kHz (fiabilité câblage long vers INA237/TCA9535)
- Source BSP : `BSP_I2C_DOCK_SCL = GPIO_NUM_40` / `BSP_I2C_DOCK_SDA = GPIO_NUM_41`

**⚠️ Migration GPIO :** Les anciens GPIO 32/33 (ESP32 Wrover) → GPIO 40/41 (BOX-3 dock PMOD1).

**PMOD1 — BMU I2C + signaux disponibles :**

| Pin PMOD1 | GPIO | Fonction BMU |
|---|---|---|
| IO1 | 42 | Libre (alerte INA optionnel) |
| IO2 | 20 | USB D+ (réservé) |
| IO3 | 39 | Libre (IR TX sur BOX-3, utilisable si IR désactivé) |
| IO4 | **40** | **I2C SCL BMU** |
| IO5 | 21 | Libre |
| IO6 | 19 | USB D- (réservé) |
| IO7 | 38 | Libre (IR RX sur BOX-3, utilisable si IR désactivé) |
| IO8 | **41** | **I2C SDA BMU** |

**PMOD2 — SD card / SPI / UART :**

| Pin PMOD2 | GPIO | Fonction BOX-3 |
|---|---|---|
| IO1 | 13 | SD D1 / SPI MISO |
| IO2 | 9 | SD D0 / SPI HD |
| IO3 | 12 | SD D3 / SPI CLK |
| IO4 | 44 | UART0 RX |
| IO5 | 10 | SPI CS |
| IO6 | 14 | SD CMD / SPI WP |
| IO7 | 11 | SD CLK / SPI MOSI |
| IO8 | 43 | UART0 TX |

**GPIOs internes occupés (ne pas utiliser) :**
- Display SPI : GPIO 4 (DC), 5 (CS), 6 (SDA), 7 (SCK), 48 (RST), 47 (backlight)
- Audio : GPIO 2 (MCLK), 17 (SCLK), 45 (LRCK), 15 (DOUT), 16 (DIN), 46 (PA)
- Touch : GPIO 3 (INT)
- Boutons : GPIO 0 (config), 1 (mute)
- USB : GPIO 19 (D-), 20 (D+)

### 4.4 I2C (remplacement Wire + I2CMutex)

```cpp
// bmu_i2c.h — bus I2C dédié BMU sur PMOD1
// SDA=GPIO41, SCL=GPIO40 (BOX-3 dock, I2C_NUM_1)
// Pull-ups 4.7kΩ externes requises (non montées sur dock)
i2c_master_bus_handle_t bmu_i2c_init(void); // GPIO40/41, 50kHz, I2C_NUM_1
i2c_master_dev_handle_t bmu_i2c_add_device(uint8_t addr);
// Le new i2c_master driver ESP-IDF v5.x est thread-safe par design.
// Plus besoin de mutex manuel — le driver gère la sérialisation.
```

**Architecture I2C double bus :**
- `I2C_NUM_0` (GPIO8/18, 400 kHz) : BOX-3 interne (écran, codec, IMU, crypto) — géré par le BSP
- `I2C_NUM_1` (GPIO40/41, 50 kHz) : BMU dédié (INA237, TCA9535) — géré par `bmu_i2c`

### 4.4 Drivers INA237 / TCA9535

Réécriture propre sur `i2c_master_transmit()` / `i2c_master_receive()`. API simplifiée :

```cpp
// bmu_ina237.h
esp_err_t ina237_init(i2c_master_bus_handle_t bus, uint8_t addr, ina237_handle_t *out);
esp_err_t ina237_read_voltage(ina237_handle_t h, float *voltage_v);
esp_err_t ina237_read_current(ina237_handle_t h, float *current_a);
esp_err_t ina237_read_all(ina237_handle_t h, float *v, float *i, float *p);
```

### 4.5 Web server (remplacement AsyncWebServer)

```cpp
// bmu_web.h — esp_http_server
esp_err_t bmu_web_start(void);
// Routes enregistrées dans bmu_web.cpp :
// GET  /              → serve SPIFFS index.html
// POST /api/switch_on  → auth + rate limit + validation + switch
// POST /api/switch_off → auth + rate limit + validation + switch
// GET  /api/batteries → JSON status (cJSON)
// WS   /ws            → WebSocket authenticated (même port 80)
```

### 4.6 Stockage

```cpp
// bmu_storage.h
esp_err_t bmu_sd_init(void);          // SDMMC + VFS mount
esp_err_t bmu_sd_log(const char *line); // fopen/fprintf/fclose
esp_err_t bmu_nvs_init(void);         // NVS pour credentials + config persistante
```

### 4.7 Tests natifs

Unity reste le framework de test. Les tests compilent en mode host (`idf.py --preview set-target linux` ou CMake natif) avec stubs pour les drivers I2C.

## 5. Plan de migration (phases)

Même si Big Bang, le travail est séquencé pour être vérifiable :

| Phase | Contenu | Critère de succès |
|---|---|---|
| 0 | Scaffold CMake + `app_main` + `esp_log` + sdkconfig | `idf.py build` OK, boot sur S3, logs visibles |
| 1 | `bmu_i2c` + `bmu_ina237` + `bmu_tca9535` | Scan I2C OK, lectures V/I correctes |
| 2 | `bmu_protection` + `bmu_battery_manager` + `bmu_config` | Protection loop fonctionnelle, LEDs OK |
| 3 | `bmu_web` + `bmu_storage` | HTTP server up, SD logging, WebSocket |
| 4 | `bmu_cloud` (MQTT + InfluxDB + SNTP) | Telemetry push OK |
| 5 | Tests host + CI + OTA + partitions finales | `idf.py test` green, OTA testée |

## 6. Risques et mitigations

| Risque | Impact | Mitigation |
|---|---|---|
| Regression protection safety | CRITIQUE | Tests L1/L2 portés en premier, validés avant chaque phase |
| Driver INA237 incompatible | HAUT | Datasheet INA237 (TI SBOS945) comme référence, pas le code Arduino |
| AsyncWebServer features manquantes | MOYEN | `esp_http_server` couvre 100% des besoins actuels (GET/POST/WS) |
| Temps de migration sous-estimé | MOYEN | Phase 0-2 = firmware minimal viable, phases 3-5 peuvent être itérées |
| PSRAM 8MB config | FAIBLE | `sdkconfig.defaults` avec `CONFIG_SPIRAM_*` vérifié au boot |

## 7. Ce qui ne change PAS

- **Logique de protection** (F01-F08) — même state machine, mêmes seuils
- **Topologie I2C** — mêmes adresses INA/TCA, même contrainte `Nb_TCA * 4 == Nb_INA`
- **Interface web** — mêmes routes, même UI (HTML/CSS/JS dans SPIFFS)
- **Format logs SD** — même CSV `;` separator
- **Specs Kill_LIFE** — S0-S2 restent valides, S3 hardware inchangé
