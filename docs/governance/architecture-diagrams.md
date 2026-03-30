# Architecture Diagrams BMU

Date: 2026-03-30
Statut: active (post-audit)

## 1. Vue bloc BMU (firmware/hardware/cloud)

```mermaid
flowchart LR
  subgraph EDGE[BMU Edge - ESP32-S3]
    MAIN[main.cpp\nOrchestration]
    BP[BatteryParallelator\nProtections locales]
    BM[BatteryManager\nComptage Ah]
    INA[INAHandler\nMesures V/I]
    TCA[TCAHandler\nSwitches batterie]
    WEB[WebServerHandler\nRoutes supervision]
    LOG[SD_Logger\nCSV ;]
    CFG[config.h\nSeuils mV/mA]
  end

  subgraph BUS[I2C Bus 50kHz]
    I2C[I2CLockGuard\nSérialisation accès]
    REC[I2CRecoveryPolicy\n5 échecs → recovery]
  end

  subgraph CLOUD[Cloud / IA]
    INFLUX[InfluxDB]
    MQTT[MQTTHandler]
    AI[Pipeline ML\nFPNN/SambaMixer]
    OPS[Ops/QA CI]
  end

  MAIN --> BP
  MAIN --> BM
  MAIN --> INA
  MAIN --> TCA
  MAIN --> WEB
  MAIN --> LOG
  CFG --> MAIN

  INA <--> I2C
  TCA <--> I2C
  I2C --> REC

  LOG --> INFLUX
  WEB --> MQTT
  MQTT --> INFLUX
  INFLUX --> AI
  AI --> OPS
```

## 2. Séquence de protection batterie (loop 500ms)

```mermaid
sequenceDiagram
  participant Main as main.cpp loop
  participant INA as INAHandler
  participant BP as BatteryParallelator
  participant TCA as TCAHandler
  participant Log as KxLogger

  loop Toutes les 500ms
    Main->>BP: check_battery_connected_status(i)
    BP->>INA: read_volt(i) + read_current(i)
    INA-->>BP: voltage_V, current_A

    alt NAN (I2C error)
      BP->>Log: WARNING skip protection
    else voltage < 0 OU |current| > 2x max
      BP->>TCA: switch OFF (ERROR)
      BP->>Log: CRITICAL error
    else !check_battery_status OU !check_voltage_offset
      BP->>TCA: switch OFF (DISCONNECTED)
      BP->>Log: disconnect
    else nb_switch > nbSwitchMax
      BP->>TCA: switch OFF (LOCKED)
      BP->>Log: permanent lock
    else nb_switch < max ET delay OK
      BP->>BP: is_voltage_within_range + is_current_within_range
      BP->>TCA: switch ON (RECONNECTING)
      BP->>Log: reconnect
    else
      BP->>Log: CONNECTED (nominal)
    end
  end
```

## 3. State machine batterie

```mermaid
stateDiagram-v2
  [*] --> DISCONNECTED: boot
  DISCONNECTED --> RECONNECTING: conditions OK\n+ delay respecté
  RECONNECTING --> CONNECTED: switch ON réussi
  CONNECTED --> DISCONNECTED: seuil V/I dépassé
  CONNECTED --> ERROR: surcourant critique\n(|I| > 2x max)
  DISCONNECTED --> ERROR: voltage < 0
  ERROR --> DISCONNECTED: après switch OFF
  RECONNECTING --> LOCKED: nb_switch > max
  LOCKED --> [*]: reboot requis
```

## 4. Architecture I2C et topologie

```mermaid
flowchart TB
  ESP[ESP32-S3\nGPIO32=SDA GPIO33=SCL]
  MUX[I2C Bus 50kHz]
  ESP --> MUX

  subgraph TCA_GRP[TCA9535 0x20-0x27]
    TCA0[TCA_0 0x20\nPins 0-3: switches\nPins 8-15: LEDs]
    TCA1[TCA_1 0x21]
    TCA2[TCA_2 0x22]
    TCA3[TCA_3 0x23]
  end

  subgraph INA_GRP[INA237 0x40-0x4F]
    INA0[INA 0x40-0x43\n4 batteries]
    INA1[INA 0x44-0x47\n4 batteries]
    INA2[INA 0x48-0x4B\n4 batteries]
    INA3[INA 0x4C-0x4F\n4 batteries]
  end

  MUX --> TCA0
  MUX --> TCA1
  MUX --> TCA2
  MUX --> TCA3
  MUX --> INA0
  MUX --> INA1
  MUX --> INA2
  MUX --> INA3

  TCA0 -.-> INA0
  TCA1 -.-> INA1
  TCA2 -.-> INA2
  TCA3 -.-> INA3
```

**Contrainte topologie:** `Nb_TCA * 4 == Nb_INA` — sinon fail-safe (toutes batteries OFF).

## 5. Architecture web et sécurité

```mermaid
flowchart LR
  CLIENT[Navigateur / WebSocket]
  HTTP[AsyncWebServer :80]
  WS[WebSocketsServer :81]

  subgraph AUTH[Chaîne de sécurité]
    RL[Rate Limiter\n10 req/10s par IP]
    TK[Token Auth\nBMU_WEB_ADMIN_TOKEN]
    VAL[BatteryRouteValidation\nIndex + voltage check]
  end

  subgraph ACTIONS[Actions batterie]
    ON[switch_on]
    OFF[switch_off]
    STATUS[Telemetry JSON]
  end

  CLIENT -->|GET /switch_on| HTTP
  HTTP --> RL --> TK --> VAL --> ON
  CLIENT -->|GET /switch_off| HTTP
  HTTP --> RL --> TK --> OFF
  CLIENT -->|WStype_TEXT| WS -->|Non authentifié| STATUS
```

**Audit CRIT-D:** Routes GET doivent migrer vers POST. WebSocket :81 non authentifié. Token par défaut vide.

## 6. Pipeline ML (optionnel V2)

```mermaid
flowchart LR
  SD[SD Card CSV\nhardware/log-sd/]
  PARSE[parse_csv.py]
  FEAT[extract_features.py]
  TRAIN[train_fpnn.py\nSOH capacity mode]
  QUANT[quantize_tflite.py\nINT8 QDQ]
  EDGE[ESP32-S3\n< 50KB model]

  SD --> PARSE --> FEAT --> TRAIN --> QUANT --> EDGE

  TRAIN2[train_sambamixer.py\nRUL cloud]
  FEAT --> TRAIN2
  TRAIN2 --> CLOUD[InfluxDB Cloud]
```

## 7. Rappels de sûreté

- Les protections critiques restent locales sur MCU — ML reste consultatif.
- Toute opération INA/TCA passe via I2CLockGuard (jamais de double-lock).
- `stateMutex` protège les tableaux partagés (battery_voltages, Nb_switch, reconnect_time).
- Le fail-safe topologie force OFF si `Nb_TCA * 4 != Nb_INA`.
