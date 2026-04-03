# Intégration Victron — SmartSolar + VRM + BLE

**Date:** 2026-04-02
**Branche:** `victron`
**Hardware:** ESP32-S3-BOX-3 + SmartSolar MPPT 150/35 (VE.Direct)
**VRM Portal ID:** `70b3d549969af37b` — Site ID: `30322`

## Objectif

Intégration complète du BMU dans l'écosystème Victron Energy :
1. Lecture SmartSolar 150/35 via VE.Direct (données solaires partout)
2. Publication sur Victron VRM (BOX-3 = mini-GX)
3. BMU visible dans Victron Connect via BLE Instant Readout

## Architecture

```
SmartSolar 150/35
    │ VE.Direct (UART2, GPIO39 RX / GPIO38 TX, 19200 baud)
    ▼
┌─────────────────────────────────────────────────┐
│                ESP32-S3 BOX-3                    │
│                                                  │
│  bmu_vedirect ──→ bmu_vedirect_data_t            │
│       │                                          │
│       ├──→ bmu_display (écran SYS section Solar) │
│       ├──→ MQTT KXKM (bmu/{device}/solar)        │
│       ├──→ InfluxDB (measurement solar)           │
│       ├──→ bmu_vrm (mqtt.victronenergy.com)      │
│       └──→ bmu_ble (characteristic Solar exist.)  │
│                                                  │
│  bmu_ble_victron ──→ BLE advertising             │
│       │    Instant Readout Battery Monitor        │
│       │    + Solar Charger data relay             │
│       └──→ Visible dans Victron Connect           │
└─────────────────────────────────────────────────┘
         │              │
         ▼              ▼
    Victron VRM    Victron Connect
    (cloud)        (smartphone BLE)
```

### Composants

| Composant | Action | Rôle |
|-----------|--------|------|
| `bmu_vedirect` | Modifier | Changer GPIOs (39/38), UART2, activer |
| `bmu_vrm` | Créer | Client MQTT TLS vers VRM |
| `bmu_ble_victron` | Créer | Advertising BLE format Victron |
| `cloud_telemetry_task` | Modifier | Ajouter publication solar MQTT/InfluxDB |

## Couche 1 — Data (VE.Direct + telemetry solaire)

### Activation VE.Direct

Modifications Kconfig et sdkconfig :
- `CONFIG_BMU_VEDIRECT_ENABLED=y`
- `CONFIG_BMU_VEDIRECT_UART_NUM=2` (pas UART0 = console)
- `CONFIG_BMU_VEDIRECT_RX_GPIO=39`
- `CONFIG_BMU_VEDIRECT_TX_GPIO=38`
- `CONFIG_BMU_VEDIRECT_BAUD=19200`

Pas de modification au parser TEXT — il couvre déjà tous les champs SmartSolar 150/35 : V, I, VPV, PPV, CS, MPPT, ERR, H19, H20, H21, PID, SER#, FW, LOAD.

### Telemetry solaire

Ajouter dans `cloud_telemetry_task` (main.cpp), après la boucle batteries :

**MQTT** — topic `bmu/{device_name}/solar` :
```json
{
  "vpv": 45.2,
  "ppv": 340,
  "vbat": 27.1,
  "ibat": 12.3,
  "cs": "Float",
  "yield_today": 1250,
  "err": 0
}
```

**InfluxDB** — measurement `solar` :
```
solar,device=kryole vpv=45.2,ppv=340,vbat=27.1,ibat=12.3,cs=5i,yield_today=1250i
```

Publication conditionnelle : seulement si `bmu_vedirect_is_connected()`.

### Pas de nouveau composant

~20 lignes ajoutées dans main.cpp + sdkconfig changes.

## Couche 2 — Cloud (VRM MQTT)

### Protocole VRM

Broker : `mqtt.victronenergy.com`, port 8883 (TLS) ou 1883 (plain)
Client ID : `<portal_id>` = `70b3d549969af37b`
Keepalive : publish `R/<portal_id>/keepalive` toutes les 30s

### Topics publiés

**Solar Charger (instance 0)** — relay des données SmartSolar :
```
N/<id>/solarcharger/0/Pv/V          → {"value": 45.2}
N/<id>/solarcharger/0/Pv/P          → {"value": 340}
N/<id>/solarcharger/0/Dc/0/Voltage  → {"value": 27.1}
N/<id>/solarcharger/0/Dc/0/Current  → {"value": 12.3}
N/<id>/solarcharger/0/State         → {"value": 5}
N/<id>/solarcharger/0/Yield/User    → {"value": 1.25}
N/<id>/solarcharger/0/ErrorCode     → {"value": 0}
N/<id>/solarcharger/0/ProductId     → {"value": "SmartSolar 150/35"}
N/<id>/solarcharger/0/Serial        → {"value": "<from VE.Direct>"}
```

**Battery Monitor (instance 0)** — données BMU agrégées :
```
N/<id>/battery/0/Dc/0/Voltage       → {"value": 26.8}
N/<id>/battery/0/Dc/0/Current       → {"value": 4.1}
N/<id>/battery/0/Soc                → {"value": 85}
N/<id>/battery/0/ConsumedAmphours   → {"value": 12.3}
```

**System :**
```
N/<id>/system/0/Serial              → {"value": "70b3d549969af37b"}
```

### Composant `bmu_vrm`

**Fichiers :**
- `firmware-idf/components/bmu_vrm/bmu_vrm.cpp`
- `firmware-idf/components/bmu_vrm/include/bmu_vrm.h`
- `firmware-idf/components/bmu_vrm/Kconfig`
- `firmware-idf/components/bmu_vrm/CMakeLists.txt`

**API :**
```c
esp_err_t bmu_vrm_init(bmu_protection_ctx_t *prot,
                       bmu_battery_manager_t *mgr,
                       uint8_t nb_ina);
void bmu_vrm_set_vedirect(const bmu_vedirect_data_t *solar);
bool bmu_vrm_is_connected(void);
```

**Config Kconfig :**
- `CONFIG_BMU_VRM_ENABLED` (bool, default n)
- `CONFIG_BMU_VRM_PORTAL_ID` (string, default "70b3d549969af37b")
- `CONFIG_BMU_VRM_USE_TLS` (bool, default y)
- `CONFIG_BMU_VRM_PUBLISH_INTERVAL_S` (int, default 30)

**Config NVS (runtime override) :**
- `vrm_id` — portal ID
- `vrm_enabled` — bool

**Implémentation :**
- Client MQTT séparé de `bmu_mqtt` (broker et auth différents)
- FreeRTOS task dédiée, stack 4096, priorité 2
- Publish toutes les 30s + keepalive
- TLS via mbedtls (ESP-IDF intégré, ~40KB heap)
- Reconnexion automatique

**Dépendances CMake :**
```
REQUIRES bmu_protection bmu_config bmu_vedirect
PRIV_REQUIRES mqtt esp-tls
```

### SOC estimation

VRM attend un SOC (State of Charge). Estimation simple basée sur la tension moyenne :
```c
/* Mapping linéaire 24V=0%, 28.8V=100% (LiFePO4 24V 8S) */
float soc = (avg_voltage_v - 24.0f) / 4.8f * 100.0f;
if (soc < 0) soc = 0;
if (soc > 100) soc = 100;
```

Note : ce mapping est approximatif. Paramétrable via Kconfig (v_soc_0 et v_soc_100).

## Couche 3 — BLE Victron Instant Readout

### Protocole BLE Victron

Les devices Victron émettent des **advertising packets** (pas de connexion GATT) contenant des Manufacturer Specific Data chiffrées. Victron Connect scanne passivement et déchiffre avec la clé utilisateur.

### Format advertising

**Header Manufacturer Specific Data :**
```
Company ID:    0x02E1 (Victron Energy, little-endian: 0xE1, 0x02)
Record type:   uint8 (0x01=Solar, 0x02=Battery Monitor)
Nonce/counter: uint16 (rolling, pour AES-CTR IV)
Encrypted:     N bytes (payload chiffré AES-CTR-128)
```

### Clé de chiffrement

AES-128-CTR, clé 16 bytes. Configurable :
- Kconfig : `CONFIG_BMU_VICTRON_BLE_KEY` (32 hex chars)
- NVS : `vrm_ble_key` (runtime override)
- Affichée dans l'écran CONFIG du BOX-3 pour saisie dans Victron Connect

### Payload Battery Monitor (record type 0x02, ~12 bytes avant chiffrement)

| Champ | Type | Unité | Source BMU |
|-------|------|-------|------------|
| Remaining Ah | uint16 LE | 0.1 Ah | Ah charge total |
| Voltage | uint16 LE | 0.01 V | Tension moyenne batteries |
| Current | int16 LE | 0.1 A | Courant total |
| SOC | uint16 LE | 0.1 % | Estimation tension |
| Consumed Ah | uint16 LE | 0.1 Ah | Ah discharge total |

### Payload Solar Charger (record type 0x01, ~10 bytes avant chiffrement)

| Champ | Type | Unité | Source |
|-------|------|-------|--------|
| State | uint8 | CS enum | bmu_vedirect CS |
| Error | uint8 | ERR enum | bmu_vedirect ERR |
| Yield today | uint16 LE | 0.01 kWh | bmu_vedirect H20 |
| PV Power | uint16 LE | W | bmu_vedirect PPV |
| Battery Current | int16 LE | 0.1 A | bmu_vedirect I |
| Battery Voltage | uint16 LE | 0.01 V | bmu_vedirect V |

### Stratégie advertising

Advertising alterné — NimBLE permet de changer les données d'advertising à chaud :
1. **Slot KXKM** (500ms) — advertising KXKM-BMU existant (app iOS)
2. **Slot Victron Battery Monitor** (500ms) — record type 0x02
3. **Slot Victron Solar Charger** (500ms) — record type 0x01

Timer 500ms qui fait tourner les 3 slots via `ble_gap_adv_set_data()`.
Victron Connect scanne passivement et capte les records quand ils passent.

### Composant `bmu_ble_victron`

**Fichiers :**
- `firmware-idf/components/bmu_ble_victron/bmu_ble_victron.cpp`
- `firmware-idf/components/bmu_ble_victron/include/bmu_ble_victron.h`
- `firmware-idf/components/bmu_ble_victron/Kconfig`
- `firmware-idf/components/bmu_ble_victron/CMakeLists.txt`

**API :**
```c
esp_err_t bmu_ble_victron_init(bmu_protection_ctx_t *prot,
                               bmu_battery_manager_t *mgr,
                               uint8_t nb_ina);
void bmu_ble_victron_update_solar(const bmu_vedirect_data_t *solar);
```

**Config Kconfig :**
- `CONFIG_BMU_VICTRON_BLE_ENABLED` (bool, default n)
- `CONFIG_BMU_VICTRON_BLE_KEY` (string, 32 hex chars)
- `CONFIG_BMU_VICTRON_ADV_INTERVAL_MS` (int, default 500)

**Dépendances CMake :**
```
REQUIRES bmu_protection bmu_config bmu_vedirect bt
PRIV_REQUIRES mbedtls
```

### AES-CTR sur ESP32-S3

ESP32-S3 a un accélérateur hardware AES. Utiliser `mbedtls_aes_crypt_ctr()` — pas de surcoût CPU notable.

```c
mbedtls_aes_context aes;
mbedtls_aes_setkey_enc(&aes, key, 128);
uint8_t nonce_counter[16] = {0};
/* Set nonce from rolling counter */
nonce_counter[0] = (uint8_t)(counter & 0xFF);
nonce_counter[1] = (uint8_t)(counter >> 8);
size_t nc_off = 0;
uint8_t stream_block[16];
mbedtls_aes_crypt_ctr(&aes, payload_len, &nc_off,
                       nonce_counter, stream_block,
                       plaintext, ciphertext);
```

## Configuration UI

### Écran CONFIG du BOX-3

Ajouter une section "VICTRON" dans `bmu_ui_config.cpp` :
- VRM Portal ID : textarea (pré-rempli depuis Kconfig/NVS)
- VRM Enabled : switch toggle
- BLE Key : textarea (32 hex chars, affiché pour copier dans Victron Connect)
- BLE Victron : switch toggle

### Config NVS (ajouts à bmu_config)

| Clé NVS | Type | Défaut |
|---------|------|--------|
| `vrm_id` | string | `70b3d549969af37b` |
| `vrm_enabled` | bool | false |
| `vrm_ble_key` | string (hex) | random initial |
| `vrm_ble_enabled` | bool | false |

## Boot sequence (ajouts)

Dans main.cpp, après l'init MQTT :

```cpp
/* VRM */
if (bmu_wifi_is_connected()) {
    bmu_vrm_init(&prot, &mgr, nb_ina);
}

/* BLE Victron advertising */
#ifdef CONFIG_BMU_VICTRON_BLE_ENABLED
bmu_ble_victron_init(&prot, &mgr, nb_ina);
#endif
```

## Risques et mitigations

| Risque | Impact | Mitigation |
|--------|--------|------------|
| Format BLE Victron change | Victron Connect ne lit plus | Records versionnés, mise à jour OTA |
| TLS VRM +40KB heap | OOM au boot | Monitoring heap, init conditionnel |
| Portal ID non enregistré | VRM ignore les données | Log warning, pas de crash |
| Advertising alterné timing | Victron Connect rate des packets | Ajuster interval, tester empiriquement |
| AES key management | Utilisateur doit entrer la clé manuellement | Afficher clairement sur l'écran CONFIG |

## Références

- [victron-ble](https://github.com/keshavdv/victron-ble) — Reverse-engineering BLE Victron
- [Venus OS](https://github.com/victronenergy/venus) — Format MQTT VRM
- [VE.Direct Protocol FAQ](https://www.victronenergy.com/support-and-downloads/technical-information) — TEXT protocol
- Victron Company ID BLE : `0x02E1`

## Hors scope

- VE.Direct HEX protocol (pas nécessaire pour le SmartSolar TEXT)
- Connexion GATT Victron (seul l'advertising Instant Readout est implémenté)
- Multi-SmartSolar (un seul par BOX-3)
- GX device emulation complète (on ne publie que solar + battery, pas inverter/charger/etc.)
