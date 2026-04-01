# KXKM BMU Smartphone App — Design Spec

**Date:** 2026-04-01
**Client:** KompleX KapharnaüM (Villeurbanne)
**Projet:** Application mobile compagnon pour le BMU (Battery Management Unit)

## Objectif

Application iOS + Android native pour monitorer et contrôler les batteries BMU en parallèle (24-30V, jusqu'à 16 batteries). Trois canaux de communication : BLE en proximité, WiFi direct sur le même réseau, et cloud via kxkm-ai à distance. Contrôle complet (switch, reset, config seuils) en proximité, lecture seule à distance.

## Stack technique

- **Kotlin Multiplatform (KMP)** — logique partagée iOS/Android (~80% du code)
- **SwiftUI** — UI native iOS
- **Jetpack Compose** — UI native Android
- **Kable** — BLE cross-platform (wrapper CoreBluetooth/Android BLE)
- **Ktor Client** — HTTP/WebSocket cross-platform
- **Eclipse Paho** — MQTT client cross-platform
- **SQLDelight** — persistance locale cross-platform (SQLite)

## Architecture modules

```
kxkm-bmu-app/
├── shared/                    # Kotlin Multiplatform
│   ├── transport/
│   │   ├── TransportManager.kt    # Sélection auto BLE>WiFi>Cloud>Offline
│   │   ├── BleTransport.kt        # Kable GATT, parsing 3 services
│   │   ├── WifiTransport.kt       # Ktor WebSocket direct BMU
│   │   ├── MqttTransport.kt       # Paho MQTT → kxkm-ai broker
│   │   ├── CloudRestClient.kt     # Ktor REST → kxkm-ai API (historique)
│   │   └── OfflineTransport.kt    # Lecture cache SQLDelight
│   ├── model/                 # BatteryState, SystemInfo, SolarData, AuditEvent
│   ├── domain/                # Use cases (monitoring, control, config)
│   ├── auth/                  # Rôles (Admin/Technician/Viewer), PIN, audit
│   ├── db/                    # SQLDelight schemas + queries
│   └── sync/                  # Queue locale → kxkm-ai REST batch
├── androidApp/                # Jetpack Compose UI
│   └── ui/                    # Screens, navigation, Material3
├── iosApp/                    # SwiftUI UI
│   └── Views/                 # Screens, navigation, thème natif
└── gradle/                    # Build config KMP
```

## Transport — 4 canaux avec fallback

| Canal | Données | Contrôle | Latence | Quand |
|-------|---------|----------|---------|-------|
| BLE | notify 1s | complet (switch, reset, config, WiFi) | ~50ms | Device à portée BLE |
| WiFi direct | WebSocket 500ms | complet (switch, reset, config) | ~20ms | Même réseau que BMU |
| Cloud MQTT | subscribe `bmu/battery/#` | lecture seule | ~500ms | N'importe où avec internet |
| Cloud REST | polling on-demand | lecture seule (historique, audit) | ~1s | N'importe où avec internet |
| Offline | cache SQLDelight | aucun | — | Aucune connexion |

**Fallback automatique :** TransportManager détecte la disponibilité de chaque canal et bascule dans l'ordre BLE > WiFi > Cloud > Offline. L'utilisateur peut forcer un canal dans les paramètres.

## Modèle de données

### Entités temps réel

```kotlin
data class BatteryState(
    val index: Int,            // 0-15
    val voltageMv: Int,
    val currentMa: Int,
    val state: BatteryStatus,  // CONNECTED, DISCONNECTED, RECONNECTING, ERROR, LOCKED
    val ahDischargeMah: Int,
    val ahChargeMah: Int,
    val nbSwitch: Int
)

enum class BatteryStatus { CONNECTED, DISCONNECTED, RECONNECTING, ERROR, LOCKED }

data class SystemInfo(
    val firmwareVersion: String,
    val heapFree: Long,
    val uptimeSeconds: Long,
    val wifiIp: String?,
    val nbIna: Int,
    val nbTca: Int,
    val topologyValid: Boolean
)

data class SolarData(
    val batteryVoltageMv: Int,
    val batteryCurrentMa: Int,
    val panelVoltageMv: Int,
    val panelPowerW: Int,
    val chargeState: Int,
    val yieldTodayWh: Long
)
```

### Entités persistées (SQLDelight)

```kotlin
data class AuditEvent(
    val timestamp: Long,
    val userId: String,
    val action: String,        // "switch_on", "switch_off", "reset", "config_change", "wifi_config"
    val target: Int?,          // battery index (null pour config globale)
    val detail: String?
)

enum class UserRole { ADMIN, TECHNICIAN, VIEWER }

data class UserProfile(
    val id: String,
    val name: String,
    val role: UserRole,
    val pinHash: String,       // SHA-256 + salt
    val salt: String
)
```

### Tables SQLDelight

- **battery_history** — snapshots toutes les 10s (voltage, current, state par batterie). Rétention 7 jours, ~8 Mo/jour max.
- **audit_events** — append-only, non effaçable même par admin.
- **user_profiles** — rôle + nom + PIN hashé.
- **sync_queue** — events en attente de sync vers kxkm-ai. Purgée après sync confirmé.

## Écrans

### 1. Dashboard (onglet principal)

Grille de cellules batterie (layout adaptatif 2-4 colonnes). Chaque cellule affiche :
- Numéro batterie
- Tension (V) avec code couleur (vert normal, orange warning, rouge erreur)
- Courant (A)
- État (icône + couleur : vert=connecté, rouge=déconnecté/locked, jaune=reconnecting)
- Indicateur switch count

Tap sur une cellule → écran Détail.

Barre de statut en haut : canal actif (BLE/WiFi/Cloud/Offline), nom device, RSSI.

### 2. Détail batterie

- Graphe historique tension + courant (zoomable, pan horizontal, dernières 24h)
- Compteurs : Ah déchargés, Ah chargés, nb switches
- État courant avec timestamp dernier changement
- Boutons d'action (selon rôle) :
  - Switch ON / Switch OFF (Technician+Admin) — avec dialog de confirmation
  - Reset compteur switches (Technician+Admin)
- Historique d'événements pour cette batterie (filtré depuis audit trail)

### 3. Système

- Firmware version, uptime, heap libre
- Topologie : nb INA / nb TCA, état validation
- Données solaire VE.Direct : tension panel, puissance, état charge, yield today
- État connexion : canal actif, RSSI BLE, IP WiFi, état MQTT/REST

### 4. Audit

- Liste chronologique des actions (icônes par type)
- Filtres : par utilisateur, par action, par batterie, par période
- Non effaçable
- Indicateur sync : événements en attente de push cloud

### 5. Config (Admin)

- **Protection** — seuils V_min, V_max, I_max, V_diff (envoyés via BLE GATT ou WiFi API)
- **WiFi BMU** — SSID + password (BLE uniquement), état connexion, IP, RSSI
- **Utilisateurs** — créer/modifier/supprimer profils, assigner rôles
- **Sync cloud** — URL kxkm-ai, credentials MQTT, fréquence sync, état queue
- **Transport** — canal actif, forçage manuel, timeouts

## Sécurité

### Authentification app

- Premier lancement : création profil Admin (nom + PIN 6 chiffres)
- Profils suivants créés par Admin avec rôle assigné
- PIN requis à chaque ouverture (+ option biométrie Face ID / Touch ID)
- PIN hashé SHA-256 + salt, stocké dans SQLDelight

### Matrice de permissions

| Action | Admin | Technician | Viewer |
|--------|-------|------------|--------|
| Dashboard + détail batteries | oui | oui | oui |
| Système + solaire | oui | oui | oui |
| Consulter audit trail | oui | oui | oui |
| Switch ON/OFF batterie | oui | oui | non |
| Reset compteur switches | oui | oui | non |
| Modifier seuils protection | oui | non | non |
| Config WiFi BMU | oui | non | non |
| Gérer utilisateurs/rôles | oui | non | non |
| Config sync cloud | oui | non | non |

### Sécurité par canal

- **BLE** — pairing Secure Connections (déjà firmware). Caractéristiques Control exigent encryption (WRITE_ENC).
- **WiFi direct** — token d'auth (existant dans bmu_web). Saisi une fois, persisté localement.
- **Cloud MQTT** — credentials username/password vers broker kxkm-ai.
- **Cloud REST** — API key dans header Authorization.

### Audit trail

- Chaque action de contrôle génère un AuditEvent horodaté
- Stocké en append-only dans SQLDelight (non effaçable)
- Synchronisé vers kxkm-ai quand réseau disponible

## Sync cloud

### Flux

```
BMU → (BLE/WiFi) → App → SQLDelight local
                              ↓ (quand réseau dispo)
                    kxkm-ai InfluxDB (historique batteries)
                    kxkm-ai PostgreSQL/SQLite (audit trail)
```

### Mécanisme

- File d'attente `sync_queue` en SQLDelight
- Push HTTP POST batch (max 100 events) vers `kxkm-ai/api/bmu/sync`
- Retry exponentiel en cas d'échec (1s, 2s, 4s, 8s, max 60s)
- Indicateur UI : dernier sync réussi, nb events en queue

### API kxkm-ai à créer

| Method | Route | Auth | Description |
|--------|-------|------|-------------|
| POST | `/api/bmu/sync` | API key | Réception batch historique + audit |
| GET | `/api/bmu/batteries` | API key | État courant (dernier point InfluxDB) |
| GET | `/api/bmu/history?from=&to=&battery=` | API key | Série temporelle InfluxDB |
| GET | `/api/bmu/audit?from=&to=&user=&action=` | API key | Journal audit filtré |

Service Python FastAPI (~200 lignes), déployé sur kxkm-ai, query InfluxDB existant.

## Modifications firmware

### BLE — 2 caractéristiques à ajouter (Control Service 0x0003)

| Caractéristique | UUID suffix | Type | Payload | Action |
|---|---|---|---|---|
| WiFi Config | 0x0034 | WRITE_ENC | SSID (32B) + password (64B) | Sauve NVS, reconnexion auto |
| WiFi Status | 0x0035 | READ, NOTIFY | SSID + IP + RSSI (int8) + connected (bool) | Notify toutes les 10s |

Ajout dans `bmu_ble_control_svc.cpp`. Estimé ~60 lignes.

### HTTP — 2 routes à ajouter (bmu_web.cpp)

| Method | Route | Auth | Response |
|--------|-------|------|----------|
| GET | `/api/system` | none | `{"fw_version":"0.4.0","uptime":3600,"heap":16800000,"nb_ina":4,"nb_tca":1,"topology_valid":true}` |
| GET | `/api/solar` | none | `{"battery_mv":26800,"panel_mv":35200,"panel_w":120,"charge_state":3,"yield_wh":450}` |

Estimé ~90 lignes.

**Total firmware : ~150 lignes de code.**

## Interface BLE GATT existante (référence)

### Service 0x0001 — Battery (16 caractéristiques, READ+NOTIFY 1s)

Par batterie (0x0010–0x001F) : `{voltage_mv: i32, current_ma: i32, state: u8, ah_discharge_mah: i32, ah_charge_mah: i32, nb_switch: u8}` — 15 bytes little-endian.

### Service 0x0002 — System (6 caractéristiques)

- 0x0020 Firmware Version (READ) — string
- 0x0021 Heap Free (READ, NOTIFY 10s) — u32
- 0x0022 Uptime (READ) — u32 secondes
- 0x0023 WiFi IP (READ) — string
- 0x0024 Topology (READ) — `{nb_ina: u8, nb_tca: u8, valid: u8}`
- 0x0025 Solar (READ, NOTIFY 10s) — 12 bytes struct VE.Direct

### Service 0x0003 — Control (6 caractéristiques, toutes WRITE_ENC)

- 0x0030 Switch — `{battery_idx: u8, on_off: u8}`
- 0x0031 Reset — `{battery_idx: u8}`
- 0x0032 Config — `{min_mv: u16, max_mv: u16, max_ma: u16, diff_mv: u16}`
- 0x0033 Status (READ, NOTIFY) — `{last_cmd: u8, battery_idx: u8, result: u8}`
- 0x0034 WiFi Config — `{ssid: 32B, password: 64B}` **(à ajouter)**
- 0x0035 WiFi Status (READ, NOTIFY) — `{ssid, ip, rssi, connected}` **(à ajouter)**

## Hors scope V1

- Notifications push OS (iOS/Android) quand batterie en erreur
- Multi-BMU (l'app gère un seul BMU à la fois)
- Firmware OTA depuis l'app
- Localisation GPS des BMU
- Export CSV/PDF des rapports
