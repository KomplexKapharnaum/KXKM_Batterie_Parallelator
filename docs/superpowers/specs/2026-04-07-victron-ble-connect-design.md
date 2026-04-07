# Victron BLE Connect — Design Spec

## Goal

Faire apparaître le BMU comme un SmartShunt dans VictronConnect (lecture seule) et scanner les appareils Victron à portée BLE pour récupérer leurs données sans câble VE.Direct.

## Scope

- **(a)** Émulation GATT SmartShunt : VictronConnect voit le BMU dans sa liste d'appareils, affiche V, I, SOC, Ah, T°C, alarmes.
- **(c/d)** Scanner BLE central : détecte tout appareil Victron (MPPT, SmartShunt, inverter, DC-DC), décrypte les pubs Instant Readout avec clé AES par appareil.
- **(b)** Configuration depuis VictronConnect : **hors scope initial**, planifié pour plus tard.

## Existing State

- `bmu_ble` : NimBLE stack (GATT peripheral + advertising), 3 services KXKM (battery, system, control).
- `bmu_ble_victron` : Instant Readout advertising (AES-CTR, company ID 0x02E1), pubs batterie + solaire.
- `bmu_vrm` : MQTT vers Victron VRM cloud (séparé, inchangé).
- NimBLE ESP32-S3 supporte central + peripheral simultané.

---

## Part 1 : Émulation SmartShunt GATT

### Service GATT

Service UUID basé sur le namespace Victron (reverse-engineered, communauté open-source).

| Caractéristique | UUID suffix | Type | Données | Unité |
|----------------|-------------|------|---------|-------|
| Battery Voltage | 0x0011 | READ+NOTIFY | V pack moyen | 0.01V uint16 |
| Battery Current | 0x0012 | READ+NOTIFY | I total | 0.1A int16 signé |
| State of Charge | 0x0013 | READ+NOTIFY | SOC estimé | 0-10000 (0-100.00%) uint16 |
| Consumed Ah | 0x0014 | READ+NOTIFY | Ah déchargés total | 0.1Ah int32 |
| Time to Go | 0x0015 | READ+NOTIFY | Estimation TTG | minutes uint16 (0xFFFF = infini) |
| Temperature | 0x0016 | READ+NOTIFY | AHT30 | 0.01°C int16 (offset +27315 = Kelvin) |
| Alarm | 0x0017 | READ+NOTIFY | Bitmask alarmes | uint16 (bit0=lowV, bit1=highV, bit2=overI) |
| Model Name | 0x0020 | READ | "KXKM BMU" | string |
| Serial Number | 0x0021 | READ | Device name (NVS) | string |

### Notification Rate

Toutes les 1s, même timer que le service BLE KXKM existant.

### SOC Estimation

Mapping linéaire tension : `SOC = (V_avg - V_min) / (V_max - V_min) * 100%`. Pas de coulomb-counting SOC (suffisant pour lecture seule). V_min/V_max depuis les seuils de protection configurés.

### TTG Estimation

`TTG = remaining_Ah / abs(I_total)` en minutes. Si I_total >= 0 (charge) → TTG = 0xFFFF (infini).

### Alarm Bitmask

| Bit | Condition |
|-----|-----------|
| 0 | V_avg < V_min configuré |
| 1 | V_avg > V_max configuré |
| 2 | abs(I_total) > I_max configuré |
| 3 | T°C > 60°C (si AHT30 dispo) |
| 4 | Au moins une batterie en ERROR |
| 5 | Au moins une batterie LOCKED |

### Composant

`firmware-idf/components/bmu_ble_victron_gatt/`
- `include/bmu_ble_victron_gatt.h` : `bmu_ble_victron_gatt_init()`, `bmu_ble_victron_gatt_register_services()`
- `bmu_ble_victron_gatt.cpp` : service GATT, timer notification 1s
- `Kconfig` : `CONFIG_BMU_VICTRON_GATT_ENABLED` (default y si BLE enabled)
- `CMakeLists.txt` : dépend de `bmu_ble`, `bmu_protection`, `bmu_battery_manager`, `bmu_climate`, `bmu_config`

### Intégration

Dans `bmu_ble.cpp`, après l'enregistrement des services KXKM :
```c
#ifdef CONFIG_BMU_VICTRON_GATT_ENABLED
    bmu_ble_victron_gatt_register_services();
#endif
```

---

## Part 2 : Scanner BLE central

### Cycle de scan

- Durée : 5 secondes (configurable `CONFIG_BMU_VIC_SCAN_DURATION_S`)
- Période : toutes les 30 secondes (configurable `CONFIG_BMU_VIC_SCAN_PERIOD_S`)
- Filtre : company ID 0x02E1 dans les MSD
- Le BMU continue d'advertising pendant le scan (NimBLE dual mode)

### Stockage clés AES (NVS)

- Namespace NVS : `vic_keys`
- Clé NVS = adresse MAC en hex majuscules, 12 chars (`A4C138F2B301`)
- Valeur = 32 chars hex (clé AES-128)
- Max 8 appareils configurés
- API config :
  - `bmu_config_set_victron_device_key(const uint8_t mac[6], const char *hex_key)`
  - `bmu_config_get_victron_device_key(const uint8_t mac[6], char *hex_key, size_t len)`
  - `bmu_config_set_victron_device_label(const uint8_t mac[6], const char *label)`
  - `bmu_config_get_victron_device_label(const uint8_t mac[6], char *label, size_t len)`
  - `bmu_config_list_victron_devices(uint8_t macs[][6], int max, int *count)`

### Décryptage

Même algo que `bmu_ble_victron` (AES-128-CTR, mbedtls). Le nonce est dans les bytes 3-4 du MSD (counter).

### Records décodés

| Record type | Appareil | Champs décodés |
|-------------|----------|----------------|
| 0x01 | Solar charger (MPPT) | charge_state, error_code, yield_today_wh, panel_power_w, battery_current_a, battery_voltage_v |
| 0x02 | Battery monitor (SmartShunt) | remaining_ah, voltage_v, current_a, soc_pct, consumed_ah |
| 0x03 | Inverter | ac_voltage_v, ac_current_a, state |
| 0x04 | DC-DC charger | input_voltage_v, output_voltage_v, state |

### Cache mémoire

```c
#define BMU_VIC_SCAN_MAX_DEVICES 8

typedef struct {
    uint8_t  mac[6];
    uint8_t  record_type;        // 0x01-0x04
    uint8_t  raw_decrypted[10];  // payload brut déchiffré
    int64_t  last_seen_ms;       // timestamp dernier scan
    bool     key_configured;     // true si clé AES dans NVS
    bool     decrypted;          // true si déchiffrement réussi
    char     label[16];          // nom user ("MPPT 1")
    // Parsed fields (union par record_type)
    union {
        struct { uint8_t cs; uint8_t err; uint16_t yield_wh; uint16_t ppv_w; int16_t ibat_da; uint16_t vbat_cv; } solar;
        struct { uint16_t rem_ah_dah; uint16_t v_cv; int16_t i_da; uint16_t soc_pm; uint16_t cons_dah; } battery;
        struct { uint16_t vac_cv; uint16_t iac_da; uint8_t state; } inverter;
        struct { uint16_t vin_cv; uint16_t vout_cv; uint8_t state; } dcdc;
    };
} bmu_vic_device_t;
```

Expiration : 5 minutes sans pub → `last_seen_ms` dépassé → marqué offline dans l'UI.

### Composant

`firmware-idf/components/bmu_ble_victron_scan/`
- `include/bmu_ble_victron_scan.h` : API publique
- `bmu_ble_victron_scan.cpp` : scanner, décrypteur, cache
- `Kconfig` : enable, scan duration, scan period, max devices
- `CMakeLists.txt` : dépend de `bmu_ble`, `bmu_config`, `bmu_mqtt`, `bmu_influx`

### API publique

```c
esp_err_t bmu_vic_scan_init(void);
esp_err_t bmu_vic_scan_start(void);    // démarre le cycle périodique
void      bmu_vic_scan_stop(void);
int       bmu_vic_scan_get_devices(bmu_vic_device_t *out, int max);
const bmu_vic_device_t *bmu_vic_scan_get_device(const uint8_t mac[6]);
int       bmu_vic_scan_count(void);
```

### Telemetry

- MQTT : `bmu/{device_name}/victron/{record_type}` avec payload JSON
  - Solar : `{"vpv":35.2,"ppv":350,"vbat":27.5,"ibat":12.3,"cs":"Bulk","yield":1250}`
  - Battery : `{"v":27.2,"i":-3.2,"soc":98,"rem_ah":95.5,"cons_ah":4.5}`
- InfluxDB : measurement `victron`, tags `device={name}`, `mac={MAC}`, `type={solar|battery|inverter|dcdc}`

---

## Part 3 : Affichage

### Écran LCD — Onglet Système

Nouvelle section "Victron" dans `bmu_ui_system.cpp`, après les infos WiFi/BLE :

```
── Victron ──────────────────────
MPPT 1   28.5V  350W  Bulk    ●
Shunt    27.2V -3.2A   98%   ●
A4:C1:38  (locked)            ○
```

- Point vert = en ligne (vu dans les 5 min), gris = offline
- Appareils sans clé : affiche MAC tronquée + "(locked)"
- Max 4 lignes visibles, scroll si plus

### Écran LCD — Onglet Config

Section "Appareils Victron" dans `bmu_ui_config.cpp` :
- Liste des appareils détectés
- Tap → saisie clé AES (clavier LVGL, 32 chars hex)
- Tap long → éditer label (16 chars)
- Bouton "Supprimer" pour retirer une clé

### App iOS

- `SystemView.swift` : section "Appareils Victron" (même layout que l'écran LCD)
- `ConfigView.swift` : sous-vue "Victron Devices" pour gérer les clés
- Données via BLE GATT (ajout d'une caractéristique "Victron scan results" au service System KXKM) ou via MQTT cloud

---

## Kconfig résumé

```kconfig
# bmu_ble_victron_gatt
CONFIG_BMU_VICTRON_GATT_ENABLED  (bool, default y)

# bmu_ble_victron_scan
CONFIG_BMU_VIC_SCAN_ENABLED      (bool, default y)
CONFIG_BMU_VIC_SCAN_DURATION_S   (int, default 5, range 2-10)
CONFIG_BMU_VIC_SCAN_PERIOD_S     (int, default 30, range 10-300)
CONFIG_BMU_VIC_SCAN_MAX_DEVICES  (int, default 8, range 1-16)
```

---

## Tests

| Suite | Contenu |
|-------|---------|
| `test_victron_gatt` | Encoding des 9 caractéristiques, alarm bitmask, SOC mapping, TTG calcul |
| `test_victron_scan` | Décryption payload (réutilise les vecteurs de test_ble_victron), cache expiration, MAC key lookup |

---

## Sécurité

- Les clés AES sont stockées dans NVS (non chiffré par défaut — même risque que les credentials WiFi/MQTT)
- Le scanner ne se connecte jamais aux appareils Victron — lecture passive des pubs uniquement
- Le service GATT SmartShunt est en lecture seule — aucune commande d'écriture
- Aucun impact sur la boucle de protection (scan en tâche séparée, priorité 1)

## Impact mémoire

- RAM : ~12 KB (8 devices × 1 KB + NimBLE central overhead)
- Flash : ~15 KB (code AES déjà inclus via mbedtls)
- Pas de PSRAM utilisé

## Évolution future (b)

Pour ajouter la configuration depuis VictronConnect :
- Ajouter des caractéristiques WRITE au service GATT SmartShunt
- Mapper les écritures Victron vers `bmu_config_set_thresholds()`
- Nécessite plus de reverse-engineering du protocole GATT Victron (commandes, handshake)
- Le scanner central (c/d) reste inchangé
