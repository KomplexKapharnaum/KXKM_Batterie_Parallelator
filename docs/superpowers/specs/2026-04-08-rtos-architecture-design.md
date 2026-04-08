# KXKM BMU — Refonte Architecture RTOS & Firmware Logic

**Date:** 2026-04-08
**Statut:** Approuvé (brainstorming)
**Scope:** Phase C (patterns RTOS) + Phase A (restructuration architecture)

---

## 1. Objectifs

1. **Moderniser les patterns FreeRTOS** — remplacer polling/mutex partagé par des queues, snapshots, event-driven
2. **Restructurer l'architecture** — tâches découplées avec protection maître du timing et des switches
3. **Scalabilité** — préparer le dual-bus 32 batteries (TCA9548A mux)
4. **Testabilité** — interfaces abstraites permettant des tests host sans hardware

## 2. Modèle d'exécution

### 2.1 Architecture en tâches

| Tâche | Période | Priorité | Mécanisme de réveil |
|-------|---------|----------|---------------------|
| **protection** | 100ms | 8 (haute) | `vTaskDelayUntil` (timing dur) |
| **balancer** | 500ms | 5 (moyenne) | Réception snapshot via queue |
| **display** | 200ms | 3 (basse) | `vTaskDelayUntil` + dernier snapshot |
| **cloud/telemetry** | 10s | 2 (basse) | Timer software ESP-IDF (`esp_timer`) |
| **hotplug** | 5-10s | 2 (basse) | Timer software ESP-IDF (`esp_timer`) |
| **BLE notify** | 2s | 3 (basse) | Timer software ESP-IDF (`esp_timer`) |

### 2.2 Principes fondamentaux

- **Protection = seul acteur** qui touche aux switches TCA9535
- **Snapshot immutable** pushé dans N queues (une par consommateur)
- **Balancer = conseiller** — envoie des `BALANCE_REQUEST`, la protection décide
- **Hotplug = producteur d'événements** — envoie `TOPOLOGY_CHANGED`, la protection applique
- **I2C** protégé par mutex avec priority inheritance
- **Chaque device** a un health score (0-100) inclus dans le snapshot

### 2.3 Flux de données

```
                    cmd queue
  hotplug ──TOPO_CHANGED──▶┐
  balancer ─BALANCE_REQ───▶│
  web ──────WEB_SWITCH────▶│
                           ▼
                    ┌──────────────┐
                    │  protection  │
                    │  prio 8      │
                    │  100ms hard  │
                    └──────┬───────┘
                           │ snapshot (immutable)
              ┌────────────┼────────────┬────────────┐
              ▼            ▼            ▼            ▼
         q_balancer   q_display    q_cloud      q_ble
         (depth 1)    (depth 1)   (depth 2)    (depth 1)
         overwrite    overwrite   overwrite    overwrite
```

## 3. Structure du snapshot

```c
#define BMU_MAX_BATTERIES 32  // dual-bus ready

typedef struct {
    uint32_t timestamp_ms;
    uint16_t cycle_count;
    uint8_t  nb_batteries;
    bool     topology_ok;

    struct {
        float       voltage_mv;
        float       current_a;
        bmu_state_t state;
        uint8_t     health_score;   // 0-100, santé I2C du capteur
        uint8_t     nb_switches;
        bool        balancer_active;
    } battery[BMU_MAX_BATTERIES];

    float fleet_max_mv;
    float fleet_mean_mv;
} bmu_snapshot_t;
```

## 4. Queue de commandes vers la protection

```c
typedef enum {
    CMD_TOPOLOGY_CHANGED,   // payload: nouvelle topologie (devices + counts)
    CMD_BALANCE_REQUEST,    // payload: index batterie + action (duty ON/OFF)
    CMD_WEB_SWITCH,         // payload: index batterie + ON/OFF (depuis UI web)
    CMD_CONFIG_UPDATE,      // payload: nouveaux seuils (depuis NVS/web)
    CMD_BUS_RECOVERY,       // payload: bus_id (triggered by hotplug quorum failure)
} bmu_cmd_type_t;

typedef struct {
    bmu_cmd_type_t type;
    union {
        struct { bmu_device_t* devices; uint8_t nb_ina; uint8_t nb_tca; } topology;
        struct { uint8_t battery_idx; bool on; } balance_req;
        struct { uint8_t battery_idx; bool on; } web_switch;
        struct { uint8_t bus_id; } bus_recovery;
    } payload;
} bmu_cmd_t;
```

La protection traite les commandes au début de chaque cycle, **avant** les lectures I2C.

## 5. Health score par device

### 5.1 Structure

```c
typedef struct {
    uint8_t score;          // 0-100, init à 100
    uint8_t consec_fails;   // pour bus recovery global
} bmu_device_health_t;
```

### 5.2 Règles d'évolution

| Événement | Effet sur score |
|-----------|----------------|
| Lecture OK | `score = min(100, score + 5)` — 20 succès pour revenir à 100 |
| Lecture fail | `score = max(0, score - 20)` — 5 fails pour atteindre 0 |
| Bus recovery | score inchangé, `consec_fails` remis à 0 |

### 5.3 Seuils d'action (protection)

| Score | État | Action |
|-------|------|--------|
| 100-60 | Sain | Normal |
| 59-30 | Dégradé | Log warning, LED orange clignotante |
| 29-1 | Critique | Batterie forcée OFF, événement cloud |
| 0 | Mort | Batterie LOCKED, arrêt lectures |

### 5.4 Hystérésis

Batterie passée OFF à score < 30 ne se reconnecte que si score remonte au-dessus de 60.

### 5.5 Bus recovery global

Quand `consec_fails` de **tous** les devices actifs atteint 5 simultanément → `i2c_master_bus_reset()`. Détecté par le hotplug (quorum failure), propagé via `CMD_BUS_RECOVERY`.

## 6. Scalabilité dual-bus 32 batteries

### 6.1 Abstraction bus I2C

```c
typedef struct {
    i2c_master_bus_handle_t handle;
    SemaphoreHandle_t       mutex;      // priority inheritance
    uint8_t                 bus_id;     // 0 = DOCK hardware, 1 = bitbang
    bmu_device_health_t     bus_health;
} bmu_i2c_bus_t;

typedef struct {
    uint8_t                 bus_id;
    uint8_t                 local_idx;  // index sur ce bus (0-15)
    uint8_t                 global_idx; // index global (0-31)
    i2c_master_dev_handle_t handle;
    bmu_device_health_t     health;
} bmu_device_t;
```

### 6.2 Mapping

```
Bus 0 (DOCK, hardware 100kHz)       Bus 1 (bitbang ou TCA9548A mux)
  INA 0x40-0x4F → batterie 0-15      INA 0x40-0x4F → batterie 16-31
  TCA 0x20-0x23 → groupes 0-3        TCA 0x20-0x23 → groupes 4-7
```

### 6.3 Impact par composant

| Composant | Changement |
|-----------|-----------|
| protection | Itère sur `bmu_device_t[]` avec `global_idx`, transparent au bus |
| snapshot | `BMU_MAX_BATTERIES = 32`, déjà dimensionné |
| hotplug | Deux passes de scan (une par bus), topologie validée globalement |
| balancer | `fleet_mean` sur les 32, duty-cycle indépendant du bus |
| INA237/TCA9535 | Reçoivent `bmu_device_t*` — le driver utilise le bon bus handle |

### 6.4 Migration douce

Bus 1 désactivé si GPIOs non configurés dans NVS. `nb_buses = 1` par défaut. Zéro régression sur hardware 16 batteries actuel.

## 7. Testabilité — interfaces abstraites

### 7.1 Interfaces (vtables C)

```c
typedef struct {
    esp_err_t (*read_register)(bmu_device_t* dev, uint8_t reg, uint8_t* buf, size_t len);
    esp_err_t (*write_register)(bmu_device_t* dev, uint8_t reg, const uint8_t* buf, size_t len);
    esp_err_t (*probe)(bmu_i2c_bus_t* bus, uint8_t addr);
} bmu_i2c_ops_t;

typedef struct {
    esp_err_t (*read_voltage_current)(bmu_device_t* dev, float* v, float* i);
    uint8_t   (*get_health_score)(bmu_device_t* dev);
} bmu_sensor_ops_t;

typedef struct {
    esp_err_t (*switch_battery)(bmu_device_t* dev, uint8_t channel, bool on);
    esp_err_t (*set_led)(bmu_device_t* dev, uint8_t channel, uint8_t color);
} bmu_switch_ops_t;
```

### 7.2 Injection

```c
esp_err_t bmu_protection_init(const bmu_protection_config_t* cfg);
// cfg contient: sensor_ops, switch_ops, queues, seuils...
```

### 7.3 Tests débloqués

| Test | Méthode |
|------|---------|
| State machine complète | Mock sensor (inject V/I), mock switch (vérifie ON/OFF) |
| Imbalance 3 cycles | Mock retourne fleet_max - 600mV × 3, vérifie DISCONNECTED |
| Health score decay | Mock retourne ESP_FAIL × N, vérifie score et transition OFF |
| Glitch filter | Mock retourne V/2 puis V, vérifie rejet du glitch |
| Balancer duty-cycle | Mock sensor + vérifie BALANCE_REQUEST en queue |
| Hotplug topology | Mock probe (appear/disappear), vérifie CMD_TOPOLOGY_CHANGED |

### 7.4 Migration des tests

Les 13 tests PlatformIO sim-host migrent vers `firmware-idf/host_test/` avec ces interfaces. Plus de `#ifdef NATIVE` — mocks injectés au runtime.

## 8. Plan de migration incrémental

**Contrainte :** firmware en production avec 8 batteries. Chaque phase = firmware flashable + test terrain.

### Phase 1 — Fondations (sans changer le comportement)

- Créer `bmu_device_t` et `bmu_i2c_bus_t` (abstraction bus)
- Créer `bmu_snapshot_t` (struct, pas encore utilisée)
- Créer les interfaces `*_ops_t` (sensor, switch, i2c)
- Ajouter priority inheritance au mutex I2C existant
- Tests host : snapshot creation, health score logic

### Phase 2 — Protection autonome

- Extraire la protection du `while(true)` app_main → tâche dédiée prio 8
- Protection produit le snapshot, le pousse dans les queues
- Ajouter la cmd queue (mais seul `CMD_WEB_SWITCH` pour l'instant)
- Main loop réduit : juste dispatch snapshot vers display
- Tests host : state machine complète avec mock sensor/switch

### Phase 3 — Découplage complet

- Balancer → tâche prio 5, reçoit snapshot, envoie `BALANCE_REQUEST`
- Display → tâche prio 3, consomme snapshot
- Cloud → `esp_timer`, consomme snapshot
- Hotplug → envoie `CMD_TOPOLOGY_CHANGED`
- Health score par device (remplace compteur global)
- Tests host : balancer logic, topology changes

### Phase 4 — Dual-bus 32 batteries

- `bmu_i2c_bus_t[2]` avec bus bitbang
- `global_idx` mapping (0-31)
- Hotplug dual-bus scan
- Validation topologique multi-bus
- Tests host : dual-bus scenarios

### Règle de sécurité

Chaque phase se termine par flash + test terrain (8 batteries actuelles) avant de passer à la suivante. Régression → rollback de la phase.

### Fichiers non impactés

`bmu_config`, `bmu_web`, `bmu_ota`, `bmu_wifi`, `bmu_ble_victron*`, `bmu_vedirect`, `bmu_storage` — aucun changement.

## 9. Accès I2C

- Mutex avec **priority inheritance** natif FreeRTOS (`xSemaphoreCreateMutex`)
- Chaque bus a son propre mutex (pas de contention inter-bus)
- Transactions I2C à 100kHz durent ~1-2ms max → contention faible
- Si protection (prio 8) attend le bus tenu par display (prio 3), display est boostée temporairement à prio 8

## 10. Composants impactés vs non impactés

### Modifiés

| Composant | Nature du changement |
|-----------|---------------------|
| `bmu_protection` | Tâche autonome, snapshot producer, cmd queue consumer |
| `bmu_balancer` | Tâche autonome, snapshot consumer, BALANCE_REQUEST producer |
| `bmu_ina237` | Accepte `bmu_device_t*`, implémente `bmu_sensor_ops_t` |
| `bmu_tca9535` | Accepte `bmu_device_t*`, implémente `bmu_switch_ops_t` |
| `bmu_i2c` | Abstraction multi-bus, priority inheritance, health score |
| `bmu_i2c_hotplug` | Dual-bus scan, CMD_TOPOLOGY_CHANGED via queue |
| `bmu_display` | Consomme snapshot via queue au lieu de lire état partagé |
| `bmu_influx` | Consomme snapshot via queue |
| `bmu_ble` | Consomme snapshot via queue |
| `bmu_battery_manager` | Reçoit snapshot, Ah tracking depuis données snapshot |
| `main.cpp` | Boot crée les tâches et queues, boucle principale supprimée |

### Non modifiés

`bmu_config`, `bmu_web` (changement mineur : poste `CMD_WEB_SWITCH` dans la cmd queue au lieu d'appeler la protection directement), `bmu_web_security`, `bmu_ota`, `bmu_wifi`, `bmu_ble_victron`, `bmu_ble_victron_gatt`, `bmu_ble_victron_scan`, `bmu_vedirect`, `bmu_vrm`, `bmu_storage`, `bmu_sntp`, `bmu_mqtt`, `bmu_climate`, `bmu_soh`, `bmu_rint`
