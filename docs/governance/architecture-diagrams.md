# Architecture Diagrams BMU

Date: 2026-03-31
Statut: active (post-migration ESP-IDF v5.4)

## 1. Vue bloc complete (firmware ESP-IDF / hardware / cloud)

```mermaid
flowchart LR
  subgraph BOX3[ESP32-S3-BOX-3]
    subgraph FW[Firmware ESP-IDF v5.4]
      MAIN[main.cpp\nOrchestration]
      PROT[bmu_protection\nState machine F01-F08]
      BMGR[bmu_battery_manager\nAh tracking]
      INA[bmu_ina237\nCapteurs V/I/P/T]
      TCA[bmu_tca9535\nSwitches + LEDs]
      CFG[bmu_config\nKconfig seuils]
      WEB[bmu_web\nHTTP + WebSocket]
      WIFI[bmu_wifi\nSTA auto-reconnect]
      STOR[bmu_storage\nSD + NVS]
      DISP[bmu_display\nLVGL v9 tabview]
      OTA[bmu_ota\nA/B rollback]
      VEDR[bmu_vedirect\nVictron MPPT UART]
    end
    subgraph HW[Hardware interne BOX-3]
      LCD[ILI9342C 320x240]
      TOUCH[TT21100 touch]
      AUDIO[ES8311 codec]
      IMU[ICM-42607 IMU]
    end
  end

  subgraph I2C_BUS[I2C Bus]
    BUS0[I2C_NUM_0\nGPIO8/18 400kHz\nBSP interne]
    BUS1[I2C_NUM_1\nGPIO40/41 50kHz\nBMU PMOD1]
  end

  subgraph CLOUD[Cloud / kxkm-ai]
    MQTT_B[MQTT Broker]
    INFLUX[InfluxDB v2]
    GRAF[Grafana]
    ML[Pipeline ML\nFPNN/SambaMixer]
  end

  subgraph SOLAR[Victron MPPT]
    MPPT[Chargeur solaire\nUART 19200]
  end

  MAIN --> PROT & BMGR & WEB & DISP & VEDR
  PROT --> INA & TCA
  INA <--> BUS1
  TCA <--> BUS1
  LCD <--> BUS0
  TOUCH <--> BUS0
  WIFI --> MQTT_B & INFLUX
  MQTT_B --> GRAF
  INFLUX --> ML
  MPPT --> VEDR
  DISP --> LCD
```

## 2. Sequence protection batterie (boucle 500ms)

```mermaid
sequenceDiagram
  participant Main as main.cpp
  participant INA as bmu_ina237
  participant Prot as bmu_protection
  participant TCA as bmu_tca9535
  participant Disp as bmu_display

  loop Toutes les 500ms
    Main->>Prot: check_battery(i)
    Prot->>INA: read_voltage_current()
    INA-->>Prot: v_mv, i_a

    alt NAN / I2C error
      Prot-->>Main: skip
    else |I| > 2x max
      Prot->>TCA: switch OFF (ERROR)
    else nb_switch > max
      Prot->>TCA: switch OFF (LOCKED)
      Prot-->>Main: return early
    else V/I hors range ou imbalance
      Prot->>TCA: switch OFF (DISCONNECTED)
    else conditions OK + delay
      Prot->>TCA: switch ON (RECONNECTING)
    else nominal
      Note over Prot: CONNECTED
    end

    Disp->>Prot: get_state(i) get_voltage(i)
    Disp-->>Disp: update UI
  end
```

## 3. State machine batterie (5 etats)

```mermaid
stateDiagram-v2
  [*] --> DISCONNECTED: boot
  DISCONNECTED --> RECONNECTING: V/I OK + delay
  RECONNECTING --> CONNECTED: switch ON OK
  CONNECTED --> DISCONNECTED: seuil depasse
  CONNECTED --> ERROR: |I| > 2x max
  DISCONNECTED --> ERROR: V < 0
  ERROR --> DISCONNECTED: switch OFF
  DISCONNECTED --> LOCKED: nb_switch > max
  LOCKED --> [*]: reboot requis
```

## 4. Architecture I2C double bus BOX-3

```mermaid
flowchart TB
  ESP[ESP32-S3-BOX-3]

  subgraph BUS0[I2C_NUM_0 — BSP interne 400kHz]
    LCD_C[ILI9342C display]
    TOUCH_C[TT21100 touch]
    CODEC[ES8311 audio]
    IMU_C[ICM-42607 IMU]
    CRYPTO[ATECC608A crypto]
  end

  subgraph BUS1[I2C_NUM_1 — BMU PMOD1 50kHz]
    direction TB
    TCA0[TCA_0 0x20 → 4 batteries]
    TCA1[TCA_1 0x21 → 4 batteries]
    TCA2[TCA_2 0x22 → 4 batteries]
    TCA3[TCA_3 0x23 → 4 batteries]
    INA_GRP[INA237 0x40-0x4F → 16 capteurs]
  end

  ESP -->|GPIO8 SDA / GPIO18 SCL| BUS0
  ESP -->|GPIO41 SDA / GPIO40 SCL| BUS1
```

## 5. Architecture web + securite

```mermaid
flowchart LR
  CLIENT[Navigateur]
  WS_C[WebSocket /ws]
  HTTP[esp_http_server :80]

  subgraph AUTH[Chaine securite]
    RL[Rate Limiter\n16 slots LRU]
    TK[Token Auth\nconstant-time]
    VAL[Validation\nbounds + voltage]
  end

  subgraph ACT[Actions]
    PROT_SW[bmu_protection_web_switch]
    API[/api/batteries JSON]
    SPIFFS[SPIFFS index.html]
  end

  CLIENT -->|POST /api/battery/switch_*| HTTP
  HTTP --> RL --> TK --> VAL --> PROT_SW
  CLIENT -->|GET /api/batteries| HTTP --> API
  CLIENT -->|GET /| HTTP --> SPIFFS
  WS_C -->|token first msg| HTTP
```

## 6. Pipeline cloud telemetrie

```mermaid
flowchart LR
  PROT[bmu_protection] --> CLOUD_TASK[cloud_telemetry_task\nFreeRTOS 10s]

  CLOUD_TASK --> MQTT[bmu_mqtt\nesp_mqtt]
  CLOUD_TASK --> INFLUX[bmu_influx\nHTTP POST]

  MQTT --> BROKER[MQTT Broker\nkxkm-ai:1883]
  INFLUX --> INFLUXDB[InfluxDB v2\nkxkm-ai:8086]

  BROKER --> GRAFANA[Grafana dashboards]
  INFLUXDB --> GRAFANA
  INFLUXDB --> ML[Pipeline ML\nFPNN SOH/RUL]

  SNTP[bmu_sntp\npool.ntp.org] --> INFLUX
```

## 7. Display LVGL tabview (5 onglets planifies)

```mermaid
flowchart TB
  TABVIEW[lv_tabview — bas de l'ecran]

  TAB1["⚡ Batt\nGrille 4x4"]
  TAB2["⚙ Sys\nFirmware/heap/WiFi"]
  TAB3["⚠ Alert\nHistorique 20 entries"]
  TAB4["👁 I2C\nDebug bus log"]
  TAB5["☀ Solar\nVE.Direct MPPT"]

  TABVIEW --> TAB1 & TAB2 & TAB3 & TAB4 & TAB5

  TAB1 -->|tap cellule| DETAIL["Detail batterie\nChart V/I + boutons"]
  DETAIL -->|bouton ←| TAB1
```

## 8. Architecture BLE planifiee (Phase 9)

```mermaid
flowchart LR
  PHONE[Smartphone]
  WEB_BLE[Web Bluetooth\nChrome]

  subgraph GATT[NimBLE GATT Services]
    SVC_BAT[Battery Service\n16 chars READ+NOTIFY]
    SVC_SYS[System Service\nfirmware/heap/WiFi]
    SVC_CTL[Control Service\nswitch/reset/config\nPAIRING REQUIS]
  end

  PHONE -->|BLE| SVC_BAT
  PHONE -->|BLE + pairing| SVC_CTL
  WEB_BLE -->|Web Bluetooth API| SVC_BAT & SVC_SYS
  SVC_CTL --> PROT[bmu_protection_web_switch]
```

## 9. Rappels de surete

- Protections critiques restent locales sur MCU — ML reste consultatif
- Toute operation I2C multi-registre : `bmu_i2c_lock()`/`bmu_i2c_unlock()`
- `stateMutex` protege les tableaux partages
- Fail-safe topologie force OFF si `Nb_TCA * 4 != Nb_INA`
- Web + BLE mutations passent par `bmu_protection_web_switch()` — jamais direct TCA
- OTA avec rollback : `bmu_ota_mark_valid()` appele au boot
