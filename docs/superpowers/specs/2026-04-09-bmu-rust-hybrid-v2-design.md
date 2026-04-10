# BMU Rust-Hybrid V2 — Design Spec

**Date:** 2026-04-09
**Author:** L'Electron Rare (brainstorming session Claude Opus 4.6)
**Status:** Draft — awaiting user review
**Supersedes:** `2026-03-30-rust-hybrid-firmware-design.md` (248 lignes, concept seulement)
**Companion plan:** `docs/superpowers/plans/2026-04-09-bmu-rust-hybrid-v2-plan.md` (à produire par writing-plans)

## 0. Résumé exécutif

Réécriture complète du firmware BMU (Battery Management Unit) KXKM en architecture hybride **Rust no_std (critique) + C ESP-IDF (connectivité/UI/ML)**. La réécriture est motivée par :

1. **Consolidation spec** — 15 specs superpowers pré-existants se sont accumulés sans fil conducteur. Ce document les remplace pour le périmètre V1.
2. **Pivot technique** — abandon du firmware ESP-IDF C/C++ actuel (25 composants, dette architecturale, fragilité runtime documentée par 5 commits de fix successifs sur I²C/WDT/INA237/SOH).
3. **Safety by-design** — les 4 audit findings CRIT de mars 2026 (mV/V confusion, imbalance vs fleet max, deadlock, auth web) sont éliminés par construction grâce au type system Rust et au remplacement du web server par une auth BLE signée.

Le firmware v1 (ESP-IDF C/C++, commit `f8baa2a`) est **archivé intact** sur le tag `v1-final-archive` et la branche `archive/firmware-v1` (locaux, non pushés). Il reste flashable à tout moment pendant la migration comme référence comportementale.

## 1. Goals et non-goals

### Goals (V1)

- G1. Garantir la safety batterie F01-F11 du spec Kill_LIFE `specs/01_spec.md` avec une latence de coupure mesurée ≤ 250 ms (cible Kill_LIFE = 500 ms).
- G2. Placer toute la logique critique (drivers I²C, state machine protection, R_int, balancer, Ah counting) dans un seul staticlib Rust no_std auditable, testé sur host Mac/Linux à 100% sans hardware.
- G3. Éliminer les 4 audit findings CRIT (CRIT-A/B/C/D) avec des tests de régression dédiés et des mécanismes by-design.
- G4. Fournir une app-facing BLE GATT complète (battery/system/control/config + Victron SmartShunt emulation) compatible avec l'app iOS KMP existante.
- G5. Publier de la télémétrie MQTT vers le broker `kxkm-ai:1883` avec fallback log SD rolling 1 GB et replay automatique à la reconnexion.
- G6. Afficher 5 tabs LVGL sur l'écran BOX-3 (BATT, SOH, SYS, CLIMATE, CONFIG) avec touch-to-detail sur la tab BATT.
- G7. Débloquer le gate Kill_LIFE S3 en validant TB01-TB13 du `specs/04_validation.md`.

### Non-goals (V1, explicitement hors périmètre)

| Feature | Raison |
|---|---|
| Dual-I²C 32 batteries | Q2 utilisateur — on reste à 16 batteries / 1 bus pour V1 |
| VE.Direct UART (lecture SmartSolar physique) | Q2/Q8 — droppé |
| Scan BLE Victron tiers (parser balises MPPT externes) | Q8 — droppé |
| Cloud Victron VRM (`vrm.victronenergy.com`) | Q8 — droppé |
| InfluxDB HTTP direct | Remplacé par MQTT + SD replay |
| USB MSC (stockage USB-visible PC) | Q8 — droppé |
| Web server local (AsyncWebServer) | Q8 — droppé, CRIT-D éliminé par ce drop |
| OTA HTTPS pull-based | Report V2, pas de mécanisme de sécurité mature en V1 |
| Embassy async runtime | Option C Rust no_std sync — pas de futures, dual-core pinning suffit |
| Balancer hardware MOSFET autoritaire | Le balancer propose, la protection dispose (AND-mask) |
| iOS app / KMP app development | Hors scope firmware — consommateur, pas produit |
| Modification hardware (PCB v3, BOM) | Gelé — on réécrit le firmware sur la PCB v2 existante |

### Acceptance criteria (V1 = DONE)

1. Toutes les features F01-F11 de `specs/01_spec.md` couvertes (F10 web interface → remplacé par BLE control).
2. Les 4 fixes audit CRIT vérifiés par tests de régression Rust dédiés (host).
3. HIL TB01-TB13 (10 MUST) tous au vert, rapport PDF signé dans `docs/superpowers/validation/`.
4. Bench longue durée 72 h continu sans WDT trigger, sans reboot inattendu, sans perte SD log.
5. Mémoire flash < 85% du budget OTA (2 MB), RAM < 75% (512 KB internal + PSRAM dispo).
6. Latence coupure mesurée ≤ 250 ms (p99 bench HIL).
7. Couverture tests Rust ≥ 80% sur `bmu-protection`, `bmu-rint`, `bmu-balancer` — **target indicatif**, mesuré en CI via `cargo llvm-cov`, **non-bloquant** pour merge ni pour acceptance V1. Sert de signal de qualité, pas de gate.
8. iOS app (build existant) affiche les 16 batteries via BLE et peut envoyer ForceOff/ResetAh/TriggerRint avec effet visible < 1 s.
9. VictronConnect app voit le BMU comme un SmartShunt valide (V/I/SoC fleet).
10. MQTT broker `kxkm-ai:1883` reçoit la télémétrie continue ; replay SD fonctionne après coupure réseau de 5+ min.

## 2. Architecture globale

### 2.1 Modèle d'exécution : deux mondes, un pont FFI unidirectionnel

```
┌────────────────────────── ESP32-S3 ──────────────────────────┐
│                                                              │
│  PRO_CPU (core 0)              APP_CPU (core 1)              │
│  ────────────────              ────────────────              │
│                                                              │
│  task_bmu_core (prio 8)        task_ble       (prio 6)       │
│  period 200 ms (5 Hz)          task_wifi_mqtt (prio 4)       │
│    ↓                           task_display  (prio 3)        │
│  [READ raw I²C via glue]       task_soh      (prio 3)        │
│    ↓                           task_climate  (prio 2)        │
│  bmu_core_tick(&raw,           task_sd_replay (prio 2)       │
│                &snapshot,                                    │
│                &actions)  ← ←  q_cmd ← commands from BLE/UI  │
│    ↓                                                         │
│  [EXECUTE actions                                            │
│   via TCA9535 writes]                                        │
│    ↓                                                         │
│  fan-out → 4× xQueueOverwrite                                │
│            ├─► q_display ──► task_display                    │
│            ├─► q_ble      ──► task_ble                       │
│            ├─► q_cloud    ──► task_wifi_mqtt                 │
│            └─► q_balancer ──► (feedback duty loop)           │
│                                                              │
└──────────────────────────────────────────────────────────────┘
          ╔═══════════════════════════════╗
          ║   FFI one-way (C → Rust)      ║
          ║   bmu_core.h (cbindgen)       ║
          ╚═══════════════════════════════╝
```

**Règle d'or : Rust n'appelle JAMAIS C directement.** Le flux est strictement unidirectionnel :

1. C (`task_bmu_core`) lit les bytes bruts I²C via le composant `bmu_i2c_glue` (wrapping `esp-idf` `i2c_master`).
2. C appelle `bmu_core_tick(core, &raw_buf, &snapshot_out, &actions_out)` en passant les bytes.
3. Rust parse les bytes (drivers pure-Rust), exécute la state machine protection, calcule R_int/balancer, produit un `Snapshot` immuable et une liste d'`Actions` (bitmask TCA9535 à set/clr, trigger R_int, request SOH).
4. C exécute les actions (écritures I²C TCA9535, démarrage mesure pulse R_int).
5. C publie le snapshot dans les 4 queues FreeRTOS (`xQueueOverwrite` — toujours la valeur fraîche, jamais de backpressure).

Conséquences :

- **Drivers Rust = parseurs purs**. Ils prennent `&[u8]` et rendent `Result<Measurement, Error>`. Testables sur Mac en quelques millisecondes, sans mock de bus.
- **Core Rust = déterministe**. Même input → même output. Injection de fixtures triviales en test.
- **Pas d'embassy, pas d'async**. Code sync appelé à 5 Hz, cohérent avec l'Option C Rust.
- **Commandes** (ForceOff, ResetAh, TriggerRint, SetConfig, …) transitent par un buffer circulaire SPSC drainé au début de chaque tick.

### 2.2 Garanties temps-réel

- `task_bmu_core` **pinné sur PRO_CPU (core 0)** via `xTaskCreatePinnedToCore`.
- Tâches BLE/Wi-Fi/LVGL/SOH/Climate/SD **pinnées sur APP_CPU (core 1)** via `esp_pthread_cfg` et `xTaskCreatePinnedToCore`.
- WDT task-level à 3 s sur `task_bmu_core`, feed à chaque tick 200 ms.
- Latence de coupure garantie **≤ 250 ms** (1 tick pour détection + 1 tick pour exécution TCA9535).
- Aucune allocation heap côté Rust après `bmu_core_init` (tableaux statiques bornés).

### 2.3 Tag & branche d'archive du firmware v1

- Tag annoté `v1-final-archive` pointant sur le commit `f8baa2a` (dernier commit firmware C/C++).
- Branche locale `archive/firmware-v1` sur le même commit.
- Les dossiers `firmware-idf/` et `firmware/` restent restaurés dans le working tree de la branche de travail `feat/rust-hybrid-v2` comme **référence comportementale** pendant la migration.
- **Ni le tag ni la branche ne sont pushés** sur le remote à ce stade. Push à faire manuellement quand l'utilisateur valide.

## 3. Workspace Rust et surface FFI

### 3.1 Structure `firmware-rust/`

Le dossier `firmware-rs/` actuel (768 lignes, scaffold partiel) est **écrasé** et remplacé par une structure complète sous un nouveau nom `firmware-rust/` (le tiret `-rust` vs `-rs` marque la rupture) :

```
firmware-rust/
├── Cargo.toml                     # workspace manifest
├── rust-toolchain.toml            # channel "esp" via espup
├── .cargo/config.toml             # NO default target — spécifié en CLI
├── crates/
│   ├── bmu-types/                 # types partagés, zéro dépendance
│   │   ├── Cargo.toml
│   │   └── src/lib.rs             # Millivolts, Milliamps, Snapshot, Action, Command, Error, Config
│   ├── bmu-i2c/                   # trait I2cBus + MockBus helpers
│   │   ├── Cargo.toml
│   │   └── src/lib.rs             # pub trait I2cBus { write_read, write, read, probe_idle, recover }
│   ├── bmu-drivers/               # INA237, TCA9535, AHT30 — parseurs purs
│   │   ├── Cargo.toml
│   │   └── src/
│   │       ├── lib.rs
│   │       ├── ina237.rs
│   │       ├── tca9535.rs
│   │       └── aht30.rs
│   ├── bmu-protection/            # state machine F01-F11 + battery manager Ah counting
│   │   ├── Cargo.toml
│   │   └── src/
│   │       ├── lib.rs
│   │       ├── state.rs           # enum BatteryState, transitions
│   │       ├── manager.rs         # battery_manager Ah counting + fleet aggregates
│   │       ├── thresholds.rs      # config + validation runtime
│   │       └── latch.rs           # compteur switchs + permanent latch
│   ├── bmu-rint/                  # mesure R_int pulse-off
│   │   ├── Cargo.toml
│   │   └── src/lib.rs
│   ├── bmu-balancer/              # duty cycling scheduler 3 ON / 2 OFF
│   │   ├── Cargo.toml
│   │   └── src/lib.rs
│   ├── bmu-core/                  # façade FFI, staticlib, cbindgen
│   │   ├── Cargo.toml
│   │   ├── build.rs               # cbindgen → ../../target/include/bmu_core.h
│   │   ├── cbindgen.toml
│   │   └── src/lib.rs             # extern "C" bmu_core_init/tick/cmd/get_cached_snapshot/set_config
│   └── bmu-test-fixtures/         # dev-dep only, captures I²C réelles en binaire
│       ├── Cargo.toml
│       ├── fixtures/              # *.bin captures de registres INA237/TCA9535/AHT30
│       └── src/lib.rs             # helpers de chargement
└── xtask/
    ├── Cargo.toml
    └── src/main.rs                # cargo xtask vendor-header | test-all | abi-check | size
```

### 3.2 Toolchain et cible

- `rust-toolchain.toml` : `channel = "esp"` (espup xtensa-esp-rust, installé via `espup install --targets esp32s3`).
- **Pas** de `[build] target = ...` dans `.cargo/config.toml` racine — ça casse les tests host. Le target est spécifié en CLI :
  - `cargo test` → target host natif (Mac aarch64-apple-darwin par défaut, ou CI linux)
  - `cargo build --target xtensa-esp32s3-none-elf --release -p bmu-core` → build embedded
- Target config xtensa isolée dans un fichier dédié `firmware-rust/crates/bmu-core/.cargo/config.toml` (scoped au crate staticlib).
- `ESP_IDF_VERSION=v5.4` (aligné sur BOX-3 BSP actuel, upgrade depuis 5.3 du scaffold précédent).

### 3.3 Dépendances Rust — audit strict, zéro dépendance native

- `heapless` (String/Vec bornés, no_std)
- `nb` (non-blocking primitives, no_std)
- `bitflags` (registres INA237 bit-packés)
- `libm` (fp math no_std pour R_int)
- `serde` + `postcard` (**dev-only**, serialization pour tests/golden files)
- `cbindgen` (**build-dep uniquement**, côté `bmu-core`)
- `proptest` (**dev-dep**, property tests)

**Interdictions explicites, vérifiées en CI via `cargo tree` grep :**

- ❌ `esp-idf-sys`
- ❌ `esp-hal`, `esp-idf-hal`
- ❌ `embassy-*`
- ❌ `embedded-hal` (décision utilisateur Q3 = C.2, trait maison)
- ❌ toute `sys` crate avec `build.rs` qui linke du C
- ❌ `std` (sauf en `[dev-dependencies]` pour les tests host)

### 3.4 Surface FFI — header unique `bmu_core.h`

Le seul artefact livré au code C ESP-IDF est `libbmu_core.a` + `bmu_core.h`. Le header est généré par `cbindgen` dans `build.rs` du crate `bmu-core`, et **vendoré** dans `firmware-idf-v2/components/bmu_core_rs/include/bmu_core.h` via `cargo xtask vendor-header` pour permettre l'indexing IDE C/C++ sans build Rust préalable.

**Workflow `vendor-header` :** le `build.rs` de `bmu-core` génère automatiquement le header à chaque build Rust dans `firmware-rust/target/.../bmu_core.h`. La commande `cargo xtask vendor-header` copie ce fichier vers `firmware-idf-v2/components/bmu_core_rs/include/bmu_core.h` et doit être lancée manuellement après **toute modification de la surface FFI**. Un hook `pre-commit` est ajouté (étape 11 du plan) qui détecte les divergences entre le header généré et le header vendoré et échoue le commit si le vendor n'a pas été lancé. Ainsi l'indexing IDE reste cohérent sans build Rust préalable, et les incohérences sont interceptées à la pré-push.

Extrait représentatif :

```c
/* Auto-generated by cbindgen — do not edit */
#ifndef BMU_CORE_H
#define BMU_CORE_H

#include <stdint.h>
#include <stdbool.h>

/* Opaque handle — Rust owns state, C never dereferences */
typedef struct BmuCore BmuCore;

/* Config passed at boot (from NVS) */
typedef struct {
    uint32_t umin_mv;              /* default 24000 */
    uint32_t umax_mv;              /* default 30000 */
    int32_t  imax_ma;              /* default 1000 */
    uint32_t vdiff_imbalance_mv;   /* default 1000 */
    uint8_t  nb_switch_max;        /* default 5 */
    uint32_t reconnect_delay_ms;   /* default 10000 */
    uint32_t tick_period_ms;       /* default 200 */
} BmuConfigC;

/* Raw measurement buffer — bytes read from each INA237, in order of address */
typedef struct {
    uint8_t  n_ina;                /* 0..16 */
    uint8_t  n_tca;                /* 0..4 */
    uint8_t  ina_registers[16][18];/* raw register dumps */
    uint8_t  tca_inputs[4];        /* TCA9535 INPUT_PORT0 reads */
    int16_t  climate_temp_c10;     /* AHT30 T × 10 */
    uint16_t climate_rh_pct10;     /* AHT30 RH × 10 */
    uint64_t monotonic_us;         /* esp_timer_get_time() */
} BmuRawInputs;

/* Battery record inside the snapshot */
typedef struct {
    uint8_t  idx;
    uint8_t  state;               /* 0=Unknown 1=Absent 2=Online 3=Offline 4=Latched */
    uint8_t  state_reason;        /* 0=OK 1=UV 2=OV 3=OC 4=Imbalance 5=Topology 6=Manual */
    uint8_t  switch_count;
    int32_t  voltage_mv;
    int32_t  current_ma;
    int32_t  ah_remaining_ma_h;
    int16_t  temp_c10;
    uint8_t  soh_pct;             /* 0..100, 0xFF = unknown */
    uint8_t  balancer_duty_pct;   /* 0..100 */
    uint32_t r_ohmic_m_ohms;      /* 0xFFFFFFFF = not measured */
} BmuBatteryC;

typedef struct {
    bool     topology_ok;
    uint8_t  n_bat;
    uint32_t tick_us_p50;
    uint32_t tick_us_p99;
    uint32_t wdt_feeds;
} BmuSystemC;

typedef struct {
    uint8_t     n_bat;
    BmuBatteryC batteries[16];
    BmuSystemC  system;
} BmuSnapshotC;

/* Actions to execute after tick */
typedef struct {
    uint16_t tca_set_mask;         /* bitmask of relays to turn ON */
    uint16_t tca_clr_mask;         /* bitmask of relays to turn OFF */
    uint8_t  rint_trigger_idx;     /* 0xFF = none */
    uint8_t  request_soh_inference;/* 0 or 1 */
} BmuActionsC;

/* Commands from BLE/UI to the Rust core */
typedef enum {
    BMU_CMD_NONE        = 0,
    BMU_CMD_FORCE_OFF   = 1,
    BMU_CMD_RESET_AH    = 2,
    BMU_CMD_TRIGGER_RINT= 3,
    BMU_CMD_RESET_LATCH = 4,
    BMU_CMD_SET_LABEL   = 5,
    BMU_CMD_SET_CONFIG  = 6,
    BMU_CMD_UPDATE_SOH  = 7,
    BMU_CMD_TOPOLOGY_CHANGED = 8,
} BmuCommandKind;

typedef struct {
    BmuCommandKind kind;
    uint8_t        target_idx;
    uint8_t        payload[32];
} BmuCommandC;

/* === API === */
BmuCore* bmu_core_init(const BmuConfigC* cfg);
void     bmu_core_destroy(BmuCore* core);
int      bmu_core_tick(BmuCore* core,
                       const BmuRawInputs* in,
                       BmuSnapshotC* out_snapshot,
                       BmuActionsC* out_actions);
int      bmu_core_command(BmuCore* core, const BmuCommandC* cmd);
int      bmu_core_get_cached_snapshot(const BmuCore* core, BmuSnapshotC* out);
int      bmu_core_set_config(BmuCore* core, const BmuConfigC* cfg);
int      bmu_core_serialize_battery(const BmuCore* core,
                                    uint8_t idx,
                                    uint8_t out_buf[24]);

#endif /* BMU_CORE_H */
```

Cette surface est **stable par contrat**. Toute évolution = nouveau major version dans `Cargo.toml` du crate `bmu-core` + migration C documentée.

**Note sur le singleton :** contrairement au scaffold `firmware-rs/` actuel qui utilise un `static mut PROT: MaybeUninit`, le nouveau `bmu-core` expose un handle opaque `BmuCore*`. L'instance vit dans un `Box<BmuCore>` Rust créé à l'init (allocation unique, jamais libérée en pratique) et passé par pointeur au C. Raison : permet l'instance multiple en tests host (`#[cfg(test)]`) et évite les pièges `static mut` UB de Rust 2024 edition.

## 4. Types forts et drivers

### 4.1 `bmu-types` — fondation type-safe (CRIT-A by-design)

```rust
// crates/bmu-types/src/lib.rs
#![no_std]

/// Millivolts signés. Jamais confondus avec Volts ou Milliamps.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct Millivolts(i32);

/// Milliamps signés (positif = discharge).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct Milliamps(i32);

/// Milliohms non signés.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct Milliohms(u32);

impl Millivolts {
    pub const ZERO: Self = Self(0);
    pub const fn from_raw(mv: i32) -> Self { Self(mv) }
    pub const fn as_raw(self) -> i32 { self.0 }
    pub const fn abs_diff(self, other: Self) -> u32 {
        (self.0 - other.0).unsigned_abs()
    }
    // Volontairement PAS de From<u32>/From<i32> — forcer from_raw explicite
}

impl Milliamps {
    pub const ZERO: Self = Self(0);
    pub const fn from_raw(ma: i32) -> Self { Self(ma) }
    pub const fn as_raw(self) -> i32 { self.0 }
    pub const fn abs(self) -> u32 { self.0.unsigned_abs() }
}
```

**Conséquence CRIT-A :** il est **impossible** de passer un `i32` en mV à une fonction qui attend des V. Le compilateur refuse. Le bug historique (spec `01_spec.md` valeur `24` au lieu de `24000`) ne peut plus exister.

### 4.2 Trait `I2cBus` (décision Q3 = C.2)

```rust
// crates/bmu-i2c/src/lib.rs
#![no_std]

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum I2cError {
    Nack,           // device didn't ACK
    ArbLost,        // arbitration lost
    Timeout,        // bus stuck (SDA/SCL held)
    BusBusy,        // bus already busy at start
    InvalidLength,  // buffer length out of bounds
    Hardware(u16),  // opaque platform code
}

pub trait I2cBus {
    fn write_read(&mut self, addr: u8, wr: &[u8], rd: &mut [u8]) -> Result<(), I2cError>;
    fn write(&mut self, addr: u8, wr: &[u8]) -> Result<(), I2cError>;
    fn read(&mut self, addr: u8, rd: &mut [u8]) -> Result<(), I2cError>;
    fn probe_idle(&mut self) -> Result<bool, I2cError>;
    fn recover(&mut self) -> Result<(), I2cError>;
}
```

**Trois implémentations possibles, deux vivent dans le repo :**

1. `MockBus` dans `bmu-test-fixtures` (dev-only). Accepte un `Vec<Transaction>` scripté, utilisé par TOUS les tests host.
2. `EspIdfBus` implémentée **hors du workspace Rust**, directement dans le composant ESP-IDF `firmware-idf-v2/components/bmu_i2c_glue/` aux côtés de l'impl C. Ce composant contient à la fois le C (`src/bmu_i2c_glue.c` + `include/bmu_i2c_glue.h` — accès `i2c_master` ESP-IDF) et un petit module Rust (`rust/src/esp_i2c_bus.rs`, ~100 lignes, zéro logique) qui implémente le trait `I2cBus` en FFI vers les symboles C. Ce module Rust est compilé séparément via un `Cargo.toml` local et lié au staticlib `bmu-core` via corrosion. **Il ne vit pas dans le workspace `firmware-rust/`** — raison : il dépend des symboles C ESP-IDF, incompatible avec `cargo test` host. Il n'y a **pas** de composant séparé `bmu_core_rs_bridge` ; cette fonction est assumée par `bmu_i2c_glue/`.
3. `BitbangBus` — **non implémentée en V1**, le trait est designed pour l'accueillir en V2 sans casser l'ABI.

**Gestion des retries et hotplug : dans la couche C, pas Rust.** Le core Rust est stateless vis-à-vis du bus — il reçoit `BmuRawInputs` déjà lus. Si C échoue à lire un INA237, il met `n_ina` plus petit ; le core marque cette batterie `Absent`. La politique de retry + bus recovery + rescan vit dans `task_bmu_core.c` :

- **Comptage fail par adresse** : chaque adresse I²C (16 INA + 4 TCA + 1 AHT30) a un compteur `consecutive_fails[addr]` réinitialisé à 0 sur tout succès.
- **3 fails consécutifs** = 3 ticks successifs (600 ms) où la lecture de cette adresse échoue. Au 3e fail : `bmu_i2c_glue_recover()` (9 clock pulses + STOP) et reset du compteur.
- **Si recover échoue ou 3 nouveaux fails** → adresse considérée morte pour le tick courant, `n_ina` décrémenté dans `BmuRawInputs`, commande `bmu_core_command(BmuCmd::TopologyChanged)` envoyée.
- **Scan hotplug** : **nouvelle tâche FreeRTOS `task_hotplug_scan`**, pinned APP_CPU, priorité 1 (la plus basse), période 30 s. Elle scanne toutes les adresses 0x40-0x4F et 0x20-0x23 (et 0x38 AHT30) en read-only `probe_idle`, compare avec la topologie connue et notifie le core via `BmuCmd::TopologyChanged` si différence. À ajouter dans le diagramme §2.1 étape d'implémentation.

### 4.3 Drivers INA237, TCA9535, AHT30 — parseurs purs

**Philosophie : driver = (parseur bytes + encodeur bytes), pas de bus.** Chaque driver est une struct quasi-vide avec des méthodes pures, générique sur `B: I2cBus` pour les opérations qui touchent le bus.

Exemple `bmu-drivers::ina237` :

```rust
// crates/bmu-drivers/src/ina237.rs
#![no_std]
use bmu_types::{Millivolts, Milliamps, Error};

pub const INA237_ADDR_BASE: u8 = 0x40;
pub const INA237_ADDR_MAX:  u8 = 0x4F;

#[repr(u8)]
pub enum Reg {
    Config     = 0x00,
    AdcConfig  = 0x01,
    ShuntCal   = 0x02,
    VShunt     = 0x04,
    VBus       = 0x05,
    DieTemp    = 0x06,
    Current    = 0x07,
    Power      = 0x08,
    Diag       = 0x0B,
    ManufId    = 0x3E,
    DeviceId   = 0x3F,
}

/// Pure parseur: transform 2 bytes from VBus register into Millivolts.
/// LSB = 3.125 mV/bit après shift droit de 3 bits (bits 0..2 réservés).
pub fn parse_vbus(raw: [u8; 2]) -> Millivolts {
    let word = i16::from_be_bytes(raw);
    let steps = (word >> 3) as i32;
    Millivolts::from_raw((steps * 3125) / 1000)
}

/// Pure encodeur: génère SHUNT_CAL bytes à partir shunt et max_current.
/// Déterministe et testé contre valeurs de référence de la datasheet.
pub fn encode_shunt_cal(shunt_micro_ohms: u32, max_current_ma: u32) -> [u8; 2] {
    let current_lsb_na = (max_current_ma as u64 * 1000 * 1000) >> 15;
    let shuntcal = (819_200_000u64 * current_lsb_na * shunt_micro_ohms as u64)
                   / 1_000_000_000;
    (shuntcal.min(0x7FFF) as u16).to_be_bytes()
}

/// Parseur Current: dépend de SHUNT_CAL appliquée (current_lsb_na connu).
pub fn parse_current(raw: [u8; 2], current_lsb_na: u32) -> Milliamps {
    let word = i16::from_be_bytes(raw) as i32;
    let micro_amps = (word * current_lsb_na as i32) / 1000;
    Milliamps::from_raw(micro_amps / 1000)
}

/// Vérif device ID (INA237 = 0x2370, INA238 = 0x2380, etc.)
pub fn check_device_id(raw: [u8; 2]) -> Result<(), Error> {
    let id = u16::from_be_bytes(raw);
    match id & 0xFFF0 {
        0x2370 | 0x2380 | 0x2390 => Ok(()),
        _ => Err(Error::UnexpectedDeviceId(id)),
    }
}
```

Le wrapper bus-aware vit à côté :

```rust
pub struct Ina237<'b, B: I2cBus> {
    bus: &'b mut B,
    addr: u8,
    current_lsb_na: u32,
}

impl<'b, B: I2cBus> Ina237<'b, B> {
    pub fn init(bus: &'b mut B, addr: u8, shunt_micro_ohms: u32, max_current_ma: u32)
        -> Result<Self, Error>
    { /* init sequence: verify ID, reset, write ADC_CONFIG, write SHUNT_CAL */ }

    pub fn read_vbus(&mut self) -> Result<Millivolts, Error> { /* ... */ }
    pub fn read_current(&mut self) -> Result<Milliamps, Error> { /* ... */ }
}
```

**Note importante :** dans la version V2 utilisée à l'exécution (appelée depuis C avec le bloc `BmuRawInputs`), le core Rust **n'utilise pas** le wrapper bus-aware. Il appelle les fonctions pures `parse_vbus` etc. sur les bytes déjà lus. Le wrapper bus-aware est utilisé uniquement par les tests host et le petit binaire de diagnostic `cargo xtask probe-bus`.

**TCA9535** — mêmes principes. Parseurs purs `parse_inputs(raw: [u8; 2]) -> u16`, encodeur `encode_output(mask: u16) -> [u8; 3]`. **Le PCB v2 reversed switch mapping** du scaffold `firmware-rs/` actuel est préservé en commentaires + constants :

```rust
/// PCB BMU v2 : ordre des switchs inversé, LEDs appairées.
/// Canal 0 (bat1) = P0.3, canal 3 (bat4) = P0.0
pub const SWITCH_PIN: [u8; 4] = [3, 2, 1, 0];
pub const ALERT_PIN:  [u8; 4] = [7, 6, 5, 4];
```

**Bug potentiel identifié dans le scaffold actuel :** la fonction `all_off()` écrit `[OUTPUT_PORT0, 0x00, 0x00]` en supposant l'auto-increment du TCA9535 pour couvrir OUTPUT_PORT1. Le TCA9535 **ne supporte pas** l'auto-increment classique comme le TCA6424 (à vérifier contre datasheet SCPS209). La version V2 écrit explicitement les deux registres en deux transactions distinctes :

```rust
pub fn all_off(&mut self) -> Result<(), I2cError> {
    self.bus.write(self.addr, &[Reg::OutputPort0 as u8, 0x00])?;
    self.bus.write(self.addr, &[Reg::OutputPort1 as u8, 0x00])?;
    Ok(())
}
```

**AHT30** — parseur simple (T/RH en Q20 fixed-point). Driver ~30 lignes.

## 5. Protection state machine (F01-F11)

### 5.1 États et transitions

```
     ┌──────────┐  V/I OK   ┌──────────┐
     │ Unknown  ├──────────►│  Online  │
     └────┬─────┘           └────┬─────┘
          │                      │ V/I fault
          │ no INA at idx        ▼
          ▼                 ┌──────────┐  5e fault  ┌──────────┐
     ┌──────────┐           │ Offline  ├───────────►│ Latched  │
     │ Absent   │           │ (reason, │            │ (reason) │
     └──────────┘           │ deadline)│            └──────────┘
                            └────┬─────┘                  ▲
                                 │ delay 10 s              │
                                 ▼                         │
                            (retry Online)                 │
                                                           │
                            topology mismatch ─────────────┘
```

**5 états** (enum `BatteryState` dans `bmu-types`) :
- `Unknown` — au boot avant première mesure
- `Absent` — pas d'INA237 détecté à cette adresse
- `Online` — connecté, V/I OK
- `Offline { reason: OfflineReason, deadline_ms: u64 }` — coupé temporairement, retry après delay
- `Latched { reason: LatchReason }` — permanent, ne se débloque que par commande explicite `ResetLatch`

**Transitions = fonctions pures** `(state, measurement, now_ms, config) → (new_state, actions)`. Testables unitairement sans mock ni setup.

### 5.2 Mapping F01-F11 → crates

| Feature Kill_LIFE | Crate/module | Notes |
|---|---|---|
| F01 sampling V/I INA237 | `bmu-drivers::ina237::parse_*` | Parseurs purs, testés contre fixtures |
| F02 Ah counting | `bmu-protection::manager::coulomb_counter` | Intégration I × dt, wraparound safe |
| F03 under-voltage protection | `bmu-protection::state::check_uv` | `v < umin` |
| F04 over-voltage protection | `bmu-protection::state::check_ov` | `v > umax` |
| F05 over-current protection | `bmu-protection::state::check_oc` | `|i| > imax` |
| F06 imbalance vs fleet max | `bmu-protection::state::check_imbalance` | `fleet_max - v > vdiff` |
| F07 switch counter + reconnect delay | `bmu-protection::latch::tick` | |
| F08 permanent latch (5e fault) | `bmu-protection::latch::latch_if_maxed` | |
| F09 topology check | `bmu-protection::manager::topology_ok` | `n_tca * 4 == n_ina` |
| F10 **web interface (DROPPED)** | — | Remplacé par BLE Control + pairing SC + HMAC |
| F11 log serial | `task_bmu_core.c` | Logs ESP_LOGI, pas dans Rust |

### 5.3 Les 4 audit fixes CRIT — tous by-design

| Fix | Mécanisme | Test de régression |
|---|---|---|
| **CRIT-A** (mV vs V) | Types `Millivolts(i32)` et `Milliamps(i32)` non interchangeables, pas de `From<i32>` automatique | `test_crit_a_mv_volt_confusion` : assert que `check_uv(Millivolts::from_raw(25), Millivolts::from_raw(24000))` ne compile pas sans conversion explicite (test compile-fail trybuild) |
| **CRIT-B** (imbalance vs fleet max) | Fonction `manager::fleet_max_mv(&snapshot) -> Millivolts` calculée **une fois** sur le snapshot complet **avant** `check_imbalance`. Jamais un "max local glissant". | `test_crit_b_imbalance_uses_fleet_max` : 3 batteries, une à 27000 mV (max fleet), une à 25500 mV (écart 1500 > 1000 mV), doit être `Offline(Imbalance)` même si sa valeur locale est stable |
| **CRIT-C** (deadlock) | Core Rust **sans mutex**. État mutable possédé par l'appelant C via `BmuCore*`. Pas de réentrance possible. | `test_crit_c_no_reentrancy` : trigger une cmd depuis dans un callback tick → asserte que la cmd est queued, pas exécutée immédiatement |
| **CRIT-D** (auth web) | Web server **droppé**. Toutes les commandes write passent par BLE + pairing SC + HMAC-SHA256 signé + rate limit | `test_crit_d_unauth_cmd_rejected` : commande sans HMAC → refusée ; pairing non établi → write refusé par NimBLE (testé côté C en Unity) |

### 5.4 Config runtime (NVS override) — Q5 = A+C

- Les seuils défaut sont des `const` Rust matchant `specs/01_spec.md` : umin=24000 mV, umax=30000 mV, imax=1000 mA, vdiff=1000 mV, nb_switch_max=5, reconnect_delay=10000 ms.
- Au boot : C lit NVS namespace `bmu/config` → remplit `BmuConfigC` → `bmu_core_init(cfg)`.
- En runtime : BLE command `SetConfig` → `bmu_core_set_config(core, &new_cfg)`.
- **Validation bornes en dur côté Rust** (rejette si dangereux) :
  - `umin_mv >= 18000` (≥ 3V/cell × 6 cells)
  - `umax_mv <= 36000` (≤ 3.6V/cell × 10 cells, marge capacité Li-ion)
  - `umin_mv < umax_mv` (trivial)
  - `imax_ma <= 10000` (plage INA237)
  - `vdiff_imbalance_mv <= 5000`
  - `nb_switch_max` entre 1 et 20
  - `reconnect_delay_ms` entre 1000 et 600000
- Si validation OK : C persiste dans NVS après retour `bmu_core_set_config`.
- Si validation KO : commande rejetée, snapshot inchangé, log d'audit BLE.

## 6. R_int, balancer, SOH

### 6.1 `bmu-rint` — mesure résistance interne pulse-off

**Principe** : ouvrir le contacteur d'une batterie ~500 ms, mesurer V avant/pendant la coupure, comparer au courant en circulation juste avant. R = ΔV / I_before.

**Contrainte architecturale :** le core Rust est stateless côté bus et appelé à 5 Hz. Impossible d'orchestrer une séquence temporelle dans une seule invocation. Solution : **micro state machine Rust** qui produit des actions, consommée sur plusieurs ticks.

```rust
pub enum RintState {
    Idle,
    ArmRequested { idx: u8 },
    PulseStarted {
        idx: u8,
        v_before: Millivolts,
        i_before: Milliamps,
        start_tick: u32,
    },
    PulseMeasured { idx: u8, r_ohmic: Milliohms },
    Cooldown { idx: u8, deadline_tick: u32 },
}

pub struct RintEngine {
    state: RintState,
    results: [Option<RintResult>; 16],
}
```

Le `RintEngine::tick(&snapshot, tick_no)` retourne `Option<RintAction>` où `RintAction` est `OpenContact(u8)` | `CloseContact(u8)`. Le core Rust merge cette action dans le bitmask TCA9535 de l'Action globale, sauf si la protection veut forcer l'état inverse (protection wins).

**Safety guards** (tous dans `RintEngine::tick`) :
- Refuse de trigger si la batterie cible est déjà en `Offline` ou `Latched`
- Refuse si le snapshot global montre un déséquilibre actif (un autre fault en cours ailleurs)
- Une seule mesure active à la fois dans toute la fleet (singleton state machine)
- Cooldown 10 s minimum entre deux mesures sur la même batterie
- Trigger uniquement via commande explicite `TriggerRint(idx)` venant de BLE/UI

### 6.2 `bmu-balancer` — duty cycling scheduler

Héritage du `bmu_balancer` actuel mais rendu **data-driven et non-autoritaire** :

```rust
pub struct BalancerEngine {
    duty_target: [u8; 16],  // 0..255, calculé selon écart SoC
    cycle_pos: u8,           // position dans le cycle PWM software
}

impl BalancerEngine {
    pub fn tick(&mut self, snapshot: &Snapshot) -> u16 {
        let targets = self.compute_target_duty(snapshot);
        self.cycle_pos = self.cycle_pos.wrapping_add(1);
        let mut mask = 0u16;
        for i in 0..snapshot.n_bat {
            if targets[i as usize] > self.cycle_pos {
                mask |= 1 << i;
            }
        }
        mask
    }
}
```

**Règle fondamentale :** le balancer **n'est jamais autoritaire** sur les contacteurs. Les actions qu'il produit sont **AND-maskées** par la décision de protection dans `bmu-protection::merge_actions()` :
- `final_on_mask = balancer_mask & protection_allowed_mask`
- Une batterie en `Offline` ou `Latched` ne peut **jamais** être ON, même si le balancer le demande.

### 6.3 SOH — TFLite Micro reste en C++

**SOH est le seul composant critique qui ne sera pas en Rust.** Raison : `esp-tflite-micro` est C++, il n'existe pas de runtime TFLite pur Rust utilisable pour ESP32-S3 en V1. Wrapper Rust autour de `tflite::MicroInterpreter` possible mais imposerait `esp-idf-sys` dans le workspace → casse la testabilité host.

**Architecture SOH V2 :**

1. Modèle `.tflite` INT8 (~12 KB) embarqué dans le composant ESP-IDF `bmu_soh/` via `EMBED_FILES`. Fichier source : `firmware-idf/components/bmu_soh/models/fpnn_soh_v3_int8.tflite` (déjà présent dans le working tree restauré).
2. Task C `task_soh` sur APP_CPU, priorité 3, période 60 s.
3. Inputs : `bmu_core_get_cached_snapshot(core, &snap)` → extrait features (V, I, T, Ah cumul) par batterie.
4. Inférence : `tflite::MicroInterpreter` en C++, résultat = `soh_pct[16]`.
5. Retour au core : `bmu_core_command(core, BmuCmd::UpdateSoh { idx, soh_pct })`. Le core Rust stocke la valeur dans son manager et l'expose dans le snapshot suivant.
6. BLE : `task_ble` lit le snapshot (qui contient désormais `battery[i].soh_pct`) et publie sur la characteristic SOH.

**Avantage :** le Rust core ne connaît pas TFLite. Si demain un runtime Rust pur arrive (`burn`, `candle-lite`), on remplace le task C par un task Rust sans toucher au core.

**Inconvénient accepté :** la feature SOH perd la safety Rust, mais c'est une info diagnostique — pas critique pour la safety batterie.

## 7. BLE GATT (iOS app + Victron emulation)

**Stack : NimBLE ESP-IDF (C). Le core Rust ne touche jamais à NimBLE.** Toute interaction BLE passe par `bmu_core_get_cached_snapshot` (read) et `bmu_core_command` (write).

### 7.1 Services GATT

| Service UUID | Nom | Direction | Contenu |
|---|---|---|---|
| `F00D0001-5BF8-4B5A-95B9-B7C5B4D63A20` | **Battery** | Notify + Read | 16 characteristics (1 par batterie), 24 bytes chacun |
| `F00D0002-...` | **System** | Notify + Read | 1 char agrégée (FW, heap, uptime, Wi-Fi, MQTT, topology, tick p50/p99) |
| `F00D0003-...` | **Control** | Write + Write-with-response | Commandes signées HMAC |
| `F00D0004-...` | **Config** | Read + Write + Notify | Labels batteries, seuils runtime, device_name |
| `0x6597` + Victron | **SmartShunt emul** | Read | Characs standards Victron |

### 7.2 Layout characteristic `Battery[i]` (24 bytes big-endian packed)

Généré par `bmu_core_serialize_battery(core, idx, buf[24])`. Pas de serde/protobuf/JSON — pack manuel pour ABI stable.

```
offset  size   field
0       1      idx (0..15)
1       1      state (0=Unknown 1=Absent 2=Online 3=Offline 4=Latched)
2       1      state_reason (0=OK 1=UV 2=OV 3=OC 4=Imbalance 5=Topology 6=Manual)
3       1      switch_count (0..5)
4       4      voltage_mv (i32 BE)
8       4      current_ma (i32 BE)
12      4      ah_remaining_ma_h (i32 BE)
16      2      temp_c10 (i16 BE)
18      1      soh_pct (0..100, 0xFF=unknown)
19      1      balancer_duty_pct (0..100)
20      4      r_ohmic_m_ohms (u32 BE, 0xFFFFFFFF=not measured)
```

### 7.3 Characteristic `System` (64 bytes fixes)

`fw_version[16]`, `git_sha[8]`, `build_date[8]`, `uptime_s (u32)`, `heap_free (u32)`, `heap_low (u32)`, `wifi_state (u8)`, `ip_addr[4]`, `mqtt_state (u8)`, `topology_ok (u8)`, `n_ina (u8)`, `n_tca (u8)`, `core_tick_us_p50 (u16)`, `core_tick_us_p99 (u16)`, padding.

### 7.4 Commandes Control (format TLV signé)

Payload = `[cmd: u8][len: u8][data: len bytes][hmac: 8 bytes]`.

| Cmd | Len | Data | Effet |
|---|---|---|---|
| `0x01` ForceOff | 1 | battery idx | Latch immédiat (reversible par `ResetLatch`) |
| `0x02` ResetAh | 1 | battery idx | Remet à zéro le compteur Ah |
| `0x03` TriggerRint | 1 | battery idx | Arme une mesure R_int si `RintState::Idle` |
| `0x04` ResetLatch | 1 | battery idx | Si latched : remet en Unknown (log audit NVS) |
| `0x05` SetLabel | 1+N | idx + UTF-8 string ≤ 16 bytes | Persisté NVS namespace `bmu/labels` |
| `0x06` SetConfig | 24 | BmuConfigC packed | Validé Rust + persisté NVS `bmu/config` |
| `0x07` Reboot | 0 | — | `esp_restart()` après flush |
| `0x08` WifiProv | var | SSID len + SSID + PSK len + PSK | Provisioning Wi-Fi STA |
| `0x09` RequestSoh | 1 | battery idx | Force inférence TFLite hors cycle 60 s |
| `0x0A` AuthChallenge | 16 | nonce | First-pair handshake (voir 7.5) |

### 7.5 Sécurité commandes — mécanisme remplaçant l'auth web droppée (CRIT-D)

1. **First-pair** : à la première connexion, BLE pairing ESP32 `ESP_LE_AUTH_REQ_SC_ONLY` (LE Secure Connections obligatoire) en mode **Passkey Entry** : le BMU est `IO_CAP_DISPLAY_ONLY`, le central iOS est `IO_CAP_KEYBOARD_DISPLAY`. L'écran BOX-3 **affiche un passkey 6 digits aléatoire**, l'utilisateur le **saisit sur iOS**. Cette association est asymétrique mais bidirectionnellement authentifiée par le protocole SMP. Pairing OK → bonding (LTK + IRK) persisté en NVS namespace `bmu/ble_bonds`. Le passkey n'est jamais transmis sur l'air, seulement les confirmations cryptographiques. Capacité de bonding limitée à 4 devices (CONFIG_BT_NIMBLE_MAX_BONDS=4) ; le 5e déclenche éviction LRU.
2. **Command auth** : chaque write sur `Control` inclut HMAC-SHA256(key, cmd_bytes) tronqué à 8 bytes en fin de payload. La clé est dérivée du LTK (link key) du bonding, jamais exposée hors NimBLE.
3. **Rejet sans pairing** : writes sur `Control` refusées si `conn->sec_state.encrypted == false` (check dans le write callback).
4. **Rate limit** : max 10 commandes/s par connexion, drop sinon.
5. **Audit log** : chaque commande exécutée est loggée dans `/sdcard/audit.log` append-only : timestamp (µs monotonic + wallclock si NTP OK), cmd, idx, result code.

### 7.6 Victron SmartShunt emulation (service séparé, agrégation fleet)

Héritage direct du `bmu_ble_victron_gatt` actuel + spec `victron-ble-connect-design.md`. Objectif : VictronConnect app voit le BMU comme un SmartShunt virtuel exposant les valeurs **fleet agrégées** (pas par batterie).

- Service UUID Victron + characs standards (voltage, current, SoC, power, consumed Ah).
- Agrégation côté C dans `task_ble` :
  - `fleet_voltage_mv = moyenne(battery[i].voltage)` sur les batteries `Online`
  - `fleet_current_ma = somme(battery[i].current)`
  - `fleet_soc = moyenne pondérée(ah_remaining / ah_nominal)` (ah_nominal = config)
  - `fleet_power_mw = fleet_voltage × fleet_current / 1000`
- Manufacturer data advertising payload compact 24 bytes au format **Victron Instant Readout** (AES-CTR avec clé projet) permettant lecture sans connexion.
- **Pas de scan/read de balises Victron tierces** (droppé Q8).

## 8. Wi-Fi, MQTT publish, SD log + replay

### 8.1 Task `task_wifi_mqtt` (C, APP_CPU, prio 4)

Boucle principale :
```
init → nvs_read(wifi_creds)
     → if has_creds: wifi_start_sta; else: wifi_off
     → wait event IP_GOT
     → mqtt_client_start(kxkm-ai:1883, user/pass NVS)
     → wait event MQTT_CONNECTED
     → loop:
         - xQueueReceive(q_cloud, &snap, 10s)
         - if snap: publish_telemetry(&snap)
         - if !connected: sd_log_append(&snap)
         - every 60s: sd_log_replay_chunk() if connected
```

### 8.2 Topic et payload (Q7 = A + A)

**Topic :** `bmu/<device_id>/telemetry` où `device_id = 12 hex chars` des 6 octets bas de la MAC. Exemple : `bmu/a4c137f2b1e8/telemetry`.

**Payload JSON compact** (1 msg par tick de telemetry, période 10 s configurable NVS) :

```json
{
  "ts": 1712683200123,
  "fw": "2.0.0+abc1234",
  "up": 12345,
  "topo": {"ina": 16, "tca": 4, "ok": true},
  "fleet": {"v": 25432, "i": -1200, "soc": 78, "temp": 221},
  "bats": [
    {"i": 0, "v": 25432, "c": -75, "s": 2, "sc": 0, "ah": 4500, "r": 4, "soh": 92, "dut": 50},
    {"i": 1, "v": 25418, "c": -75, "s": 2, "sc": 0, "ah": 4510, "r": 5, "soh": 91, "dut": 50}
  ]
}
```

**Clés courtes intentionnelles** (v/c/s/sc/ah/r/soh/dut) → économie ~35% vs noms longs. Un mapping `docs/telemetry-schema.yaml` documente chaque clé pour les consumers cloud.

**Taille estimée :** ~1.2 KB / message × 1 msg / 10 s = **600 KB/h par BMU**. Largement tenable sur le broker KXKM-AI.

**QoS :** 0 pour telemetry live (préférer perte à blocage). QoS 1 pour alertes critiques sur topic séparé `bmu/<id>/events` (latch permanent, topology mismatch, boot).

### 8.3 SD log format et rolling

- Répertoire `/sdcard/bmulog/` sur FAT partition 11 MB + SD physique 32 GB (SPI).
- Fichiers rolling par 10 MB : `bmu-YYYYMMDD-HHMMSS.lp`
- Format **line protocol InfluxDB** (Q7 = line protocol) :
  ```
  bmu_telemetry,device=a4c137f2b1e8,bat=0 v=25432i,c=-75i,state="Online",soh=92i 1712683200123000000
  ```
- **Rolling 1 GB total** (Q7) : à chaque fermeture de fichier, `task_sd_replay` vérifie la taille totale `/sdcard/bmulog/` et supprime les plus vieux jusqu'à ≤ 1 GB.
- **Naming `-NOSYNC.lp`** : si un message est écrit alors que MQTT jamais reçu, le fichier courant est suffixé `-NOSYNC`. Quand le replay a drainé le fichier, rename → `.lp` (archive).

### 8.4 SD log replay policy

- MQTT connecté + q_cloud vide → `task_sd_replay` publie 100 lignes (chunk) du plus vieux `-NOSYNC.lp` sur topic `bmu/<id>/replay` (distinct du live).
- QoS 1 sur replay, attente ACK broker → avance offset dans sidecar `.lp.cursor`.
- Fichier entièrement publié → rename `-NOSYNC.lp` → `.lp`.
- **Rate limit replay** : max 10 KB/s pour pas écraser le live.
- Topic séparé `replay` vs `telemetry` permet au consumer cloud de dédupliquer/horodater différemment.

### 8.5 Wi-Fi credentials & provisioning

- Stockage NVS namespace `bmu/wifi`, clés `ssid` (≤ 32 bytes) et `psk` (≤ 64 bytes).
- **Provisioning initial** : commande BLE `WifiProv` uniquement (Q8 = pas de SoftAP, pas de web).
- Aucun cred NVS au boot → Wi-Fi éteint, seul BLE actif → iOS app doit provisionner.
- Retry STA : 3 tentatives avec backoff 1s, 5s, 25s. Après 3 échecs → Wi-Fi off pendant 5 min → retry. **Pas de fallback AP.**

## 9. LVGL display (5 tabs BOX-3)

**Stack : LVGL 9.1 + `esp-box-3` BSP (C).** Héritage de la spec `lvgl-ui-refonte-design.md`. Task `task_display` (C, APP_CPU, prio 3) lit le cached snapshot via `bmu_core_get_cached_snapshot()` une fois par frame (200 ms).

### 9.1 Les 5 tabs

| # | Nom | Contenu |
|---|---|---|
| 1 | **BATT** | Grille 4×4 de cellules batterie avec V/I/state colorisé. Touch → overlay détail (lv_chart 5 min V/I, R_int, SoH, Ah) |
| 2 | **SOH** | Barres SOH % par batterie + âge Ah cumul |
| 3 | **SYS** | Uptime, heap, Wi-Fi state, MQTT state, topology, IP, core_tick p50/p99 |
| 4 | **CLIMATE** | T° et humidité AHT30, graph 30 min |
| 5 | **CONFIG** | Seuils courants (read-only V1), device_name, FW version |

### 9.2 Grille BATT (tab 1)

```
┌────────────────────────────── 320 px ──────────────────────────────┐
│ BATT  SOH  SYS  CLIM  CFG              ⚡ 25.4V  -1.2A  76%  ⚠︎0 │  ← bar+hdr 40 px
├────────────────────────────────────────────────────────────────────┤
│ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐                                │
│ │ B0   │ │ B1   │ │ B2   │ │ B3   │   ← 4 cellules 70×80 px       │
│ │24.98V│ │25.10V│ │25.00V│ │ LATC │     vert = Online              │
│ │-75mA │ │-80mA │ │-78mA │ │      │     jaune = Offline            │
│ │92%   │ │91%   │ │93%   │ │  OV  │     rouge = Latched            │
│ └──────┘ └──────┘ └──────┘ └──────┘     gris = Absent              │
│ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐                                │
│ │ B4   │ │ B5   │ │ B6   │ │ B7   │                                │
│ │24.95V│ │ ──── │ │24.99V│ │25.02V│                                │
│ │-75mA │ │absent│ │-74mA │ │-76mA │                                │
│ │92%   │ │      │ │93%   │ │92%   │                                │
│ └──────┘ └──────┘ └──────┘ └──────┘                                │
│ (4 autres cellules rangée 3+4)                                      │
│ Touch sur cellule → overlay détail V/I 5 min + R_int + SoH + Ah   │
└────────────────────────────────────────────────────────────────────┘
```

### 9.3 Refresh, thème, découplage

- **Cadences distinctes data vs render** :
  - **task_display lit le snapshot via `bmu_core_get_cached_snapshot()` à 5 Hz** (période 200 ms = même rate que `task_bmu_core`). C'est la fréquence de **mise à jour des données affichées**.
  - **LVGL display flush hardware tourne à 20 Hz** (`esp_timer` LVGL tick à 2 ms, `lv_timer_handler` cadence interne LVGL). C'est la fréquence de **redessin pixel** : nécessaire pour la fluidité des animations swipe et du scrolling tactile, indépendante du rate de données.
  - **Overlay détail (lv_chart V/I 5 min) : 5 Hz comme le reste** — l'« 10 Hz » mentionné précédemment était une erreur. Les samples du chart sont ajoutés à 5 Hz (1 sample par tick core), le rendu LVGL reste à 20 Hz pour la fluidité de scroll. 1 sample / 200 ms × 60 s × 5 min = 1500 points dans l'historique chart, largement suffisant pour la lecture humaine.
- Thème **dark high-contrast**, police `montserrat_14`.
- **Pas de touch control pour config en V1.** La tab CONFIG est read-only. Modifications de seuils par BLE + iOS app uniquement.
- **Aucune allocation LVGL runtime après init** : tous les widgets statiques, `lv_label_set_text_fmt` / `lv_led_set_color` / `lv_chart_set_next_value` uniquement.
- **Memory budget** : LVGL buffer double 320×240×2×2 = ~300 KB en PSRAM. `CONFIG_LV_MEM_SIZE_KILOBYTES=64` (config Kconfig à fixer dans `sdkconfig.defaults` du nouveau projet `firmware-idf-v2/`, valeur reprise du firmware v1 archivé). Grille 16 cellules + overlays ≈ 50 KB objets statiques alloués au boot.

## 10. Tests — pyramide host-first

```
                    ┌─────────────────────┐
                    │  HIL hardware       │  ← TB01-TB13 Kill_LIFE
                    │  manuel + scripté   │     bench réel
                    └─────────────────────┘
                  ┌───────────────────────────┐
                  │ Integration ESP-IDF host  │  ← Unity C-only
                  │ Unity                     │     ~8 suites gardées
                  └───────────────────────────┘
              ┌─────────────────────────────────────┐
              │ cargo test (unit + integration)    │  ← CŒUR
              │ Mac + Linux CI, ZÉRO hardware      │     ~300 tests
              └─────────────────────────────────────┘
        ┌───────────────────────────────────────────────┐
        │ cargo test --doc + cargo clippy + cargo fmt  │  ← lint
        └───────────────────────────────────────────────┘
```

### 10.1 Niveau 1 — `cargo test` (host Mac + Linux CI)

Toutes les crates `bmu-types`, `bmu-i2c`, `bmu-drivers`, `bmu-protection`, `bmu-rint`, `bmu-balancer` testées sur `aarch64-apple-darwin` (Mac M-series), `x86_64-apple-darwin` (Mac Intel), `x86_64-unknown-linux-gnu` (CI).

**Stratégie par crate :**
- `bmu-types` → tests arithmétique mV/mA, sérialisation packed, edge cases overflow
- `bmu-i2c` → tests du `MockBus` lui-même
- `bmu-drivers` → **golden file tests** : fixtures binaires captées du hardware v1 archivé, asserts sur valeurs attendues. ~50 fixtures par driver (bus voltage, current, ID, temp, edge cases)
- `bmu-protection` → tests state machine explicites pour chaque transition + tests régression CRIT-A/B/C/D dédiés
- `bmu-rint` → tests micro state machine (Idle → Arm → PulseStarted → Measured → Cooldown) avec snapshots pré-cuits
- `bmu-balancer` → tests duty cycling 3/2, AND-mask avec protection

**Property tests via `proptest`** sur les invariants critiques :
- "Une batterie en `Latched` ne peut pas redevenir `Online` sans commande `ResetLatch`."
- "La somme `tca_set_mask ∧ tca_clr_mask` est toujours 0 (jamais ON et OFF en même temps)."
- "Le compteur de switchs ne décroît jamais."
- "Si `topology_ok == false`, toutes les batteries finissent en `Offline(reason=Topology)` au tick suivant."

**Cible quantitative :** ≥ 300 tests Rust, durée ≤ 20 s. Tournent sur chaque commit local (git pre-commit hook optionnel) + CI sur chaque push.

### 10.2 Niveau 2 — cross-compile check

`cargo test --no-run --target xtensa-esp32s3-none-elf -p bmu-core` : compile-check sans exécution, garantit que les `cfg(target_arch = "xtensa")` et l'absence de `std` tiennent. CI Linux avec espup installé.

### 10.3 Niveau 3 — Integration ESP-IDF host Unity (existant, filtré)

Les 14 suites Unity actuelles sous `firmware-idf/test/` sont **filtrées** :

| Suite | V2 ? | Motif |
|---|---|---|
| `test_protection` | ❌ | Portée en `cargo test` (Rust fait mieux) |
| `test_balancer_logic` | ❌ | Portée en `cargo test` |
| `test_rint` | ❌ | Portée en `cargo test` |
| `test_health_score` | ❌ | Portée en `cargo test` |
| `test_snapshot` | ❌ | Portée en `cargo test` |
| `test_i2c_bitbang` | ❌ | Feature droppée V1 |
| `test_ble_victron` | ✅ | C-only (encoding payload advertising) |
| `test_victron_gatt` | ✅ | C-only (encoding GATT Victron) |
| `test_victron_scan` | 🗄 | Archivé (feature droppée V1) |
| `test_vedirect_parser` | 🗄 | Archivé (feature droppée V1) |
| `test_ble_soh` | ✅ | C-only (intégration SOH-BLE) |
| `test_config_labels` | ✅ | C-only (NVS labels) |
| `test_vrm_topics` | 🗄 | Archivé (feature droppée V1) |
| `test_climate` (nouveau) | ✅ | À créer (AHT30 driver C wrapper) |

Les suites archivées (🗄) vont dans **`firmware-idf-v2/test/_archive/`** — copiées depuis le firmware v1 archivé (`archive/firmware-v1` branch) au moment du scaffold étape 10. Pas supprimées, servent de référence si on remet les features en V2. Le dossier `firmware-idf/test/` (référence v1 restaurée) reste intact mais n'est ni buildé ni testé dans le pipeline V2.

### 10.4 Niveau 4 — HIL hardware (Kill_LIFE TB01-TB13)

Procédures dans `specs/04_validation.md` (271 lignes) — **gardées intactes**.

Nouveau harness Python `tests/hil/` qui automatise les TB qui peuvent l'être :
- `tb01_boot_sequence.py` — flash + monitor série, attend les logs (`bmu_core_init OK`, `topology=16/4`, `wifi_state=connected`)
- `tb02_voltage_calibration.py` — interroge MQTT topic + compare à multimètre USB Keysight
- `tb03_current_calibration.py` — idem avec charge programmable
- TB04-TB10 (logique protection/latch/reconnect) — lecture MQTT automatique, rapport pass/fail, intervention humaine pour actions physiques
- `make hil-validate` lance toutes les procédures, génère `docs/superpowers/validation/hil-report-YYYYMMDD.pdf`
- **HIL = blocker gate S3 Kill_LIFE.**

### 10.5 Couverture cible

Pas d'objectif % bloquant. Objectif **fonctionnel** : chaque feature F01-F11 a ≥ 1 test host + ≥ 1 test HIL. Mesure via `cargo llvm-cov --workspace` pour suivi, non-bloquante merge.

## 11. Build system

### 11.1 Layout repo final

```
KXKM_Batterie_Parallelator/
├── firmware-rust/                  # NOUVEAU workspace Rust (remplace firmware-rs/)
│   └── (voir section 3.1)
├── firmware-rs/                    # ANCIEN scaffold — supprimé après migration
├── firmware-idf/                   # RESTAURÉ depuis HEAD, servira de référence
│   └── (garde en lecture, pas de build V2 depuis ici)
├── firmware-idf-v2/                # NOUVEAU projet ESP-IDF V2 (wrap Rust core)
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   ├── partitions.csv              # 2×OTA 2MB + SPIFFS 1MB + FAT 11MB
│   ├── main/
│   │   ├── main.cpp                # init chain claire ~150 lignes
│   │   ├── task_bmu_core.cpp       # tick 5 Hz, appel Rust
│   │   ├── task_ble.cpp            # NimBLE services
│   │   ├── task_wifi_mqtt.cpp
│   │   ├── task_display.cpp        # LVGL 5 tabs
│   │   ├── task_climate.cpp
│   │   ├── task_soh.cpp            # TFLite Micro
│   │   ├── task_sd_replay.cpp
│   │   └── CMakeLists.txt
│   ├── components/
│   │   ├── bmu_core_rs/            # wrapper du staticlib Rust via corrosion
│   │   │   ├── CMakeLists.txt      # corrosion_import_crate(...)
│   │   │   └── include/bmu_core.h  # vendoré via xtask vendor-header
│   │   ├── bmu_i2c_glue/           # impl C de l'accès I²C
│   │   │   ├── CMakeLists.txt
│   │   │   ├── include/bmu_i2c_glue.h
│   │   │   └── src/bmu_i2c_glue.c  # esp-idf i2c_master + recover sequence
│   │   ├── bmu_ble/                # NimBLE 4 services + Victron emul
│   │   ├── bmu_wifi/
│   │   ├── bmu_mqtt/
│   │   ├── bmu_sd_log/             # remplace bmu_influx + bmu_storage
│   │   ├── bmu_display/            # LVGL 5 tabs
│   │   ├── bmu_soh/                # TFLite Micro C++ wrapper
│   │   ├── bmu_climate/            # AHT30 driver C
│   │   └── bmu_config/             # NVS bridge
│   └── test/                       # Unity C-only (8 suites)
├── docs/superpowers/specs/
│   └── 2026-04-09-bmu-rust-hybrid-v2-design.md    # CE FICHIER
└── docs/superpowers/plans/
    └── 2026-04-09-bmu-rust-hybrid-v2-plan.md      # Produit par writing-plans
```

**Note importante sur le nommage :** on ne modifie pas `firmware-idf/` (il reste référence v1 restaurée), on crée `firmware-idf-v2/`. Après validation finale et merge en `main`, `firmware-idf/` est supprimé et `firmware-idf-v2/` renommé en `firmware-idf/` dans un commit dédié. Pareil pour `firmware-rs/` → supprimé, `firmware-rust/` reste.

### 11.2 `firmware-rust/Cargo.toml` racine

```toml
[workspace]
resolver = "2"
members = [
    "crates/bmu-types",
    "crates/bmu-i2c",
    "crates/bmu-drivers",
    "crates/bmu-protection",
    "crates/bmu-rint",
    "crates/bmu-balancer",
    "crates/bmu-core",
    "crates/bmu-test-fixtures",
    "xtask",
]

[workspace.package]
edition = "2021"
license = "Proprietary"

[workspace.lints.rust]
unsafe_op_in_unsafe_fn = "deny"
unused = "warn"
missing_docs = "warn"

[workspace.lints.clippy]
all = "deny"
pedantic = "warn"
panic = "deny"
unwrap_used = "deny"
expect_used = "deny"
indexing_slicing = "warn"
arithmetic_side_effects = "warn"

[profile.release]
opt-level = "z"
lto = "fat"
codegen-units = 1
panic = "abort"
strip = "symbols"
overflow-checks = false

[profile.dev]
opt-level = 1
overflow-checks = true
```

### 11.3 Intégration corrosion côté ESP-IDF

`firmware-idf-v2/components/bmu_core_rs/CMakeLists.txt` :

```cmake
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES esp_common
)

include(FetchContent)
FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG v0.6.0
)
FetchContent_MakeAvailable(Corrosion)

corrosion_import_crate(
    MANIFEST_PATH "${CMAKE_SOURCE_DIR}/../../firmware-rust/Cargo.toml"
    CRATES bmu-core
    PROFILE release
    LOCKED
)

corrosion_set_env_vars(bmu-core
    CARGO_BUILD_TARGET=xtensa-esp32s3-none-elf
)

target_link_libraries(${COMPONENT_LIB} INTERFACE bmu-core-static)
```

### 11.4 Reproductibilité

- `Cargo.lock` commit dans le repo, `--locked` en CI
- ESP-IDF pinned dans `idf_component.yml` : `idf: ">=5.4,<5.5"`
- Toolchain Rust pinned dans `rust-toolchain.toml` (channel "esp")
- SHA256 du modèle TFLite documenté dans `firmware-idf-v2/components/bmu_soh/models/README.md`

## 12. Migration step-by-step (21 étapes)

**Principe : pas de big bang.** Tout sur la branche `feat/rust-hybrid-v2`, firmware v1 intact sur `archive/firmware-v1` et flashable à tout moment comme référence comportementale.

| # | Étape | Critère de fin | Risk |
|---|---|---|---|
| **0** | Archive + restore done. Tag `v1-final-archive`, branche `archive/firmware-v1`. `firmware-idf/` + `firmware/` restaurés dans le working tree. | Tag et branche existent (local). Working tree propre sur `feat/rust-hybrid-v2`. | nul |
| **1** | Scaffold workspace `firmware-rust/`. 9 crates vides avec `Cargo.toml`, `rust-toolchain.toml`, `xtask`. CI GitHub Actions `cargo test + clippy + fmt`. Supprimer `firmware-rs/`. | `cargo test --workspace` passe (0 tests). CI verte. | nul |
| **2** | `bmu-types` : Millivolts, Milliamps, Milliohms, Snapshot, Action, Command, Error. Tests arithmétique + edge cases. | 30+ tests passent sur Mac. Aucune dep ESP-IDF. | bas |
| **3** | `bmu-i2c` : trait I2cBus + MockBus. Tests du MockBus. | 10+ tests passent. | bas |
| **4** | `bmu-drivers` INA237 : parseurs purs + wrapper bus-aware + golden tests. Capture fixtures depuis firmware v1 via binaire debug (étape 4a). | 50+ tests golden passent. | bas |
| **5** | `bmu-drivers` TCA9535 + AHT30 : même stratégie. **Inclut fix `all_off()` 2 transactions séparées.** | 50+ tests supplémentaires. | bas |
| **6** | `bmu-protection` : state machine F03-F09, battery_manager Ah counting, fleet aggregates, latch. **Tests régression CRIT-A/B/C/D inclus dès le départ (red avant green).** Property tests proptest. | 100+ tests passent. Fuzz tient 5 min sans crash. | moyen |
| **7** | `bmu-rint` + `bmu-balancer` : state machines respectives. | 50+ tests supplémentaires. | bas |
| **8** | `bmu-core` façade : extern "C" wrappers, cbindgen → header, `xtask vendor-header`. Programme C minimal `xtask abi-check` compile et exécute. | `bmu_core.h` généré + vendoré. ABI check passe. | moyen |
| **9** | Cross-compile : `cargo build --target xtensa-esp32s3-none-elf --release -p bmu-core`. Vérifier taille staticlib < 500 KB. CI matrix Mac + Linux + xtensa. | Staticlib produit. CI matrix verte. | moyen |
| **10** | `firmware-idf-v2/` reconstitué from scratch : `idf.py create-project`, `partitions.csv`, `sdkconfig.defaults` minimaliste. **Pas encore de Rust intégré.** `main.cpp` boot + splash LVGL. | `idf.py build` passe. Flash + monitor → splash visible. | bas |
| **11** | Intégration `bmu_core_rs` via corrosion. `main.cpp` appelle `bmu_core_init` + logue snapshot vide 1 Hz. **Aucune lecture I²C réelle**, `BmuRawInputs` factice. | Boot OK, log "tick OK n_bat=0". Flash < 60% budget OTA. | élevé (frontière FFI) |
| **12** | `bmu_i2c_glue` C + wrapper Rust `EspIdfBus` hors workspace. Lecture réelle I²C, scan d'adresses, INA237 et TCA9535 live. Snapshot Rust contient vraies tensions. | Bench : `bmu_core_tick` produit snapshot avec V/I corrects. Comparaison côte à côte avec firmware v1 (archive). | élevé |
| **13** | `task_bmu_core` ordonnancement : pin PRO_CPU, period 200 ms, WDT 3 s. Politique retry I²C + bus recovery. | Stabilité 1 h sans WDT sur bench. | élevé |
| **14** | `bmu_climate` + `bmu_soh` + `bmu_sd_log` (3 composants parallélisables, indépendants). SOH lit cached snapshot via `bmu_core_get_cached_snapshot`. | 3 tests intégration manuels. SD log produit fichiers `.lp`. | moyen |
| **15** | `bmu_wifi` + `bmu_mqtt` + SD replay. Provisioning Wi-Fi via `nvs_write` en dur pour cette étape. MQTT publish + replay testés contre `kxkm-ai:1883`. | Telemetry visible dans broker. Replay relance après coupure réseau. | moyen |
| **16** | `bmu_display` LVGL : tab BATT en premier (critique), puis SOH/SYS/CLIMATE/CONFIG. Lecture snapshot only, pas de write. | Affichage cohérent avec MQTT. Swipe fluide. | moyen |
| **17** | `bmu_ble` services Battery + System + Config (**read-only**) : iOS app voit les snapshots. Pas encore de Control. | iOS app affiche 16 batteries en temps réel via BLE. | moyen |
| **18** | `bmu_ble` Control + auth pairing SC + HMAC. Audit log SD. | iOS peut envoyer ForceOff/ResetAh/TriggerRint + effet < 1 s. Audit log SD contient entrées. | élevé (sécurité) |
| **19** | Wi-Fi provisioning via BLE `WifiProv`. NVS écrit, reboot, Wi-Fi reconnecte. | Fresh device + iOS app → Wi-Fi configuré sans serial. | moyen |
| **20** | Victron SmartShunt emulation : service GATT 0x6597 + advertising Instant Readout. | VictronConnect voit le BMU comme SmartShunt avec V/I/SoC fleet. | moyen |
| **21** | HIL TB01-TB13 + bench longue durée 72 h. Rapport PDF. Merge → `main`. | Rapport HIL signé, bench propre, PR mergée. | élevé |

### 12.1 Garde-fous globaux

- Commits atomiques conventional (`feat(bmu-protection): ...`, `feat(bmu-core-rs): ...`)
- Branche jamais force-pushée
- Logs binaires monitor série archivés dans `docs/superpowers/validation/runs/YYYY-MM-DD-step-N.log` aux étapes hardware (12, 13, 15, 21)
- CI `cargo test` sur chaque commit, rouge = blocage merge
- `archive/firmware-v1` reste flashable en permanence via `git checkout archive/firmware-v1 && cd firmware-idf && idf.py build flash`
- Pas de modif hardware (PCB v2 stable, BOM gelé)
- Rollback plan : découverte problème fond étape 19-21 → retag, revient à `archive/firmware-v1` pour prod, itération sur branche sans pression

## 13. Ce que remplace / invalide ce spec

**Specs superpowers pré-existants partiellement ou totalement remplacés par ce document** (ils restent archivés mais ne sont plus la référence pour V1) :

| Spec pré-existant | Statut V1 |
|---|---|
| `2026-03-30-rust-hybrid-firmware-design.md` | Remplacé intégralement |
| `2026-03-30-esp-idf-migration-design.md` | Obsolète (on quitte ESP-IDF C/C++) |
| `2026-04-08-rtos-architecture-design.md` | Partiellement intégré (fan-out pattern), le reste obsolète |
| `2026-04-08-ios-firmware-coherence-design.md` | Intégré section 7 (GATT services) |
| `2026-04-07-victron-ble-connect-design.md` | Intégré section 7.6 (SmartShunt emul only, scan tiers droppé) |
| `2026-04-04-soh-llm-operational-design.md` | Partiellement intégré (SOH TFLite only, LLM hors scope V1) |
| `2026-04-03-rint-measurement-design.md` | Intégré section 6.1 |
| `2026-04-02-victron-integration-design.md` | Obsolète (VE.Direct et VRM droppés) |
| `2026-04-02-dual-i2c-32bat-design.md` | Reporté V2 |
| `2026-04-01-lvgl-ui-refonte-design.md` | Intégré section 9 |
| `2026-04-01-smartphone-app-design.md` | Consommateur, hors scope firmware V1 |
| `2026-03-31-phase9-ble.md` | Intégré section 7.1-7.5 |
| `2026-03-31-phase8-display-enhanced.md` | Intégré section 9.2 (touch-to-detail) |
| `2026-03-30-phase7-vedirect-victron.md` | Obsolète (VE.Direct droppé) |
| `2026-03-30-phase6-display-dashboard.md` | Intégré section 9 |

**Plans superpowers pré-existants invalidés** : les 31 plans sous `docs/superpowers/plans/` sont tous en `pending` (aucune checkbox cochée). Ils sont **tous invalidés** par ce spec et seront remplacés par un unique `2026-04-09-bmu-rust-hybrid-v2-plan.md` généré par writing-plans. Les plans obsolètes ne sont **pas supprimés** — ils vont dans `docs/superpowers/plans/_archive/` avec un `README.md` expliquant le pourquoi.

## 14. Questions ouvertes / décisions différées

| Point | Statut | Décision à prendre quand |
|---|---|---|
| Push `v1-final-archive` et `archive/firmware-v1` sur le remote | local pour l'instant | Avant étape 21 (merge) |
| Format binaire du cache NVS pour labels batteries | packed u8 len + bytes | Étape 18 |
| Longueur minimale PIN pairing BLE | 6 digits (default NimBLE) | Étape 18 |
| Clé AES projet pour Victron Instant Readout | à générer et commiter dans `firmware-idf-v2/components/bmu_ble/secrets.h.example` | Étape 20 |
| Cadence publication `fleet` aggregates dans telemetry JSON | par défaut tous les messages | Étape 15 |
| Thème LVGL (couleurs exactes) | héritage spec `lvgl-ui-refonte-design.md` | Étape 16 |
| Ordre exact des tabs BATT/SOH/SYS/CLIMATE/CONFIG | BATT en premier (critique), ordre des 4 autres TBD | Étape 16 |

Ces points ne bloquent pas l'approbation du spec : ils sont marqués pour résolution en cours d'exécution.
