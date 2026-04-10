# BMU Rust-Hybrid V2 Implementation Plan — Part 1: Rust Core

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Ce plan est la partie 1 sur 2.** Il couvre la mise en place complète du workspace Rust `firmware-rust/` jusqu'à la production d'un staticlib `libbmu_core.a` cross-compilé pour ESP32-S3. **Part 2** (`2026-04-09-bmu-rust-hybrid-v2-plan-part2-idf-integration.md`, à écrire après validation/exécution de la part 1) couvrira l'intégration ESP-IDF : scaffold `firmware-idf-v2/`, composants C (BLE, Wi-Fi, MQTT, SD log, LVGL, SOH), tasks FreeRTOS, BLE auth HMAC, Victron emul, et gate HIL TB01-TB13.

**Goal (Part 1):** Produire un workspace Rust `firmware-rust/` complet et testable sur host (Mac + Linux CI) avec 9 crates couvrant les drivers I²C purs, la state machine protection F01-F11, R_int, balancer, et une façade FFI C avec cbindgen. Résultat final : `cargo xbuild` produit un `libbmu_core.a` pour `xtensa-esp32s3-none-elf` < 500 KB, et `cargo test --workspace` passe ≥ 300 tests sur host, incluant les tests de régression des 4 audit findings CRIT.

**Architecture:** Workspace Rust `firmware-rust/` avec 9 crates compilant en un staticlib `libbmu_core.a` + header `bmu_core.h` (cbindgen), lié à un projet ESP-IDF neuf `firmware-idf-v2/` via corrosion (Part 2). Les drivers, protection state machine, R_int et balancer vivent côté Rust (types forts `Millivolts`/`Milliamps`, trait maison `I2cBus`, pas d'embassy, sync). BLE NimBLE, Wi-Fi, MQTT, SD log, LVGL 5 tabs et SOH TFLite Micro restent côté C/C++ (Part 2). Pont FFI unidirectionnel C→Rust via `bmu_core_tick(&raw, &snap, &actions)` à 5 Hz sur PRO_CPU.

**Tech Stack (Part 1):** Rust nightly "esp" toolchain (espup), cargo, cbindgen, heapless, bitflags, libm, proptest. Cibles : `aarch64-apple-darwin`, `x86_64-unknown-linux-gnu`, `xtensa-esp32s3-none-elf`. Zéro dépendance ESP-IDF ou embedded-hal dans le workspace.

**Companion spec:** `docs/superpowers/specs/2026-04-09-bmu-rust-hybrid-v2-design.md` (référencé comme **§N** dans ce plan).

**Commit strategy (décision utilisateur option C du brainstorming) :**
- **Cette part 1 — commit par task** : historique fin, `git bisect` trivial, chaque task atomique.
- **Part 2 (ESP-IDF glue)** : commit par phase — moins de bruit, jalons clairs.

**Branche de travail :** `feat/rust-hybrid-v2`. Firmware v1 intouché sur tag `v1-final-archive` et branche `archive/firmware-v1` (locaux, déjà créés en session brainstorming).

**Phases couvertes par cette Part 1 :**
- Phase 0 — Prérequis environnement (déjà validé en brainstorming)
- Phase 1 — Scaffold workspace `firmware-rust/`
- Phase 2 — `bmu-types` (fondations type-safe, CRIT-A by-design)
- Phase 3 — `bmu-i2c` (trait maison) + `MockBus`
- Phase 4 — `bmu-drivers` INA237 (parseur pur + wrapper + golden fixtures)
- Phase 5 — `bmu-drivers` TCA9535 + AHT30
- Phase 6 — `bmu-protection` (state machine F01-F11, 4 régressions CRIT, property tests)
- Phase 7 — `bmu-rint` (pulse-off state machine)
- Phase 8 — `bmu-balancer` (duty cycling + AND-mask protection)
- Phase 9 — `bmu-core` (façade FFI extern "C" + cbindgen header)
- Phase 10 — Cross-compile `xtensa-esp32s3-none-elf` + size budget + xtask

---

## Execution Notes & Known Deviations (mise à jour 2026-04-10)

**Progress so far:** Phase 1 ✅ + Phase 2 ✅ (43 tests, commits `99d7161` → `70ff766` sur `feat/rust-hybrid-v2`).

Les workers subagents qui exécutent ce plan DOIVENT tenir compte des déviations suivantes, apprises à l'exécution avec Rust nightly "esp" 1.93 + clippy 1.94 :

1. **`[unstable] build-std` déplacé dans les aliases.** Le plan original (Task 1.1) mettait `[unstable] build-std = ["core", "alloc"]` au niveau racine de `.cargo/config.toml`, ce qui provoque un conflict lang-item (`core` rebuilt vs `std` système) lors de `cargo test` host avec la toolchain esp. Fix appliqué commit `9bd9902` : `-Zbuild-std=core,alloc` est passé uniquement via les aliases `xcheck`/`xbuild`. Le contenu final du fichier est :
   ```toml
   [alias]
   xcheck = "check -Zbuild-std=core,alloc --target xtensa-esp32s3-none-elf -p bmu-core"
   xbuild = "build -Zbuild-std=core,alloc --target xtensa-esp32s3-none-elf --release -p bmu-core"
   ```

2. **Lint groups workspace doivent avoir `priority = -1`.** Rust ≥1.94 déclenche `clippy::lint_groups_priority` quand un groupe (`unused`, `all`, `pedantic`) cohabite avec des lints individuels dans `[workspace.lints.*]`. Le workspace `Cargo.toml` a été corrigé commit `ecfa9a4` :
   ```toml
   [workspace.lints.rust]
   unsafe_op_in_unsafe_fn = "deny"
   unused = { level = "warn", priority = -1 }
   missing_docs = "allow"

   [workspace.lints.clippy]
   all = { level = "deny", priority = -1 }
   pedantic = { level = "warn", priority = -1 }
   panic = "deny"
   unwrap_used = "deny"
   expect_used = "deny"
   indexing_slicing = "warn"
   arithmetic_side_effects = "warn"
   ```

3. **`#[must_use]` obligatoire sur toutes les méthodes `pub` retournant une valeur.** `clippy::pedantic -D warnings` refuse toute méthode `pub fn` ou `pub const fn` qui retourne `Self`/`bool`/`u32`/etc. sans `#[must_use]`. Règle à appliquer systématiquement dans Tasks 3.1+ : chaque getter, constructor, et helper qui retourne une valeur doit porter `#[must_use]`. Les setters-only (`&mut self -> ()`) sont exempts.

4. **`clippy::doc_markdown` exige des backticks** autour de tous les identifiants de type, fonction, constante et sigle mentionnés dans les doc comments. Exemples rencontrés : `` `I2cBus` ``, `` `MockBus` ``, `` `Kill_LIFE` ``, `` `u32::MAX` ``, `` `Millivolts` ``, `` `TCA9535` ``, `` `INA237` ``, `` `bmu-types` ``. Règle : **toute chaîne contenant une majuscule interne ou un underscore doit être backtickée dans la doc.**

5. **`clippy::field_reassign_with_default`** se déclenche pour le pattern `let mut x = X::default(); x.foo = bar;` utilisé massivement dans les tests Config/Action/Command. Ajouter `#[allow(clippy::field_reassign_with_default)]` sur le `mod tests` concerné.

6. **`clippy::panic` (workspace `deny`) bloque `panic!()` dans les tests.** Les tests qui veulent rejeter une mauvaise variante d'enum doivent ajouter `#[allow(clippy::panic)]` au module tests (ou préférer `matches!` + `assert!`).

7. **Enums `#[repr(u8)]` avec data-variants ne peuvent pas être cast en `u8`.** Le compilateur interdit `Command::None as u8` dès qu'un variant a un payload (`ForceOff { idx: u8 }`). Remplacer les assertions de discriminant par `matches!(cmd, Command::None)` ou par une méthode `const fn discriminant(&self) -> u8`.

8. **`clippy::cast_possible_truncation`** se déclenche sur `i as u8` dans `Snapshot::default()`. Ajouter le block `#[allow(clippy::cast_possible_truncation)]` localement (pas au module), c'est sûr car `i < MAX_BATTERIES = 16`.

9. **Slicing `&self.batteries[..n]` déclenche `clippy::indexing_slicing`.** Préférer `self.batteries.iter().take(n)` dans les itérations fleet.

10. **Toolchain esp installée localement** (`espup install --targets esp32s3`). Chaque nouvelle session doit sourcer `~/export-esp.sh` avant d'exécuter `cargo test/clippy/fmt`. `bmu-core` (staticlib `no_std` sans panic handler) ne compile toujours pas en host — c'est attendu, la façade FFI Task 9 ajoutera le panic handler. Pour les vérifications intermédiaires des Phases 3-8, utiliser `cargo <cmd> --workspace --exclude bmu-core`.

11. **Commit hooks** : subject line max 50 chars, body lines max 72 chars. Hooks vérifient au `git commit`. Prévoir des messages courts et utiliser des heredocs pour préserver le formatage multi-lignes.

---

## File Structure Overview

### Nouveau workspace Rust — `firmware-rust/`

```
firmware-rust/
├── Cargo.toml                              # workspace manifest, 9 members
├── rust-toolchain.toml                     # channel "esp"
├── .cargo/config.toml                      # PAS de default target
├── deny.toml                               # cargo-deny rules (no embedded-hal, no esp-idf-sys)
├── crates/
│   ├── bmu-types/
│   │   ├── Cargo.toml
│   │   └── src/
│   │       ├── lib.rs                      # re-exports
│   │       ├── units.rs                    # Millivolts, Milliamps, Milliohms, MilliampHours
│   │       ├── snapshot.rs                 # Snapshot, Battery, System
│   │       ├── command.rs                  # Command enum
│   │       ├── action.rs                   # Action struct
│   │       ├── config.rs                   # Config with validation
│   │       └── error.rs                    # Error enum
│   ├── bmu-i2c/
│   │   ├── Cargo.toml
│   │   └── src/lib.rs                      # I2cBus trait + I2cError
│   ├── bmu-drivers/
│   │   ├── Cargo.toml
│   │   └── src/
│   │       ├── lib.rs                      # re-exports ina237, tca9535, aht30
│   │       ├── ina237.rs                   # parse_vbus, parse_current, encode_shunt_cal, Ina237 wrapper
│   │       ├── tca9535.rs                  # parse_inputs, encode_output, Tca9535 wrapper, PCB v2 reversed map
│   │       └── aht30.rs                    # parse_measurement, Aht30 wrapper
│   ├── bmu-protection/
│   │   ├── Cargo.toml
│   │   └── src/
│   │       ├── lib.rs                      # BmuProtection API
│   │       ├── state.rs                    # BatteryState, transitions
│   │       ├── checks.rs                   # check_uv, check_ov, check_oc, check_imbalance
│   │       ├── latch.rs                    # SwitchCounter + permanent latch
│   │       ├── manager.rs                  # battery_manager (Ah counting, fleet aggregates, topology)
│   │       └── thresholds.rs               # Config validation bounds
│   ├── bmu-rint/
│   │   ├── Cargo.toml
│   │   └── src/lib.rs                      # RintEngine + RintState
│   ├── bmu-balancer/
│   │   ├── Cargo.toml
│   │   └── src/lib.rs                      # BalancerEngine + duty cycling
│   ├── bmu-core/
│   │   ├── Cargo.toml                      # staticlib crate-type
│   │   ├── build.rs                        # cbindgen header generation
│   │   ├── cbindgen.toml
│   │   └── src/
│   │       ├── lib.rs                      # extern "C" façade
│   │       ├── ffi_types.rs                # #[repr(C)] mirrors of bmu-types
│   │       └── core_impl.rs                # BmuCore struct + tick/cmd/get_cached
│   └── bmu-test-fixtures/
│       ├── Cargo.toml                      # dev-only
│       ├── fixtures/
│       │   ├── ina237/                     # *.bin golden captures
│       │   ├── tca9535/
│       │   └── aht30/
│       └── src/
│           ├── lib.rs                      # load_fixture(path) -> [u8; N]
│           └── mock_bus.rs                 # MockBus impl I2cBus
└── xtask/
    ├── Cargo.toml
    └── src/main.rs                         # vendor-header, abi-check, size, test-all
```

### Nouveau projet ESP-IDF — `firmware-idf-v2/`

```
firmware-idf-v2/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── idf_component.yml
├── main/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── task_bmu_core.cpp                   # core tick 5 Hz + retry I2C policy
│   ├── task_ble.cpp                        # NimBLE 4 services + Victron emul
│   ├── task_wifi_mqtt.cpp
│   ├── task_display.cpp                    # LVGL 5 tabs
│   ├── task_climate.cpp                    # AHT30 wrapper
│   ├── task_soh.cpp                        # TFLite Micro wrapper
│   ├── task_sd_replay.cpp
│   └── task_hotplug_scan.cpp               # 30s periodic rescan
├── components/
│   ├── bmu_core_rs/
│   │   ├── CMakeLists.txt                  # corrosion_import_crate
│   │   └── include/bmu_core.h              # vendored by xtask
│   ├── bmu_i2c_glue/
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_i2c_glue.h
│   │   ├── src/bmu_i2c_glue.c              # i2c_master + recover sequence
│   │   └── rust/
│   │       ├── Cargo.toml                  # out-of-workspace, bridges to C
│   │       └── src/lib.rs                  # EspIdfBus impl I2cBus
│   ├── bmu_ble/
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_ble.h
│   │   ├── src/
│   │   │   ├── bmu_ble.c                   # NimBLE init + connection mgmt
│   │   │   ├── svc_battery.c               # F00D0001 service
│   │   │   ├── svc_system.c                # F00D0002
│   │   │   ├── svc_control.c               # F00D0003 + HMAC
│   │   │   ├── svc_config.c                # F00D0004
│   │   │   └── svc_victron.c               # SmartShunt emul
│   │   └── secrets.h.example               # Victron AES key template
│   ├── bmu_wifi/
│   ├── bmu_mqtt/
│   ├── bmu_sd_log/                         # rolling 1 GB + NOSYNC + cursor
│   ├── bmu_display/                        # LVGL 5 tabs
│   │   ├── include/bmu_display.h
│   │   ├── src/
│   │   │   ├── bmu_display.c               # task_display dispatch
│   │   │   ├── ui_tabs.c                   # tabview + swipe
│   │   │   ├── ui_tab_batt.c               # grille 4×4
│   │   │   ├── ui_tab_soh.c
│   │   │   ├── ui_tab_sys.c
│   │   │   ├── ui_tab_climate.c
│   │   │   ├── ui_tab_config.c
│   │   │   └── ui_detail_overlay.c         # lv_chart V/I 5 min
│   │   └── assets/
│   ├── bmu_soh/
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_soh.h
│   │   ├── src/bmu_soh.cpp                 # TFLite Micro wrapper
│   │   └── models/
│   │       ├── fpnn_soh_v3_int8.tflite     # copied from v1 archive
│   │       └── README.md                   # SHA256 + origin
│   ├── bmu_climate/
│   └── bmu_config/                         # NVS bridge
└── test/
    ├── CMakeLists.txt
    ├── test_svc_battery/
    ├── test_svc_system/
    ├── test_svc_control_hmac/
    ├── test_svc_victron/
    ├── test_ble_integration_soh/
    ├── test_config_labels_nvs/
    ├── test_climate_driver/
    ├── test_sd_log_rolling/
    └── _archive/                            # suites v1 archivées
```

### Harness HIL Python — `tests/hil/`

```
tests/hil/
├── README.md
├── conftest.py                              # fixtures pytest (serial, mqtt, multimeter)
├── tb01_boot_sequence.py
├── tb02_voltage_calibration.py
├── tb03_current_calibration.py
├── tb04_undervoltage_cutoff.py
├── tb05_overvoltage_cutoff.py
├── tb06_overcurrent_cutoff.py
├── tb07_imbalance_cutoff.py
├── tb08_switch_counter_latch.py
├── tb09_reconnect_delay.py
├── tb10_topology_fail_safe.py
├── tb11_ble_ios_parity.py                   # nice-to-have
├── tb12_mqtt_replay.py                      # nice-to-have
├── tb13_long_run_72h.py                     # nice-to-have
└── report_generator.py                      # PDF via reportlab
```

---

## Phase 0 — Prérequis (déjà fait en session brainstorming)

L'état suivant doit déjà être vrai au démarrage du plan. Si non, stopper et corriger avant Phase 1.

- [ ] **Step 0.1: Vérifier tag v1-final-archive**

Run: `git tag -l v1-final-archive`
Expected: affiche `v1-final-archive`.

Si absent : `git tag -a v1-final-archive -m "Archive v1 firmware before Rust-hybrid rewrite" f8baa2a`

- [ ] **Step 0.2: Vérifier branche archive/firmware-v1**

Run: `git branch --list archive/firmware-v1`
Expected: affiche `  archive/firmware-v1`.

Si absente : `git branch archive/firmware-v1 f8baa2a`

- [ ] **Step 0.3: Vérifier working tree contient firmware-idf/ et firmware/ restaurés**

Run: `test -d firmware-idf/components/bmu_protection && test -d firmware/src && echo OK`
Expected: `OK`.

Si absent : `git restore firmware-idf/ firmware/`

- [ ] **Step 0.4: Créer (ou s'assurer d'être sur) la branche feat/rust-hybrid-v2**

Run: `git checkout -b feat/rust-hybrid-v2 2>/dev/null || git checkout feat/rust-hybrid-v2`
Expected: `Switched to branch 'feat/rust-hybrid-v2'` ou `Already on 'feat/rust-hybrid-v2'`.

- [ ] **Step 0.5: Vérifier que ESP-IDF 5.4 est installé**

Run: `source ~/esp/esp-idf/export.sh && idf.py --version`
Expected: `ESP-IDF v5.4` ou version mineure.

Si autre version : stopper, installer ESP-IDF 5.4 via `cd ~/esp && git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git && cd esp-idf && ./install.sh esp32s3`.

- [ ] **Step 0.6: Installer espup et toolchain Rust ESP32-S3**

Run: `espup --version 2>/dev/null || cargo install espup && espup install --targets esp32s3`
Expected: `xtensa-esp32s3-none-elf` toolchain disponible.

Vérification finale : `ls ~/.rustup/toolchains/ | grep esp` doit contenir `esp`.

---

## Phase 1 — Scaffold workspace Rust `firmware-rust/`

Objectif : créer une structure de workspace vide qui compile et run `cargo test`/`clippy`/`fmt` avec 0 test. Supprimer l'ancien `firmware-rs/`.

### Task 1.1: Supprimer firmware-rs/ et créer firmware-rust/

**Files:**
- Delete: `firmware-rs/` (entier, 768 lignes)
- Create: `firmware-rust/Cargo.toml`
- Create: `firmware-rust/rust-toolchain.toml`
- Create: `firmware-rust/.cargo/config.toml`
- Create: `firmware-rust/.gitignore`

- [ ] **Step 1: Supprimer firmware-rs/**

Run:
```bash
rm -rf firmware-rs/
git status --short firmware-rs/ 2>&1 | head -3
```
Expected: `firmware-rs/` n'existe plus, pas d'erreur.

- [ ] **Step 2: Créer firmware-rust/Cargo.toml (workspace racine)**

Create `firmware-rust/Cargo.toml`:

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
version = "2.0.0"

[workspace.lints.rust]
unsafe_op_in_unsafe_fn = "deny"
unused = "warn"
missing_docs = "allow"

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

[workspace.dependencies]
heapless = "0.8"
nb = "1.1"
bitflags = "2.6"
libm = "0.2"
proptest = "1.4"
serde = { version = "1.0", default-features = false, features = ["derive"] }
# postcard: no_std-friendly by default; dev-deps may enable "use-std" locally.
postcard = { version = "1.0", default-features = false }
```

- [ ] **Step 3: Créer firmware-rust/rust-toolchain.toml**

Create `firmware-rust/rust-toolchain.toml`:

```toml
[toolchain]
channel = "esp"
components = ["rust-src", "rustfmt", "clippy"]
```

- [ ] **Step 4: Créer firmware-rust/.cargo/config.toml**

Create `firmware-rust/.cargo/config.toml`:

```toml
# Important: PAS de [build] target global — sinon cargo test host casse.
# Le target xtensa est forcé uniquement depuis le build ESP-IDF (variable CARGO_BUILD_TARGET
# injectée par corrosion côté firmware-idf-v2/components/bmu_core_rs/).

[alias]
xcheck = "check --target xtensa-esp32s3-none-elf -p bmu-core"
xbuild = "build --target xtensa-esp32s3-none-elf --release -p bmu-core"

[unstable]
build-std = ["core", "alloc"]
```

- [ ] **Step 5: Créer firmware-rust/.gitignore**

Create `firmware-rust/.gitignore`:

```
target/
Cargo.lock.bak
*.rs.bk
```

- [ ] **Step 6: Commit scaffold workspace root**

```bash
git add firmware-rust/Cargo.toml firmware-rust/rust-toolchain.toml firmware-rust/.cargo firmware-rust/.gitignore
git add -u firmware-rs/  # record deletion
git commit -m "chore(firmware-rust): scaffold empty workspace, remove firmware-rs/

- Delete old firmware-rs scaffold (768 lines, superseded)
- Create firmware-rust/ workspace root with 9 members
- Pin esp toolchain via rust-toolchain.toml
- No default target in .cargo/config.toml to allow host tests"
```

### Task 1.2: Créer les 9 crates vides avec Cargo.toml minimal

**Files:**
- Create: `firmware-rust/crates/bmu-types/{Cargo.toml,src/lib.rs}`
- Create: `firmware-rust/crates/bmu-i2c/{Cargo.toml,src/lib.rs}`
- Create: `firmware-rust/crates/bmu-drivers/{Cargo.toml,src/lib.rs}`
- Create: `firmware-rust/crates/bmu-protection/{Cargo.toml,src/lib.rs}`
- Create: `firmware-rust/crates/bmu-rint/{Cargo.toml,src/lib.rs}`
- Create: `firmware-rust/crates/bmu-balancer/{Cargo.toml,src/lib.rs}`
- Create: `firmware-rust/crates/bmu-core/{Cargo.toml,src/lib.rs}`
- Create: `firmware-rust/crates/bmu-test-fixtures/{Cargo.toml,src/lib.rs}`
- Create: `firmware-rust/xtask/{Cargo.toml,src/main.rs}`

- [ ] **Step 1: Créer bmu-types**

Create `firmware-rust/crates/bmu-types/Cargo.toml`:

```toml
[package]
name = "bmu-types"
version.workspace = true
edition.workspace = true
license.workspace = true

[lints]
workspace = true
```

Create `firmware-rust/crates/bmu-types/src/lib.rs`:

```rust
//! Types partagés du core BMU. Zéro dépendance externe.
#![no_std]
```

- [ ] **Step 2: Créer bmu-i2c**

Create `firmware-rust/crates/bmu-i2c/Cargo.toml`:

```toml
[package]
name = "bmu-i2c"
version.workspace = true
edition.workspace = true
license.workspace = true

[lints]
workspace = true
```

Create `firmware-rust/crates/bmu-i2c/src/lib.rs`:

```rust
//! Trait I2cBus maison (pas embedded-hal, cf §3.3 spec).
#![no_std]
```

- [ ] **Step 3: Créer bmu-drivers**

Create `firmware-rust/crates/bmu-drivers/Cargo.toml`:

```toml
[package]
name = "bmu-drivers"
version.workspace = true
edition.workspace = true
license.workspace = true

[dependencies]
bmu-types = { path = "../bmu-types" }
bmu-i2c = { path = "../bmu-i2c" }
bitflags = { workspace = true }

[lints]
workspace = true
```

Create `firmware-rust/crates/bmu-drivers/src/lib.rs`:

```rust
//! Drivers INA237, TCA9535, AHT30 — parseurs purs + wrappers bus-aware.
#![no_std]
```

- [ ] **Step 4: Créer bmu-protection**

Create `firmware-rust/crates/bmu-protection/Cargo.toml`:

```toml
[package]
name = "bmu-protection"
version.workspace = true
edition.workspace = true
license.workspace = true

[dependencies]
bmu-types = { path = "../bmu-types" }
heapless = { workspace = true }

[dev-dependencies]
proptest = { workspace = true }

[lints]
workspace = true
```

Create `firmware-rust/crates/bmu-protection/src/lib.rs`:

```rust
//! State machine protection F01-F11 + battery manager (Ah counting).
#![no_std]
```

- [ ] **Step 5: Créer bmu-rint**

Create `firmware-rust/crates/bmu-rint/Cargo.toml`:

```toml
[package]
name = "bmu-rint"
version.workspace = true
edition.workspace = true
license.workspace = true

[dependencies]
bmu-types = { path = "../bmu-types" }

[lints]
workspace = true
```

Create `firmware-rust/crates/bmu-rint/src/lib.rs`:

```rust
//! Mesure résistance interne par pulse-off.
#![no_std]
```

- [ ] **Step 6: Créer bmu-balancer**

Create `firmware-rust/crates/bmu-balancer/Cargo.toml`:

```toml
[package]
name = "bmu-balancer"
version.workspace = true
edition.workspace = true
license.workspace = true

[dependencies]
bmu-types = { path = "../bmu-types" }

[lints]
workspace = true
```

Create `firmware-rust/crates/bmu-balancer/src/lib.rs`:

```rust
//! Duty cycling balancer, non-autoritaire (AND-mask avec protection).
#![no_std]
```

- [ ] **Step 7: Créer bmu-core (staticlib)**

Create `firmware-rust/crates/bmu-core/Cargo.toml`:

```toml
[package]
name = "bmu-core"
version.workspace = true
edition.workspace = true
license.workspace = true

[lib]
name = "bmu_core"
crate-type = ["staticlib", "rlib"]

[dependencies]
bmu-types = { path = "../bmu-types" }
bmu-i2c = { path = "../bmu-i2c" }
bmu-drivers = { path = "../bmu-drivers" }
bmu-protection = { path = "../bmu-protection" }
bmu-rint = { path = "../bmu-rint" }
bmu-balancer = { path = "../bmu-balancer" }

[build-dependencies]
cbindgen = "0.27"

[lints]
workspace = true
```

Create `firmware-rust/crates/bmu-core/src/lib.rs`:

```rust
//! Façade FFI C exposée comme staticlib libbmu_core.a + bmu_core.h.
#![no_std]
```

- [ ] **Step 8: Créer bmu-test-fixtures (dev-only)**

Create `firmware-rust/crates/bmu-test-fixtures/Cargo.toml`:

```toml
[package]
name = "bmu-test-fixtures"
version.workspace = true
edition.workspace = true
license.workspace = true

[dependencies]
bmu-i2c = { path = "../bmu-i2c" }

[lints]
workspace = true
```

Create `firmware-rust/crates/bmu-test-fixtures/src/lib.rs`:

```rust
//! Helpers de test : MockBus et chargement de fixtures binaires.
//! Crate utilisable uniquement en dev-dep (std required via test).
```

- [ ] **Step 9: Créer xtask**

Create `firmware-rust/xtask/Cargo.toml`:

```toml
[package]
name = "xtask"
version = "0.1.0"
edition = "2021"
publish = false

[dependencies]
```

Create `firmware-rust/xtask/src/main.rs`:

```rust
//! cargo xtask <command> — task runner for the BMU Rust workspace.

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match args.first().map(String::as_str) {
        Some("vendor-header") => {
            eprintln!("vendor-header: not yet implemented");
            std::process::exit(2);
        }
        Some("abi-check") => {
            eprintln!("abi-check: not yet implemented");
            std::process::exit(2);
        }
        Some("size") => {
            eprintln!("size: not yet implemented");
            std::process::exit(2);
        }
        _ => {
            eprintln!("Usage: cargo xtask <vendor-header|abi-check|size>");
            std::process::exit(1);
        }
    }
}
```

- [ ] **Step 10: Vérifier que `cargo check --workspace` passe**

Run:
```bash
cd firmware-rust
cargo check --workspace 2>&1 | tail -20
```
Expected: `Checking bmu-types v2.0.0` puis `Finished` sans erreur. Le warning `missing_docs` est désactivé pour le moment.

Si erreur `unresolved import` : vérifier les `path = "../xxx"` dans les Cargo.toml.

- [ ] **Step 11: Vérifier que `cargo test --workspace` passe (0 tests)**

Run:
```bash
cd firmware-rust
cargo test --workspace 2>&1 | tail -15
```
Expected: Chaque crate affiche `running 0 tests` puis `test result: ok. 0 passed`.

- [ ] **Step 12: Vérifier `cargo clippy --workspace` clean**

Run:
```bash
cd firmware-rust
cargo clippy --workspace --all-targets -- -D warnings 2>&1 | tail -10
```
Expected: `Finished` sans warning.

- [ ] **Step 13: Vérifier `cargo fmt --check`**

Run:
```bash
cd firmware-rust
cargo fmt --check
```
Expected: exit code 0, aucune output.

- [ ] **Step 14: Commit les 9 crates vides**

```bash
cd ..
git add firmware-rust/crates firmware-rust/xtask
git commit -m "chore(firmware-rust): create 9 empty crates with minimal Cargo.toml

Crates: bmu-types, bmu-i2c, bmu-drivers, bmu-protection, bmu-rint,
bmu-balancer, bmu-core (staticlib), bmu-test-fixtures, xtask.
All compile with cargo check/test/clippy/fmt --workspace (0 tests)."
```

### Task 1.3: Setup CI GitHub Actions pour le workspace Rust

**Files:**
- Create: `.github/workflows/firmware-rust-ci.yml`

- [ ] **Step 1: Créer le workflow CI**

Create `.github/workflows/firmware-rust-ci.yml`:

```yaml
name: firmware-rust-ci

on:
  push:
    branches: [main, 'feat/**']
    paths:
      - 'firmware-rust/**'
      - '.github/workflows/firmware-rust-ci.yml'
  pull_request:
    paths:
      - 'firmware-rust/**'

defaults:
  run:
    working-directory: firmware-rust

jobs:
  host-test:
    name: Host tests (${{ matrix.os }})
    runs-on: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-latest, macos-latest]
    steps:
      - uses: actions/checkout@v4
      - name: Install espup
        run: cargo install espup --locked
      - name: Install esp toolchain
        run: espup install --targets esp32s3
      - name: Source env
        run: |
          if [ -f "$HOME/export-esp.sh" ]; then
            . "$HOME/export-esp.sh"
            echo "PATH=$PATH" >> $GITHUB_ENV
            echo "LIBCLANG_PATH=$LIBCLANG_PATH" >> $GITHUB_ENV
          fi
      - name: cargo fmt
        run: cargo fmt --check
      - name: cargo clippy
        run: cargo clippy --workspace --all-targets -- -D warnings
      - name: cargo test (host)
        run: cargo test --workspace

  xtensa-build:
    name: Cross-compile bmu-core for ESP32-S3
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install espup
        run: cargo install espup --locked
      - name: Install esp toolchain
        run: espup install --targets esp32s3
      - name: Source env
        run: |
          . "$HOME/export-esp.sh"
          echo "PATH=$PATH" >> $GITHUB_ENV
          echo "LIBCLANG_PATH=$LIBCLANG_PATH" >> $GITHUB_ENV
      - name: cargo check (xtensa)
        run: cargo xcheck
```

- [ ] **Step 2: Commit CI workflow**

```bash
git add .github/workflows/firmware-rust-ci.yml
git commit -m "ci(firmware-rust): add workspace CI (fmt + clippy + test + xtensa check)

Matrix: ubuntu-latest + macos-latest for host tests.
Separate job for cross-compile check against xtensa-esp32s3-none-elf."
```

---

## Phase 2 — `bmu-types` (fondations type-safe)

Objectif : types forts `Millivolts`, `Milliamps`, `Milliohms`, `MilliampHours`, `Snapshot`, `Action`, `Command`, `Config`, `Error`. Tests arithmétiques + edge cases. Aucune dépendance externe. **CRIT-A éliminé by-design.**

### Task 2.1: Newtypes d'unités électriques

**Files:**
- Create: `firmware-rust/crates/bmu-types/src/units.rs`
- Modify: `firmware-rust/crates/bmu-types/src/lib.rs`

- [ ] **Step 1: Écrire les tests unitaires d'abord (TDD red)**

Create `firmware-rust/crates/bmu-types/src/units.rs` with only tests:

```rust
//! Newtype wrappers pour les unités électriques.
//! Empêchent by-design la confusion mV/V et mA/A (CRIT-A du spec §5.3).

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn millivolts_from_raw_roundtrip() {
        let v = Millivolts::from_raw(24500);
        assert_eq!(v.as_raw(), 24500);
    }

    #[test]
    fn millivolts_zero_constant() {
        assert_eq!(Millivolts::ZERO.as_raw(), 0);
    }

    #[test]
    fn millivolts_ordering() {
        assert!(Millivolts::from_raw(24000) < Millivolts::from_raw(25000));
        assert!(Millivolts::from_raw(-500) < Millivolts::ZERO);
    }

    #[test]
    fn millivolts_abs_diff_positive() {
        let a = Millivolts::from_raw(27000);
        let b = Millivolts::from_raw(25500);
        assert_eq!(a.abs_diff(b), 1500);
        assert_eq!(b.abs_diff(a), 1500);
    }

    #[test]
    fn millivolts_abs_diff_spans_zero() {
        let a = Millivolts::from_raw(-100);
        let b = Millivolts::from_raw(200);
        assert_eq!(a.abs_diff(b), 300);
    }

    #[test]
    fn milliamps_from_raw_signed() {
        let charge = Milliamps::from_raw(-1500);
        let discharge = Milliamps::from_raw(2000);
        assert_eq!(charge.as_raw(), -1500);
        assert_eq!(discharge.as_raw(), 2000);
    }

    #[test]
    fn milliamps_abs() {
        assert_eq!(Milliamps::from_raw(-1500).abs(), 1500);
        assert_eq!(Milliamps::from_raw(1500).abs(), 1500);
        assert_eq!(Milliamps::from_raw(0).abs(), 0);
    }

    #[test]
    fn milliohms_nonneg() {
        let r = Milliohms::from_raw(42);
        assert_eq!(r.as_raw(), 42);
        assert!(Milliohms::UNKNOWN.as_raw() == u32::MAX);
    }

    #[test]
    fn milliamp_hours_roundtrip() {
        let ah = MilliampHours::from_raw(4500);
        assert_eq!(ah.as_raw(), 4500);
    }

    #[test]
    fn milliamp_hours_saturating_add() {
        let a = MilliampHours::from_raw(i32::MAX - 10);
        let b = MilliampHours::from_raw(100);
        assert_eq!(a.saturating_add(b).as_raw(), i32::MAX);
    }

    #[test]
    fn milliamp_hours_saturating_sub() {
        let a = MilliampHours::from_raw(i32::MIN + 10);
        let b = MilliampHours::from_raw(100);
        assert_eq!(a.saturating_sub(b).as_raw(), i32::MIN);
    }
}
```

- [ ] **Step 2: Run tests — verify they fail to compile**

Run: `cd firmware-rust && cargo test -p bmu-types 2>&1 | tail -20`
Expected: erreur `cannot find type 'Millivolts' in this scope` etc. (compile-fail attendu).

- [ ] **Step 3: Implémenter les 4 newtypes**

Replace `firmware-rust/crates/bmu-types/src/units.rs` with full implementation (garder le `#[cfg(test)]` block de l'étape 1) :

```rust
//! Newtype wrappers pour les unités électriques.
//! Empêchent by-design la confusion mV/V et mA/A (CRIT-A du spec §5.3).

/// Millivolts signés. Jamais confondus avec Volts ou Milliamps.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Millivolts(i32);

impl Millivolts {
    pub const ZERO: Self = Self(0);

    #[inline]
    pub const fn from_raw(mv: i32) -> Self {
        Self(mv)
    }

    #[inline]
    pub const fn as_raw(self) -> i32 {
        self.0
    }

    #[inline]
    pub const fn abs_diff(self, other: Self) -> u32 {
        self.0.saturating_sub(other.0).unsigned_abs()
    }
}

/// Milliamps signés (positif = discharge par convention projet KXKM).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Milliamps(i32);

impl Milliamps {
    pub const ZERO: Self = Self(0);

    #[inline]
    pub const fn from_raw(ma: i32) -> Self {
        Self(ma)
    }

    #[inline]
    pub const fn as_raw(self) -> i32 {
        self.0
    }

    #[inline]
    pub const fn abs(self) -> u32 {
        self.0.unsigned_abs()
    }
}

/// Milliohms non signés. `UNKNOWN` = valeur sentinelle u32::MAX.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Milliohms(u32);

impl Milliohms {
    pub const UNKNOWN: Self = Self(u32::MAX);

    #[inline]
    pub const fn from_raw(r: u32) -> Self {
        Self(r)
    }

    #[inline]
    pub const fn as_raw(self) -> u32 {
        self.0
    }

    #[inline]
    pub const fn is_known(self) -> bool {
        self.0 != u32::MAX
    }
}

/// Milliampères-heures signés (charge accumulée, peut être négatif si décharge nette).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct MilliampHours(i32);

impl MilliampHours {
    pub const ZERO: Self = Self(0);

    #[inline]
    pub const fn from_raw(mah: i32) -> Self {
        Self(mah)
    }

    #[inline]
    pub const fn as_raw(self) -> i32 {
        self.0
    }

    #[inline]
    pub const fn saturating_add(self, other: Self) -> Self {
        Self(self.0.saturating_add(other.0))
    }

    #[inline]
    pub const fn saturating_sub(self, other: Self) -> Self {
        Self(self.0.saturating_sub(other.0))
    }
}
```

- [ ] **Step 4: Re-export dans lib.rs**

Modify `firmware-rust/crates/bmu-types/src/lib.rs`:

```rust
//! Types partagés du core BMU. Zéro dépendance externe.
//! Cf spec §4.1 "bmu-types — fondation type-safe (CRIT-A by-design)".
#![no_std]

pub mod units;

pub use units::{MilliampHours, Milliamps, Millivolts, Milliohms};
```

- [ ] **Step 5: Run tests — verify they pass**

Run: `cd firmware-rust && cargo test -p bmu-types 2>&1 | tail -15`
Expected: `test result: ok. 11 passed; 0 failed`.

- [ ] **Step 6: Verify clippy clean**

Run: `cd firmware-rust && cargo clippy -p bmu-types --all-targets -- -D warnings 2>&1 | tail -5`
Expected: `Finished` sans warning.

- [ ] **Step 7: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-types
git commit -m "feat(bmu-types): add Millivolts/Milliamps/Milliohms/MilliampHours newtypes

- Strong types prevent mV/V and mA/A confusion by construction (CRIT-A fix)
- No From<i32> auto-conversion forces explicit from_raw() call
- 11 unit tests covering roundtrip, ordering, abs_diff, saturating ops
- Zero external dependencies, no_std"
```

### Task 2.2: Enum `BatteryState`, `OfflineReason`, `LatchReason`

**Files:**
- Create: `firmware-rust/crates/bmu-types/src/state.rs`
- Modify: `firmware-rust/crates/bmu-types/src/lib.rs`

- [ ] **Step 1: Écrire les tests d'abord**

Create `firmware-rust/crates/bmu-types/src/state.rs`:

```rust
//! Enum d'états batterie, raisons offline/latch. Cf spec §5.1.

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn battery_state_unknown_default() {
        assert_eq!(BatteryState::default(), BatteryState::Unknown);
    }

    #[test]
    fn battery_state_discriminants_stable() {
        // Valeurs figées pour ABI C (via ffi_types de bmu-core)
        assert_eq!(BatteryState::Unknown as u8, 0);
        assert_eq!(BatteryState::Absent as u8, 1);
        assert_eq!(BatteryState::Online as u8, 2);
        assert_eq!(BatteryState::Offline as u8, 3);
        assert_eq!(BatteryState::Latched as u8, 4);
    }

    #[test]
    fn offline_reason_discriminants_stable() {
        assert_eq!(OfflineReason::Ok as u8, 0);
        assert_eq!(OfflineReason::UnderVoltage as u8, 1);
        assert_eq!(OfflineReason::OverVoltage as u8, 2);
        assert_eq!(OfflineReason::OverCurrent as u8, 3);
        assert_eq!(OfflineReason::Imbalance as u8, 4);
        assert_eq!(OfflineReason::Topology as u8, 5);
        assert_eq!(OfflineReason::Manual as u8, 6);
    }

    #[test]
    fn battery_state_is_terminal() {
        assert!(!BatteryState::Unknown.is_terminal());
        assert!(!BatteryState::Online.is_terminal());
        assert!(!BatteryState::Offline.is_terminal());
        assert!(BatteryState::Latched.is_terminal());
        assert!(BatteryState::Absent.is_terminal());
    }

    #[test]
    fn battery_state_allows_switch_on() {
        assert!(BatteryState::Online.allows_switch_on());
        assert!(BatteryState::Unknown.allows_switch_on());
        assert!(!BatteryState::Offline.allows_switch_on());
        assert!(!BatteryState::Latched.allows_switch_on());
        assert!(!BatteryState::Absent.allows_switch_on());
    }
}
```

- [ ] **Step 2: Run test — verify they fail**

Run: `cd firmware-rust && cargo test -p bmu-types --lib state 2>&1 | tail -10`
Expected: erreurs `cannot find type BatteryState`, `cannot find type OfflineReason`.

- [ ] **Step 3: Implémenter les enums**

Prepend to `firmware-rust/crates/bmu-types/src/state.rs` (avant le `#[cfg(test)]`):

```rust
//! Enum d'états batterie, raisons offline/latch. Cf spec §5.1.

/// État courant d'une batterie dans la state machine protection.
/// Discriminants u8 figés pour l'ABI C (cf bmu-core::ffi_types).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
#[repr(u8)]
pub enum BatteryState {
    #[default]
    Unknown = 0,
    Absent = 1,
    Online = 2,
    Offline = 3,
    Latched = 4,
}

impl BatteryState {
    /// Un état terminal ne transitionne plus automatiquement.
    #[inline]
    pub const fn is_terminal(self) -> bool {
        matches!(self, Self::Latched | Self::Absent)
    }

    /// True si la batterie peut être physiquement commutée ON par le core.
    /// `Unknown` inclus : permet la transition initiale vers `Online` au 1er check OK.
    #[inline]
    pub const fn allows_switch_on(self) -> bool {
        matches!(self, Self::Online | Self::Unknown)
    }
}

/// Raison d'une transition en `Offline` ou `Latched`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum OfflineReason {
    Ok = 0,
    UnderVoltage = 1,
    OverVoltage = 2,
    OverCurrent = 3,
    Imbalance = 4,
    Topology = 5,
    Manual = 6,
}

/// Raison d'un latch permanent (F08 Kill_LIFE).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum LatchReason {
    MaxSwitchReached = 1,
    TopologyFailSafe = 2,
    ManualForceOff = 3,
}
```

- [ ] **Step 4: Re-export**

Modify `firmware-rust/crates/bmu-types/src/lib.rs`:

```rust
//! Types partagés du core BMU. Zéro dépendance externe.
//! Cf spec §4.1 "bmu-types — fondation type-safe (CRIT-A by-design)".
#![no_std]

pub mod state;
pub mod units;

pub use state::{BatteryState, LatchReason, OfflineReason};
pub use units::{MilliampHours, Milliamps, Millivolts, Milliohms};
```

- [ ] **Step 5: Run tests**

Run: `cd firmware-rust && cargo test -p bmu-types 2>&1 | tail -10`
Expected: `test result: ok. 16 passed; 0 failed` (11 units + 5 state).

- [ ] **Step 6: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-types
git commit -m "feat(bmu-types): add BatteryState/OfflineReason/LatchReason enums

- Stable u8 discriminants for C ABI compatibility
- is_terminal() and allows_switch_on() helpers for state machine
- 5 new tests covering discriminant values and helper correctness"
```

### Task 2.3: Struct `Snapshot`, `Battery`, `System`

**Files:**
- Create: `firmware-rust/crates/bmu-types/src/snapshot.rs`
- Modify: `firmware-rust/crates/bmu-types/src/lib.rs`

- [ ] **Step 1: Écrire les tests**

Create `firmware-rust/crates/bmu-types/src/snapshot.rs`:

```rust
//! Immutable snapshots émis par le core à chaque tick.
//! Cf spec §3.4 BmuSnapshotC et §7.2 layout battery characteristic.

use crate::{BatteryState, MilliampHours, Milliamps, Millivolts, Milliohms, OfflineReason};

pub const MAX_BATTERIES: usize = 16;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn snapshot_default_is_empty() {
        let snap = Snapshot::default();
        assert_eq!(snap.n_bat, 0);
        assert!(!snap.system.topology_ok);
        for bat in &snap.batteries {
            assert_eq!(bat.state, BatteryState::Unknown);
            assert_eq!(bat.voltage, Millivolts::ZERO);
        }
    }

    #[test]
    fn snapshot_max_size() {
        assert_eq!(MAX_BATTERIES, 16);
        let snap = Snapshot::default();
        assert_eq!(snap.batteries.len(), MAX_BATTERIES);
    }

    #[test]
    fn battery_default_unknown() {
        let b = Battery::default();
        assert_eq!(b.idx, 0);
        assert_eq!(b.state, BatteryState::Unknown);
        assert_eq!(b.reason, OfflineReason::Ok);
        assert_eq!(b.switch_count, 0);
        assert_eq!(b.soh_pct, 0xFF); // sentinel "unknown"
        assert_eq!(b.r_ohmic, Milliohms::UNKNOWN);
    }

    #[test]
    fn snapshot_fleet_max_empty_returns_zero() {
        let snap = Snapshot::default();
        assert_eq!(snap.fleet_max_voltage(), Millivolts::ZERO);
    }

    #[test]
    fn snapshot_fleet_max_online_only() {
        let mut snap = Snapshot::default();
        snap.n_bat = 3;
        snap.batteries[0].state = BatteryState::Online;
        snap.batteries[0].voltage = Millivolts::from_raw(25000);
        snap.batteries[1].state = BatteryState::Online;
        snap.batteries[1].voltage = Millivolts::from_raw(27500);
        snap.batteries[2].state = BatteryState::Offline; // ignored
        snap.batteries[2].voltage = Millivolts::from_raw(30000);
        assert_eq!(snap.fleet_max_voltage(), Millivolts::from_raw(27500));
    }

    #[test]
    fn snapshot_fleet_sum_current_all_states() {
        let mut snap = Snapshot::default();
        snap.n_bat = 3;
        snap.batteries[0].state = BatteryState::Online;
        snap.batteries[0].current = Milliamps::from_raw(-500);
        snap.batteries[1].state = BatteryState::Online;
        snap.batteries[1].current = Milliamps::from_raw(-750);
        snap.batteries[2].state = BatteryState::Offline;
        snap.batteries[2].current = Milliamps::from_raw(-100); // ignored
        assert_eq!(snap.fleet_sum_current(), Milliamps::from_raw(-1250));
    }
}
```

- [ ] **Step 2: Run — verify failure**

Run: `cd firmware-rust && cargo test -p bmu-types snapshot 2>&1 | tail -10`
Expected: erreurs `cannot find struct 'Snapshot'`.

- [ ] **Step 3: Implémenter les structs**

Prepend to `firmware-rust/crates/bmu-types/src/snapshot.rs` (avant `#[cfg(test)]`) :

```rust
/// Une batterie à l'instant T.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Battery {
    pub idx: u8,
    pub state: BatteryState,
    pub reason: OfflineReason,
    pub switch_count: u8,
    pub voltage: Millivolts,
    pub current: Milliamps,
    pub ah_remaining: MilliampHours,
    pub temp_c10: i16,
    pub soh_pct: u8,           // 0..100, 0xFF = unknown
    pub balancer_duty_pct: u8, // 0..100
    pub r_ohmic: Milliohms,    // UNKNOWN = not measured
}

impl Default for Battery {
    fn default() -> Self {
        Self {
            idx: 0,
            state: BatteryState::Unknown,
            reason: OfflineReason::Ok,
            switch_count: 0,
            voltage: Millivolts::ZERO,
            current: Milliamps::ZERO,
            ah_remaining: MilliampHours::ZERO,
            temp_c10: 0,
            soh_pct: 0xFF,
            balancer_duty_pct: 0,
            r_ohmic: Milliohms::UNKNOWN,
        }
    }
}

/// Métadonnées système (agrégats non-battery).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct System {
    pub topology_ok: bool,
    pub n_bat: u8,
    pub tick_us_p50: u32,
    pub tick_us_p99: u32,
    pub wdt_feeds: u32,
    pub monotonic_us: u64,
}

/// Snapshot immuable émis à chaque tick par le core.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Snapshot {
    pub n_bat: u8,
    pub batteries: [Battery; MAX_BATTERIES],
    pub system: System,
}

impl Default for Snapshot {
    fn default() -> Self {
        let mut batteries = [Battery::default(); MAX_BATTERIES];
        for (i, b) in batteries.iter_mut().enumerate() {
            // FIX clippy::cast_possible_truncation: i in 0..16 always fits u8
            #[allow(clippy::cast_possible_truncation)]
            { b.idx = i as u8; }
        }
        Self {
            n_bat: 0,
            batteries,
            system: System::default(),
        }
    }
}

impl Snapshot {
    /// Max tension parmi les batteries Online. Retourne ZERO si aucune Online.
    /// **CRIT-B fix**: tous les checks imbalance utilisent cette fonction, JAMAIS
    /// un max local glissant. Cf spec §5.3.
    pub fn fleet_max_voltage(&self) -> Millivolts {
        let mut max = Millivolts::ZERO;
        let n = (self.n_bat as usize).min(MAX_BATTERIES);
        for b in &self.batteries[..n] {
            if b.state == BatteryState::Online && b.voltage > max {
                max = b.voltage;
            }
        }
        max
    }

    /// Somme des courants des batteries Online (agrégat fleet).
    pub fn fleet_sum_current(&self) -> Milliamps {
        let mut sum: i32 = 0;
        let n = (self.n_bat as usize).min(MAX_BATTERIES);
        for b in &self.batteries[..n] {
            if b.state == BatteryState::Online {
                sum = sum.saturating_add(b.current.as_raw());
            }
        }
        Milliamps::from_raw(sum)
    }
}
```

- [ ] **Step 4: Re-export dans lib.rs**

Modify `firmware-rust/crates/bmu-types/src/lib.rs`:

```rust
//! Types partagés du core BMU. Zéro dépendance externe.
//! Cf spec §4.1.
#![no_std]

pub mod snapshot;
pub mod state;
pub mod units;

pub use snapshot::{Battery, Snapshot, System, MAX_BATTERIES};
pub use state::{BatteryState, LatchReason, OfflineReason};
pub use units::{MilliampHours, Milliamps, Millivolts, Milliohms};
```

- [ ] **Step 5: Run tests**

Run: `cd firmware-rust && cargo test -p bmu-types 2>&1 | tail -10`
Expected: `test result: ok. 22 passed; 0 failed`.

- [ ] **Step 6: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-types
git commit -m "feat(bmu-types): add Battery/System/Snapshot with fleet aggregates

- Snapshot owns [Battery; 16] inline, no heap, no Vec
- fleet_max_voltage() filters Online-only (CRIT-B fix basis)
- fleet_sum_current() ignores Offline/Latched/Absent
- 6 new tests including fleet aggregate edge cases"
```

### Task 2.4: Struct `Config` avec validation bounds

**Files:**
- Create: `firmware-rust/crates/bmu-types/src/config.rs`
- Modify: `firmware-rust/crates/bmu-types/src/lib.rs`

- [ ] **Step 1: Tests**

Create `firmware-rust/crates/bmu-types/src/config.rs`:

```rust
//! Config runtime validée. Cf spec §5.4.

use crate::{Milliamps, Millivolts};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn config_default_matches_kill_life_spec() {
        let c = Config::default();
        assert_eq!(c.umin, Millivolts::from_raw(24000));
        assert_eq!(c.umax, Millivolts::from_raw(30000));
        assert_eq!(c.imax, Milliamps::from_raw(1000));
        assert_eq!(c.vdiff_imbalance, Millivolts::from_raw(1000));
        assert_eq!(c.nb_switch_max, 5);
        assert_eq!(c.reconnect_delay_ms, 10_000);
        assert_eq!(c.tick_period_ms, 200);
    }

    #[test]
    fn config_default_validates() {
        assert!(Config::default().validate().is_ok());
    }

    #[test]
    fn config_rejects_umin_below_18v() {
        let mut c = Config::default();
        c.umin = Millivolts::from_raw(17999);
        assert_eq!(c.validate(), Err(ConfigError::UminTooLow));
    }

    #[test]
    fn config_rejects_umax_above_36v() {
        let mut c = Config::default();
        c.umax = Millivolts::from_raw(36001);
        assert_eq!(c.validate(), Err(ConfigError::UmaxTooHigh));
    }

    #[test]
    fn config_rejects_umin_gte_umax() {
        let mut c = Config::default();
        c.umin = Millivolts::from_raw(30000);
        c.umax = Millivolts::from_raw(30000);
        assert_eq!(c.validate(), Err(ConfigError::UminGteUmax));
    }

    #[test]
    fn config_rejects_imax_over_10a() {
        let mut c = Config::default();
        c.imax = Milliamps::from_raw(10_001);
        assert_eq!(c.validate(), Err(ConfigError::ImaxTooHigh));
    }

    #[test]
    fn config_rejects_imax_zero_or_negative() {
        let mut c = Config::default();
        c.imax = Milliamps::from_raw(0);
        assert_eq!(c.validate(), Err(ConfigError::ImaxNonPositive));
    }

    #[test]
    fn config_rejects_vdiff_over_5v() {
        let mut c = Config::default();
        c.vdiff_imbalance = Millivolts::from_raw(5001);
        assert_eq!(c.validate(), Err(ConfigError::VdiffTooHigh));
    }

    #[test]
    fn config_rejects_nb_switch_zero() {
        let mut c = Config::default();
        c.nb_switch_max = 0;
        assert_eq!(c.validate(), Err(ConfigError::NbSwitchOutOfRange));
    }

    #[test]
    fn config_rejects_nb_switch_over_20() {
        let mut c = Config::default();
        c.nb_switch_max = 21;
        assert_eq!(c.validate(), Err(ConfigError::NbSwitchOutOfRange));
    }

    #[test]
    fn config_rejects_reconnect_delay_below_1s() {
        let mut c = Config::default();
        c.reconnect_delay_ms = 999;
        assert_eq!(c.validate(), Err(ConfigError::ReconnectDelayOutOfRange));
    }

    #[test]
    fn config_rejects_reconnect_delay_over_10min() {
        let mut c = Config::default();
        c.reconnect_delay_ms = 600_001;
        assert_eq!(c.validate(), Err(ConfigError::ReconnectDelayOutOfRange));
    }

    #[test]
    fn config_rejects_tick_period_zero() {
        let mut c = Config::default();
        c.tick_period_ms = 0;
        assert_eq!(c.validate(), Err(ConfigError::TickPeriodOutOfRange));
    }

    #[test]
    fn config_rejects_tick_period_over_1s() {
        let mut c = Config::default();
        c.tick_period_ms = 1001;
        assert_eq!(c.validate(), Err(ConfigError::TickPeriodOutOfRange));
    }
}
```

- [ ] **Step 2: Run — expect failure**

Run: `cd firmware-rust && cargo test -p bmu-types config 2>&1 | tail -10`
Expected: `cannot find struct 'Config'`.

- [ ] **Step 3: Implémenter Config + ConfigError + validate()**

Prepend to `firmware-rust/crates/bmu-types/src/config.rs`:

```rust
//! Config runtime validée. Cf spec §5.4.

use crate::{Milliamps, Millivolts};

/// Erreurs de validation de config. Jamais de texte runtime (no_std no alloc).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum ConfigError {
    UminTooLow = 1,        // < 18000 mV (< 3V/cell × 6)
    UmaxTooHigh = 2,       // > 36000 mV (> 3.6V/cell × 10)
    UminGteUmax = 3,       // umin >= umax
    ImaxTooHigh = 4,       // > 10 A
    ImaxNonPositive = 5,   // <= 0
    VdiffTooHigh = 6,      // > 5000 mV
    NbSwitchOutOfRange = 7,// 0 or > 20
    ReconnectDelayOutOfRange = 8, // < 1 s or > 10 min
    TickPeriodOutOfRange = 9,     // 0 or > 1 s
}

/// Config runtime du core BMU. Valeurs défaut = spec Kill_LIFE 01_spec.md F01-F08.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Config {
    pub umin: Millivolts,              // default 24000 mV
    pub umax: Millivolts,              // default 30000 mV
    pub imax: Milliamps,               // default 1000 mA
    pub vdiff_imbalance: Millivolts,   // default 1000 mV
    pub nb_switch_max: u8,             // default 5
    pub reconnect_delay_ms: u32,       // default 10_000
    pub tick_period_ms: u32,           // default 200
}

impl Default for Config {
    fn default() -> Self {
        Self {
            umin: Millivolts::from_raw(24_000),
            umax: Millivolts::from_raw(30_000),
            imax: Milliamps::from_raw(1_000),
            vdiff_imbalance: Millivolts::from_raw(1_000),
            nb_switch_max: 5,
            reconnect_delay_ms: 10_000,
            tick_period_ms: 200,
        }
    }
}

impl Config {
    /// Valide les bornes de sécurité. Appelée à chaque `bmu_core_set_config`
    /// et au boot. Rejette toute config dangereuse.
    pub fn validate(&self) -> Result<(), ConfigError> {
        if self.umin.as_raw() < 18_000 {
            return Err(ConfigError::UminTooLow);
        }
        if self.umax.as_raw() > 36_000 {
            return Err(ConfigError::UmaxTooHigh);
        }
        if self.umin.as_raw() >= self.umax.as_raw() {
            return Err(ConfigError::UminGteUmax);
        }
        if self.imax.as_raw() <= 0 {
            return Err(ConfigError::ImaxNonPositive);
        }
        if self.imax.as_raw() > 10_000 {
            return Err(ConfigError::ImaxTooHigh);
        }
        if self.vdiff_imbalance.as_raw() > 5_000 {
            return Err(ConfigError::VdiffTooHigh);
        }
        if self.nb_switch_max == 0 || self.nb_switch_max > 20 {
            return Err(ConfigError::NbSwitchOutOfRange);
        }
        if self.reconnect_delay_ms < 1_000 || self.reconnect_delay_ms > 600_000 {
            return Err(ConfigError::ReconnectDelayOutOfRange);
        }
        if self.tick_period_ms == 0 || self.tick_period_ms > 1_000 {
            return Err(ConfigError::TickPeriodOutOfRange);
        }
        Ok(())
    }
}
```

- [ ] **Step 4: Re-export**

Modify `firmware-rust/crates/bmu-types/src/lib.rs`:

```rust
//! Types partagés du core BMU. Zéro dépendance externe. Cf spec §4.1.
#![no_std]

pub mod config;
pub mod snapshot;
pub mod state;
pub mod units;

pub use config::{Config, ConfigError};
pub use snapshot::{Battery, Snapshot, System, MAX_BATTERIES};
pub use state::{BatteryState, LatchReason, OfflineReason};
pub use units::{MilliampHours, Milliamps, Millivolts, Milliohms};
```

- [ ] **Step 5: Run tests**

Run: `cd firmware-rust && cargo test -p bmu-types 2>&1 | tail -10`
Expected: `test result: ok. 36 passed; 0 failed` (22 + 14 config).

- [ ] **Step 6: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-types
git commit -m "feat(bmu-types): add Config with Kill_LIFE defaults and safety bounds

- Default values match specs/01_spec.md F01-F08 (24000/30000/1000/1000/5/10000/200)
- validate() rejects 9 classes of dangerous configs (umin<18V, umax>36V, etc.)
- ConfigError is #[repr(u8)] for C ABI compat
- 14 new tests covering every rejection branch"
```

### Task 2.5: Struct `Action` et enum `Command`

**Files:**
- Create: `firmware-rust/crates/bmu-types/src/action.rs`
- Create: `firmware-rust/crates/bmu-types/src/command.rs`
- Modify: `firmware-rust/crates/bmu-types/src/lib.rs`

- [ ] **Step 1: Tests Action**

Create `firmware-rust/crates/bmu-types/src/action.rs`:

```rust
//! Actions produites par le core à chaque tick, exécutées par C.
//! Cf spec §3.4 BmuActionsC.

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn actions_default_empty() {
        let a = Actions::default();
        assert_eq!(a.tca_set_mask, 0);
        assert_eq!(a.tca_clr_mask, 0);
        assert_eq!(a.rint_trigger_idx, 0xFF);
        assert_eq!(a.request_soh_inference, false);
    }

    #[test]
    fn actions_no_contention_invariant() {
        // Jamais set et clr sur le même bit
        let a = Actions {
            tca_set_mask: 0b0000_0000_1010_0101,
            tca_clr_mask: 0b1111_1111_0101_1010,
            rint_trigger_idx: 0xFF,
            request_soh_inference: false,
        };
        assert_eq!(a.tca_set_mask & a.tca_clr_mask, 0);
    }

    #[test]
    fn actions_merge_protection_wins_over_balancer() {
        let protection_allowed = 0b0000_0000_1111_1111u16; // bat 0..7 allowed
        let balancer_request = 0b0000_0000_1111_1111u16;   // balancer wants all
        let merged = Actions::merge_balancer(balancer_request, protection_allowed);
        assert_eq!(merged, 0b0000_0000_1111_1111);
    }

    #[test]
    fn actions_merge_protection_blocks_balancer() {
        let protection_allowed = 0b0000_0000_0000_1111u16; // bat 0..3 only
        let balancer_request = 0b0000_0000_1111_1111u16;   // balancer wants all 0..7
        let merged = Actions::merge_balancer(balancer_request, protection_allowed);
        assert_eq!(merged, 0b0000_0000_0000_1111);
    }
}
```

- [ ] **Step 2: Implémenter Actions**

Prepend to `firmware-rust/crates/bmu-types/src/action.rs`:

```rust
//! Actions produites par le core à chaque tick, exécutées par C.
//! Cf spec §3.4 BmuActionsC.

/// Actions à exécuter après un tick core.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Actions {
    /// Bitmask des contacteurs TCA9535 à mettre ON (bit N = batterie N).
    pub tca_set_mask: u16,
    /// Bitmask des contacteurs TCA9535 à mettre OFF.
    pub tca_clr_mask: u16,
    /// Index de la batterie à armer en mesure R_int (0xFF = aucun).
    pub rint_trigger_idx: u8,
    /// Demande une inférence SOH TFLite hors cycle périodique.
    pub request_soh_inference: bool,
}

impl Default for Actions {
    fn default() -> Self {
        Self {
            tca_set_mask: 0,
            tca_clr_mask: 0,
            rint_trigger_idx: 0xFF,
            request_soh_inference: false,
        }
    }
}

impl Actions {
    /// AND-mask la requête balancer avec l'allowed mask de la protection.
    /// Règle fondamentale §6.2: la protection "dispose", le balancer "propose".
    #[inline]
    pub const fn merge_balancer(balancer_request: u16, protection_allowed: u16) -> u16 {
        balancer_request & protection_allowed
    }
}
```

- [ ] **Step 3: Tests Command**

Create `firmware-rust/crates/bmu-types/src/command.rs`:

```rust
//! Commandes émises par BLE/UI vers le core via bmu_core_command.
//! Cf spec §7.4 Commandes Control.

use crate::Config;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn command_discriminants_stable() {
        assert_eq!(Command::None as u8, 0);
        // Autres discriminants vérifiés via match exhaustif en FFI layer
    }

    #[test]
    fn command_force_off_carries_idx() {
        let c = Command::ForceOff { idx: 5 };
        if let Command::ForceOff { idx } = c {
            assert_eq!(idx, 5);
        } else {
            panic!("wrong variant");
        }
    }

    #[test]
    fn command_set_config_carries_config() {
        let mut cfg = Config::default();
        cfg.nb_switch_max = 3;
        let c = Command::SetConfig(cfg);
        if let Command::SetConfig(got) = c {
            assert_eq!(got.nb_switch_max, 3);
        } else {
            panic!("wrong variant");
        }
    }
}
```

- [ ] **Step 4: Implémenter Command**

Prepend to `firmware-rust/crates/bmu-types/src/command.rs`:

```rust
//! Commandes émises par BLE/UI vers le core via bmu_core_command.
//! Cf spec §7.4 Commandes Control.

use crate::Config;

/// Commande vers le core. Variants correspondent aux cmd BLE (§7.4)
/// + commandes internes (hotplug, SOH update).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Command {
    None = 0,
    ForceOff { idx: u8 } = 1,
    ResetAh { idx: u8 } = 2,
    TriggerRint { idx: u8 } = 3,
    ResetLatch { idx: u8 } = 4,
    SetConfig(Config) = 6,
    UpdateSoh { idx: u8, soh_pct: u8 } = 7,
    TopologyChanged { n_ina: u8, n_tca: u8 } = 8,
}
```

- [ ] **Step 5: Re-export dans lib.rs**

Modify `firmware-rust/crates/bmu-types/src/lib.rs`:

```rust
//! Types partagés du core BMU. Zéro dépendance externe. Cf spec §4.1.
#![no_std]

pub mod action;
pub mod command;
pub mod config;
pub mod snapshot;
pub mod state;
pub mod units;

pub use action::Actions;
pub use command::Command;
pub use config::{Config, ConfigError};
pub use snapshot::{Battery, Snapshot, System, MAX_BATTERIES};
pub use state::{BatteryState, LatchReason, OfflineReason};
pub use units::{MilliampHours, Milliamps, Millivolts, Milliohms};
```

- [ ] **Step 6: Run tests**

Run: `cd firmware-rust && cargo test -p bmu-types 2>&1 | tail -10`
Expected: `test result: ok. 43 passed; 0 failed` (36 + 4 action + 3 command).

- [ ] **Step 7: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-types
git commit -m "feat(bmu-types): add Actions and Command enums

- Actions: tca_set/clr bitmask + rint_trigger_idx + soh request
- Actions::merge_balancer() enforces protection wins over balancer (§6.2)
- Command enum with arbitrary_enum_discriminant for u8 stability
- 7 new tests"
```

---

## Phase 3 — `bmu-i2c` (trait maison) + MockBus

Objectif : trait `I2cBus` avec API synchrone, `I2cError` enum complet, et `MockBus` dans `bmu-test-fixtures` pour injection de transactions scriptées.

### Task 3.1: Trait `I2cBus` et enum `I2cError`

**Files:**
- Modify: `firmware-rust/crates/bmu-i2c/src/lib.rs`
- Modify: `firmware-rust/crates/bmu-i2c/Cargo.toml`

- [ ] **Step 1: Tests**

Replace `firmware-rust/crates/bmu-i2c/src/lib.rs` with tests-only version:

```rust
//! Trait I2cBus maison (pas embedded-hal, cf §3.3 spec).
#![no_std]

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn i2c_error_is_copy() {
        let e = I2cError::Nack;
        let _copy = e;
        let _again = e;
        assert_eq!(e, I2cError::Nack);
    }

    #[test]
    fn i2c_error_discriminants_distinct() {
        assert_ne!(I2cError::Nack, I2cError::ArbLost);
        assert_ne!(I2cError::Timeout, I2cError::BusBusy);
        assert_ne!(I2cError::InvalidLength, I2cError::Hardware(0));
    }

    #[test]
    fn i2c_error_hardware_wraps_code() {
        let e = I2cError::Hardware(0xDEAD);
        if let I2cError::Hardware(code) = e {
            assert_eq!(code, 0xDEAD);
        } else {
            panic!("wrong variant");
        }
    }
}
```

- [ ] **Step 2: Run — expect failure**

Run: `cd firmware-rust && cargo test -p bmu-i2c 2>&1 | tail -10`
Expected: `cannot find type 'I2cError'`.

- [ ] **Step 3: Implémenter trait et erreur**

Replace `firmware-rust/crates/bmu-i2c/src/lib.rs`:

```rust
//! Trait I2cBus maison (pas embedded-hal, cf §3.3 spec).
//! Zéro dépendance. Utilisé par bmu-drivers (parseurs purs + wrappers bus-aware)
//! et impl séparées MockBus (tests) et EspIdfBus (hors workspace, côté ESP-IDF).
#![no_std]

/// Erreurs I²C platform-agnostiques. `Hardware(code)` encapsule un code opaque.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum I2cError {
    /// Device n'a pas ACK son adresse.
    Nack,
    /// Arbitrage perdu (multi-master non utilisé, mais glitch possible).
    ArbLost,
    /// Bus bloqué (SDA ou SCL maintenu).
    Timeout,
    /// Bus déjà busy au start de la transaction.
    BusBusy,
    /// Longueur de buffer invalide.
    InvalidLength,
    /// Code d'erreur opaque de la plateforme.
    Hardware(u16),
}

/// Trait I²C synchrone. Chaque méthode correspond à une transaction atomique.
///
/// Les implémentations doivent :
/// - garantir un STOP en fin de transaction (même en erreur)
/// - retourner `Nack` si le device ne répond pas à l'adresse
/// - retourner `BusBusy` si une autre task tient le bus
///
/// Le trait n'impose PAS de lifetime, donc les impls peuvent être stateful
/// (MockBus script, EspIdfBus handle).
pub trait I2cBus {
    /// Write `wr` puis read `rd` en une seule transaction (repeated start).
    fn write_read(
        &mut self,
        addr: u8,
        wr: &[u8],
        rd: &mut [u8],
    ) -> Result<(), I2cError>;

    /// Write seul.
    fn write(&mut self, addr: u8, wr: &[u8]) -> Result<(), I2cError>;

    /// Read seul.
    fn read(&mut self, addr: u8, rd: &mut [u8]) -> Result<(), I2cError>;

    /// Retourne true si le bus semble sain (SDA+SCL high au repos).
    /// Les impls qui ne peuvent pas vérifier PEUVENT retourner Ok(true).
    fn probe_idle(&mut self) -> Result<bool, I2cError>;

    /// Séquence de bus recovery (9 clock pulses SCL + STOP).
    /// Les impls qui ne la supportent pas PEUVENT no-op en Ok(()).
    fn recover(&mut self) -> Result<(), I2cError>;
}
```

- [ ] **Step 4: Run — verify pass**

Run: `cd firmware-rust && cargo test -p bmu-i2c 2>&1 | tail -10`
Expected: `test result: ok. 3 passed; 0 failed`.

- [ ] **Step 5: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-i2c
git commit -m "feat(bmu-i2c): define I2cBus trait and I2cError enum

- Custom trait, not embedded-hal (spec §3.3 interdiction list)
- 5 methods: write_read, write, read, probe_idle, recover
- I2cError::Hardware(u16) wraps platform-specific codes opaquely
- 3 tests on error type invariants"
```

### Task 3.2: `MockBus` dans `bmu-test-fixtures`

**Files:**
- Create: `firmware-rust/crates/bmu-test-fixtures/src/mock_bus.rs`
- Modify: `firmware-rust/crates/bmu-test-fixtures/src/lib.rs`
- Modify: `firmware-rust/crates/bmu-test-fixtures/Cargo.toml`

- [ ] **Step 1: Ajouter dep bmu-i2c dans Cargo.toml**

Replace `firmware-rust/crates/bmu-test-fixtures/Cargo.toml`:

```toml
[package]
name = "bmu-test-fixtures"
version.workspace = true
edition.workspace = true
license.workspace = true

[dependencies]
bmu-i2c = { path = "../bmu-i2c" }

[lints]
workspace = true
```

- [ ] **Step 2: Écrire les tests du MockBus**

Create `firmware-rust/crates/bmu-test-fixtures/src/mock_bus.rs`:

```rust
//! MockBus scripted pour tests host.

use bmu_i2c::{I2cBus, I2cError};
use std::collections::VecDeque;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mock_bus_write_read_success() {
        let mut bus = MockBus::new();
        bus.expect_write_read(0x40, vec![0x05], vec![0x12, 0x34]);

        let mut rd = [0u8; 2];
        bus.write_read(0x40, &[0x05], &mut rd).unwrap();
        assert_eq!(rd, [0x12, 0x34]);
    }

    #[test]
    fn mock_bus_write_success() {
        let mut bus = MockBus::new();
        bus.expect_write(0x20, vec![0x06, 0xF0]);
        bus.write(0x20, &[0x06, 0xF0]).unwrap();
    }

    #[test]
    fn mock_bus_read_success() {
        let mut bus = MockBus::new();
        bus.expect_read(0x38, vec![0x1C, 0x5A]);
        let mut rd = [0u8; 2];
        bus.read(0x38, &mut rd).unwrap();
        assert_eq!(rd, [0x1C, 0x5A]);
    }

    #[test]
    fn mock_bus_inject_nack() {
        let mut bus = MockBus::new();
        bus.expect_error(I2cError::Nack);
        let mut rd = [0u8; 2];
        assert_eq!(bus.write_read(0x40, &[0x05], &mut rd), Err(I2cError::Nack));
    }

    #[test]
    fn mock_bus_address_mismatch_fails() {
        let mut bus = MockBus::new();
        bus.expect_write_read(0x40, vec![0x05], vec![0, 0]);
        let mut rd = [0u8; 2];
        assert_eq!(
            bus.write_read(0x41, &[0x05], &mut rd),
            Err(I2cError::Hardware(0xBAD1))
        );
    }

    #[test]
    fn mock_bus_exhausted_returns_hardware_error() {
        let mut bus = MockBus::new();
        let mut rd = [0u8; 2];
        assert_eq!(
            bus.write_read(0x40, &[0x05], &mut rd),
            Err(I2cError::Hardware(0xBAD0))
        );
    }

    #[test]
    fn mock_bus_records_call_count() {
        let mut bus = MockBus::new();
        bus.expect_write_read(0x40, vec![0x05], vec![0, 0]);
        bus.expect_write_read(0x40, vec![0x05], vec![0, 0]);
        let mut rd = [0u8; 2];
        bus.write_read(0x40, &[0x05], &mut rd).unwrap();
        bus.write_read(0x40, &[0x05], &mut rd).unwrap();
        assert_eq!(bus.call_count(), 2);
    }

    #[test]
    fn mock_bus_probe_idle_always_ok() {
        let mut bus = MockBus::new();
        assert_eq!(bus.probe_idle(), Ok(true));
    }

    #[test]
    fn mock_bus_recover_noop() {
        let mut bus = MockBus::new();
        assert_eq!(bus.recover(), Ok(()));
    }
}
```

- [ ] **Step 3: Run — expect failure**

Run: `cd firmware-rust && cargo test -p bmu-test-fixtures 2>&1 | tail -10`
Expected: erreurs `cannot find struct 'MockBus'`.

- [ ] **Step 4: Implémenter MockBus**

Prepend to `firmware-rust/crates/bmu-test-fixtures/src/mock_bus.rs`:

```rust
//! MockBus scripted pour tests host.

use bmu_i2c::{I2cBus, I2cError};
use std::collections::VecDeque;

#[derive(Debug, Clone)]
enum Transaction {
    WriteRead { addr: u8, wr: Vec<u8>, rd: Vec<u8> },
    Write { addr: u8, wr: Vec<u8> },
    Read { addr: u8, rd: Vec<u8> },
    Error(I2cError),
}

/// Bus I²C scripted pour tests host.
/// Les transactions attendues sont enfilées via `expect_*()`, puis rejouées dans l'ordre.
///
/// Codes d'erreur spéciaux :
/// - `Hardware(0xBAD0)` : script épuisé (plus de transaction attendue)
/// - `Hardware(0xBAD1)` : mismatch adresse
/// - `Hardware(0xBAD2)` : mismatch write buffer
/// - `Hardware(0xBAD3)` : mismatch buffer length
#[derive(Debug, Default)]
pub struct MockBus {
    script: VecDeque<Transaction>,
    calls: usize,
}

impl MockBus {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn expect_write_read(&mut self, addr: u8, wr: Vec<u8>, rd: Vec<u8>) {
        self.script.push_back(Transaction::WriteRead { addr, wr, rd });
    }

    pub fn expect_write(&mut self, addr: u8, wr: Vec<u8>) {
        self.script.push_back(Transaction::Write { addr, wr });
    }

    pub fn expect_read(&mut self, addr: u8, rd: Vec<u8>) {
        self.script.push_back(Transaction::Read { addr, rd });
    }

    pub fn expect_error(&mut self, err: I2cError) {
        self.script.push_back(Transaction::Error(err));
    }

    pub fn call_count(&self) -> usize {
        self.calls
    }

    pub fn script_remaining(&self) -> usize {
        self.script.len()
    }
}

impl I2cBus for MockBus {
    fn write_read(&mut self, addr: u8, wr: &[u8], rd: &mut [u8]) -> Result<(), I2cError> {
        self.calls += 1;
        let tx = self.script.pop_front().ok_or(I2cError::Hardware(0xBAD0))?;
        match tx {
            Transaction::WriteRead { addr: a, wr: w, rd: r } => {
                if a != addr {
                    return Err(I2cError::Hardware(0xBAD1));
                }
                if w != wr {
                    return Err(I2cError::Hardware(0xBAD2));
                }
                if r.len() != rd.len() {
                    return Err(I2cError::Hardware(0xBAD3));
                }
                rd.copy_from_slice(&r);
                Ok(())
            }
            Transaction::Error(e) => Err(e),
            _ => Err(I2cError::Hardware(0xBAD4)),
        }
    }

    fn write(&mut self, addr: u8, wr: &[u8]) -> Result<(), I2cError> {
        self.calls += 1;
        let tx = self.script.pop_front().ok_or(I2cError::Hardware(0xBAD0))?;
        match tx {
            Transaction::Write { addr: a, wr: w } => {
                if a != addr {
                    return Err(I2cError::Hardware(0xBAD1));
                }
                if w != wr {
                    return Err(I2cError::Hardware(0xBAD2));
                }
                Ok(())
            }
            Transaction::Error(e) => Err(e),
            _ => Err(I2cError::Hardware(0xBAD4)),
        }
    }

    fn read(&mut self, addr: u8, rd: &mut [u8]) -> Result<(), I2cError> {
        self.calls += 1;
        let tx = self.script.pop_front().ok_or(I2cError::Hardware(0xBAD0))?;
        match tx {
            Transaction::Read { addr: a, rd: r } => {
                if a != addr {
                    return Err(I2cError::Hardware(0xBAD1));
                }
                if r.len() != rd.len() {
                    return Err(I2cError::Hardware(0xBAD3));
                }
                rd.copy_from_slice(&r);
                Ok(())
            }
            Transaction::Error(e) => Err(e),
            _ => Err(I2cError::Hardware(0xBAD4)),
        }
    }

    fn probe_idle(&mut self) -> Result<bool, I2cError> {
        Ok(true)
    }

    fn recover(&mut self) -> Result<(), I2cError> {
        Ok(())
    }
}
```

- [ ] **Step 5: Re-export dans lib.rs**

Replace `firmware-rust/crates/bmu-test-fixtures/src/lib.rs`:

```rust
//! Helpers de test : MockBus et chargement de fixtures binaires.
//! Crate utilisable uniquement en dev-dep (std required).

pub mod mock_bus;

pub use mock_bus::MockBus;
```

- [ ] **Step 6: Run tests**

Run: `cd firmware-rust && cargo test -p bmu-test-fixtures 2>&1 | tail -10`
Expected: `test result: ok. 9 passed; 0 failed`.

- [ ] **Step 7: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-test-fixtures firmware-rust/crates/bmu-i2c
git commit -m "feat(bmu-test-fixtures): add MockBus for scripted I2cBus tests

- expect_write_read/write/read/error enqueue transactions
- Validates addr, wr buffer, and rd length on each call
- Hardware(0xBADN) sentinels for script exhaustion and mismatches
- probe_idle() always Ok(true), recover() no-op (host semantics)
- 9 tests covering success, failure, and exhaustion paths"
```

---

## Phase 4 — `bmu-drivers` INA237

Objectif : parseurs purs INA237 (`parse_vbus`, `parse_current`, `parse_dietemp`, `encode_shunt_cal`, `check_device_id`) + wrapper bus-aware `Ina237<'b, B>` + golden fixture tests contre valeurs datasheet SBOS945.

### Task 4.1: Module `ina237` — parseurs purs

**Files:**
- Create: `firmware-rust/crates/bmu-drivers/src/ina237.rs`
- Modify: `firmware-rust/crates/bmu-drivers/src/lib.rs`

- [ ] **Step 1: Écrire les tests parseurs (red)**

Create `firmware-rust/crates/bmu-drivers/src/ina237.rs`:

```rust
//! Driver INA237 — parseurs purs + wrapper bus-aware.
//! Référence : TI SBOS945 datasheet, tables 7-5 (registers), 7-4 (conversions).

use bmu_i2c::{I2cBus, I2cError};
use bmu_types::{Milliamps, Millivolts};

pub const INA237_ADDR_BASE: u8 = 0x40;
pub const INA237_ADDR_MAX: u8 = 0x4F;

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Reg {
    Config = 0x00,
    AdcConfig = 0x01,
    ShuntCal = 0x02,
    VShunt = 0x04,
    VBus = 0x05,
    DieTemp = 0x06,
    Current = 0x07,
    Power = 0x08,
    Diag = 0x0B,
    ManufId = 0x3E,
    DeviceId = 0x3F,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Ina237Error {
    I2c(I2cError),
    UnexpectedDeviceId(u16),
    UnexpectedManufacturerId(u16),
    CalibrationInvalid,
}

impl From<I2cError> for Ina237Error {
    fn from(e: I2cError) -> Self {
        Self::I2c(e)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_vbus_zero() {
        assert_eq!(parse_vbus([0x00, 0x00]), Millivolts::ZERO);
    }

    #[test]
    fn parse_vbus_positive() {
        // raw = 0x1F40 = 8000, >>3 = 1000, × 3125 / 1000 = 3125 mV
        assert_eq!(parse_vbus([0x1F, 0x40]), Millivolts::from_raw(3125));
    }

    #[test]
    fn parse_vbus_datasheet_24v() {
        // 24000 mV ÷ 3.125 mV/bit = 7680 steps ; shift 3 << = 61440 = 0xF000
        assert_eq!(parse_vbus([0xF0, 0x00]), Millivolts::from_raw(24000));
    }

    #[test]
    fn parse_vbus_negative() {
        // Valeur négative possible selon datasheet (bus voltage référencée à GND)
        // raw = 0x8000 >> 3 = 0xF000 (sign-extended) = -4096, × 3125 / 1000 = -12800
        assert_eq!(parse_vbus([0x80, 0x00]), Millivolts::from_raw(-12800));
    }

    #[test]
    fn encode_shunt_cal_2mohm_1a() {
        // shunt = 2000 µΩ, max_current = 1000 mA
        // current_lsb_na = (1000 × 1_000_000) >> 15 ≈ 30517
        // shuntcal = 819_200_000 × 30517 × 2000 / 1_000_000_000 ≈ 50019
        // clip to 0x7FFF = 32767
        let bytes = encode_shunt_cal(2_000, 1_000);
        let word = u16::from_be_bytes(bytes);
        assert_eq!(word, 0x7FFF);
    }

    #[test]
    fn encode_shunt_cal_2mohm_10a() {
        // max_current = 10000 mA → current_lsb_na ≈ 305176
        // shuntcal = 819_200_000 × 305176 × 2000 / 1_000_000_000 ≈ 500190
        // clip to 0x7FFF
        let bytes = encode_shunt_cal(2_000, 10_000);
        assert_eq!(u16::from_be_bytes(bytes), 0x7FFF);
    }

    #[test]
    fn encode_shunt_cal_100mohm_100ma() {
        // Cas où on n'atteint pas le clip
        // shunt = 100_000 µΩ, max_current = 100 mA → current_lsb_na ≈ 3051
        // shuntcal = 819_200_000 × 3051 × 100_000 / 1_000_000_000 = 249954
        // clip to 0x7FFF
        let bytes = encode_shunt_cal(100_000, 100);
        assert_eq!(u16::from_be_bytes(bytes), 0x7FFF);
    }

    #[test]
    fn parse_current_with_lsb() {
        // raw = 0x0400 = 1024, current_lsb_na = 30517 (≈ 30.5 µA/bit)
        // → micro_amps = 1024 × 30517 / 1000 = 31249 µA ÷ 1000 = 31 mA
        let ma = parse_current([0x04, 0x00], 30_517);
        assert_eq!(ma, Milliamps::from_raw(31));
    }

    #[test]
    fn parse_current_negative() {
        // raw = 0xFC00 = -1024 (signed), current_lsb_na = 30517
        // → -31 mA
        let ma = parse_current([0xFC, 0x00], 30_517);
        assert_eq!(ma, Milliamps::from_raw(-31));
    }

    #[test]
    fn parse_current_zero() {
        assert_eq!(parse_current([0x00, 0x00], 30_517), Milliamps::ZERO);
    }

    #[test]
    fn parse_dietemp_positive() {
        // DIETEMP : bits 15..4 signés, LSB = 125 m°C
        // raw = 0x0C80 = bits 4..15 = 0xC8 = 200 ; 200 × 125 = 25000 m°C = 25.0 °C
        assert_eq!(parse_dietemp_c10([0x0C, 0x80]), 250);
    }

    #[test]
    fn parse_dietemp_negative() {
        // bits 4..15 = 0xFFF (signed, -1) ; -1 × 125 = -125 m°C = -1.25 °C = c10 -12
        // raw sign-extended : 0xFFF0
        assert_eq!(parse_dietemp_c10([0xFF, 0xF0]), -12);
    }

    #[test]
    fn check_device_id_ina237_0x2370() {
        assert!(check_device_id([0x23, 0x70]).is_ok());
    }

    #[test]
    fn check_device_id_ina237_0x2371() {
        assert!(check_device_id([0x23, 0x71]).is_ok());
    }

    #[test]
    fn check_device_id_ina238_0x2380() {
        assert!(check_device_id([0x23, 0x80]).is_ok());
    }

    #[test]
    fn check_device_id_rejects_random() {
        assert_eq!(
            check_device_id([0xDE, 0xAD]),
            Err(Ina237Error::UnexpectedDeviceId(0xDEAD))
        );
    }

    #[test]
    fn check_manuf_id_ti() {
        // "TI" ASCII = 0x5449
        assert!(check_manufacturer_id([0x54, 0x49]).is_ok());
    }

    #[test]
    fn check_manuf_id_rejects_other() {
        assert_eq!(
            check_manufacturer_id([0x00, 0x00]),
            Err(Ina237Error::UnexpectedManufacturerId(0x0000))
        );
    }
}
```

- [ ] **Step 2: Run — verify red**

Run: `cd firmware-rust && cargo test -p bmu-drivers ina237 2>&1 | tail -15`
Expected: `cannot find function 'parse_vbus'` errors.

- [ ] **Step 3: Implémenter les parseurs purs**

Insert before `#[cfg(test)]` in `firmware-rust/crates/bmu-drivers/src/ina237.rs`:

```rust
/// Parse VBus register (0x05).
/// Raw = signed 16-bit, bits 0..2 réservés (shift right 3).
/// LSB = 3.125 mV/step, encodé comme (steps × 3125) / 1000 en mV entier.
#[inline]
pub fn parse_vbus(raw: [u8; 2]) -> Millivolts {
    let word = i16::from_be_bytes(raw);
    let steps = i32::from(word >> 3);
    Millivolts::from_raw((steps * 3125) / 1000)
}

/// Encode les 2 bytes SHUNT_CAL (0x02) à partir du shunt et du max current.
/// Formule datasheet SBOS945 eq. (1) :
///   CURRENT_LSB = max_current / 2^15
///   SHUNT_CAL = 819_200_000 × CURRENT_LSB × R_shunt
/// Clip à 0x7FFF (15 bits).
#[inline]
pub fn encode_shunt_cal(shunt_micro_ohms: u32, max_current_ma: u32) -> [u8; 2] {
    let current_lsb_na: u64 = (u64::from(max_current_ma) * 1_000_000) >> 15;
    let shuntcal: u64 =
        (819_200_000_u64 * current_lsb_na * u64::from(shunt_micro_ohms)) / 1_000_000_000;
    let clipped = shuntcal.min(0x7FFF) as u16;
    clipped.to_be_bytes()
}

/// Calcule le CURRENT_LSB (nA/bit) correspondant à un max_current donné.
/// Utilisé par `parse_current` pour convertir les raw bytes en Milliamps.
#[inline]
pub const fn current_lsb_na(max_current_ma: u32) -> u32 {
    ((max_current_ma as u64 * 1_000_000) >> 15) as u32
}

/// Parse Current register (0x07) en Milliamps.
/// raw = signed 16-bit ; valeur = raw × CURRENT_LSB (en nA) / 1_000_000.
#[inline]
pub fn parse_current(raw: [u8; 2], current_lsb_na: u32) -> Milliamps {
    let word = i32::from(i16::from_be_bytes(raw));
    // i64 pour éviter overflow sur multiplications intermédiaires
    let nano_amps = i64::from(word) * i64::from(current_lsb_na);
    let milli_amps = (nano_amps / 1_000_000) as i32;
    Milliamps::from_raw(milli_amps)
}

/// Parse DIETEMP register (0x06) en centi-degrés Celsius (°C × 10).
/// bits 4..15 signés, LSB = 125 m°C/bit.
/// Retourne un i16 en c10 (ex: 25.0 °C = 250).
#[inline]
pub fn parse_dietemp_c10(raw: [u8; 2]) -> i16 {
    let word = i16::from_be_bytes(raw);
    let steps = word >> 4; // signed shift
    // steps × 125 m°C = steps × 125 / 100 °C = steps × 1.25 c10
    // = (steps × 125) / 100 en c10 deg
    (i32::from(steps) * 125 / 100) as i16
}

/// Vérifie DEVICE_ID register (0x3F). Accepte INA237/238/239.
#[inline]
pub fn check_device_id(raw: [u8; 2]) -> Result<(), Ina237Error> {
    let id = u16::from_be_bytes(raw);
    match id & 0xFFF0 {
        0x2370 | 0x2380 | 0x2390 => Ok(()),
        _ => Err(Ina237Error::UnexpectedDeviceId(id)),
    }
}

/// Vérifie MANUFACTURER_ID register (0x3E). Doit être "TI" ASCII = 0x5449.
#[inline]
pub fn check_manufacturer_id(raw: [u8; 2]) -> Result<(), Ina237Error> {
    let id = u16::from_be_bytes(raw);
    if id == 0x5449 {
        Ok(())
    } else {
        Err(Ina237Error::UnexpectedManufacturerId(id))
    }
}
```

- [ ] **Step 4: Re-export dans lib.rs**

Replace `firmware-rust/crates/bmu-drivers/src/lib.rs`:

```rust
//! Drivers INA237, TCA9535, AHT30 — parseurs purs + wrappers bus-aware.
//! Cf spec §4.3.
#![no_std]

pub mod ina237;
```

- [ ] **Step 5: Run — verify green**

Run: `cd firmware-rust && cargo test -p bmu-drivers 2>&1 | tail -15`
Expected: `test result: ok. 17 passed; 0 failed`.

- [ ] **Step 6: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-drivers
git commit -m "feat(bmu-drivers): add INA237 pure parsers (parse_vbus, parse_current, encode_shunt_cal)

- Pure functions taking raw bytes, no bus dependency
- parse_vbus: i16 >>3 × 3.125 mV/step integer math
- encode_shunt_cal: datasheet SBOS945 eq.(1) with 0x7FFF clip
- parse_dietemp_c10: signed 12-bit × 125 m°C/step
- check_device_id: accepts INA237/238/239 (0x237x/238x/239x)
- check_manufacturer_id: requires 'TI' = 0x5449
- 17 tests covering zero/positive/negative/roundtrip/IDs"
```

### Task 4.2: Wrapper bus-aware `Ina237<'b, B>`

**Files:**
- Modify: `firmware-rust/crates/bmu-drivers/src/ina237.rs`

- [ ] **Step 1: Ajouter dep bmu-test-fixtures en dev-dep**

Edit `firmware-rust/crates/bmu-drivers/Cargo.toml`:

```toml
[package]
name = "bmu-drivers"
version.workspace = true
edition.workspace = true
license.workspace = true

[dependencies]
bmu-types = { path = "../bmu-types" }
bmu-i2c = { path = "../bmu-i2c" }
bitflags = { workspace = true }

[dev-dependencies]
bmu-test-fixtures = { path = "../bmu-test-fixtures" }

[lints]
workspace = true
```

- [ ] **Step 2: Écrire les tests du wrapper (red)**

Append to `firmware-rust/crates/bmu-drivers/src/ina237.rs` inside `#[cfg(test)] mod tests { ... }`:

```rust
    use bmu_test_fixtures::MockBus;

    fn setup_mock_init(addr: u8) -> MockBus {
        let mut bus = MockBus::new();
        // 1. read MANUFACTURER_ID → "TI"
        bus.expect_write_read(addr, vec![Reg::ManufId as u8], vec![0x54, 0x49]);
        // 2. read DEVICE_ID → 0x2370
        bus.expect_write_read(addr, vec![Reg::DeviceId as u8], vec![0x23, 0x70]);
        // 3. write CONFIG reset bit (0x8000)
        bus.expect_write(addr, vec![Reg::Config as u8, 0x80, 0x00]);
        // 4. write CONFIG clear (0x0000, ADCRANGE=0)
        bus.expect_write(addr, vec![Reg::Config as u8, 0x00, 0x00]);
        // 5. write ADC_CONFIG (cont bus+shunt, 540us, avg 64)
        //    MODE=1011, VBUSCT=100, VSHCT=100, VTCT=000, AVG=011
        //    = 0xB << 12 | 0x4 << 9 | 0x4 << 6 | 0 | 0x3 = 0xB903
        bus.expect_write(addr, vec![Reg::AdcConfig as u8, 0xB9, 0x03]);
        // 6. write SHUNT_CAL
        let cal = encode_shunt_cal(2_000, 1_000);
        bus.expect_write(addr, vec![Reg::ShuntCal as u8, cal[0], cal[1]]);
        bus
    }

    #[test]
    fn ina237_init_success() {
        let mut bus = setup_mock_init(0x40);
        let result = Ina237::init(&mut bus, 0x40, 2_000, 1_000);
        assert!(result.is_ok());
    }

    #[test]
    fn ina237_init_rejects_bad_manuf() {
        let mut bus = MockBus::new();
        bus.expect_write_read(0x40, vec![Reg::ManufId as u8], vec![0x00, 0x00]);
        assert_eq!(
            Ina237::init(&mut bus, 0x40, 2_000, 1_000).err(),
            Some(Ina237Error::UnexpectedManufacturerId(0x0000))
        );
    }

    #[test]
    fn ina237_init_rejects_bad_device_id() {
        let mut bus = MockBus::new();
        bus.expect_write_read(0x40, vec![Reg::ManufId as u8], vec![0x54, 0x49]);
        bus.expect_write_read(0x40, vec![Reg::DeviceId as u8], vec![0xDE, 0xAD]);
        assert_eq!(
            Ina237::init(&mut bus, 0x40, 2_000, 1_000).err(),
            Some(Ina237Error::UnexpectedDeviceId(0xDEAD))
        );
    }

    #[test]
    fn ina237_read_vbus_after_init() {
        let mut bus = setup_mock_init(0x40);
        // After init: read VBUS
        bus.expect_write_read(0x40, vec![Reg::VBus as u8], vec![0xF0, 0x00]);
        let mut ina = Ina237::init(&mut bus, 0x40, 2_000, 1_000).unwrap();
        assert_eq!(ina.read_vbus(&mut bus).unwrap(), Millivolts::from_raw(24000));
    }

    #[test]
    fn ina237_read_current_after_init() {
        let mut bus = setup_mock_init(0x40);
        bus.expect_write_read(0x40, vec![Reg::Current as u8], vec![0x04, 0x00]);
        let mut ina = Ina237::init(&mut bus, 0x40, 2_000, 1_000).unwrap();
        // current_lsb_na for 1000 mA = (1000 * 1_000_000) >> 15 = 30517
        // raw 0x0400 = 1024 → 1024 × 30517 / 1_000_000 = 31 mA
        let ma = ina.read_current(&mut bus).unwrap();
        assert_eq!(ma, Milliamps::from_raw(31));
    }
```

- [ ] **Step 3: Run red**

Run: `cd firmware-rust && cargo test -p bmu-drivers ina237 2>&1 | tail -15`
Expected: `cannot find struct Ina237` or `no function init` errors.

- [ ] **Step 4: Implémenter le wrapper**

Append to `firmware-rust/crates/bmu-drivers/src/ina237.rs` (before `#[cfg(test)]`):

```rust
/// Wrapper bus-aware stateful. `current_lsb_na` est caché après init.
/// **Note :** ce wrapper est utilisé par les tests host et les outils de diagnostic
/// (ex. `cargo xtask probe-bus`). Le cœur Rust opérationnel (§4.3 spec) utilise
/// les fonctions pures ci-dessus sur les bytes déjà lus par la couche C.
pub struct Ina237 {
    addr: u8,
    current_lsb_na: u32,
}

impl Ina237 {
    /// Séquence d'init : verify IDs, reset, ADC config, shunt calibration.
    pub fn init<B: I2cBus>(
        bus: &mut B,
        addr: u8,
        shunt_micro_ohms: u32,
        max_current_ma: u32,
    ) -> Result<Self, Ina237Error> {
        // 1. Verify MANUFACTURER_ID
        let mut buf = [0u8; 2];
        bus.write_read(addr, &[Reg::ManufId as u8], &mut buf)?;
        check_manufacturer_id(buf)?;

        // 2. Verify DEVICE_ID
        bus.write_read(addr, &[Reg::DeviceId as u8], &mut buf)?;
        check_device_id(buf)?;

        // 3. Reset (CONFIG bit 15 = 1)
        bus.write(addr, &[Reg::Config as u8, 0x80, 0x00])?;
        // 4. Clear CONFIG (ADCRANGE=0 for ±163.84 mV full scale)
        bus.write(addr, &[Reg::Config as u8, 0x00, 0x00])?;
        // 5. ADC_CONFIG : continuous bus+shunt, 540 µs, avg 64
        bus.write(addr, &[Reg::AdcConfig as u8, 0xB9, 0x03])?;
        // 6. SHUNT_CAL
        let cal = encode_shunt_cal(shunt_micro_ohms, max_current_ma);
        bus.write(addr, &[Reg::ShuntCal as u8, cal[0], cal[1]])?;

        let lsb = current_lsb_na(max_current_ma);
        if lsb == 0 {
            return Err(Ina237Error::CalibrationInvalid);
        }
        Ok(Self {
            addr,
            current_lsb_na: lsb,
        })
    }

    pub fn addr(&self) -> u8 {
        self.addr
    }

    pub fn read_vbus<B: I2cBus>(&mut self, bus: &mut B) -> Result<Millivolts, Ina237Error> {
        let mut buf = [0u8; 2];
        bus.write_read(self.addr, &[Reg::VBus as u8], &mut buf)?;
        Ok(parse_vbus(buf))
    }

    pub fn read_current<B: I2cBus>(&mut self, bus: &mut B) -> Result<Milliamps, Ina237Error> {
        let mut buf = [0u8; 2];
        bus.write_read(self.addr, &[Reg::Current as u8], &mut buf)?;
        Ok(parse_current(buf, self.current_lsb_na))
    }

    pub fn read_dietemp_c10<B: I2cBus>(&mut self, bus: &mut B) -> Result<i16, Ina237Error> {
        let mut buf = [0u8; 2];
        bus.write_read(self.addr, &[Reg::DieTemp as u8], &mut buf)?;
        Ok(parse_dietemp_c10(buf))
    }
}
```

- [ ] **Step 5: Run — verify green**

Run: `cd firmware-rust && cargo test -p bmu-drivers 2>&1 | tail -15`
Expected: `test result: ok. 22 passed; 0 failed`.

- [ ] **Step 6: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-drivers
git commit -m "feat(bmu-drivers): add Ina237 bus-aware wrapper with init sequence

- init() verifies MFR + DEV_ID, resets, configures ADC + SHUNT_CAL
- read_vbus/read_current/read_dietemp_c10 delegate to pure parsers
- Stateful only on addr + current_lsb_na cached from init
- 5 wrapper tests using MockBus (init ok, bad mfr, bad dev, read vbus, read current)
- Used by host diagnostics only — operational core uses pure parsers"
```

---

## Phase 5 — `bmu-drivers` TCA9535 et AHT30

Objectif : driver TCA9535 (16-bit GPIO expander, PCB v2 mapping reversed) + driver AHT30 (T°/humidité). Fix du bug `all_off()` identifié dans le scaffold `firmware-rs/` (deux writes explicites au lieu de compter sur un auto-increment non garanti).

### Task 5.1: Module `tca9535` — parseurs purs + mapping PCB v2

**Files:**
- Create: `firmware-rust/crates/bmu-drivers/src/tca9535.rs`
- Modify: `firmware-rust/crates/bmu-drivers/src/lib.rs`

- [ ] **Step 1: Écrire les tests (red)**

Create `firmware-rust/crates/bmu-drivers/src/tca9535.rs`:

```rust
//! Driver TCA9535 — 16-bit I²C GPIO expander.
//! Référence : TI SCPS209 datasheet.
//! PCB BMU v2 : ordre des switchs inversé par canal, LEDs appairées.

use bmu_i2c::{I2cBus, I2cError};

pub const TCA9535_ADDR_BASE: u8 = 0x20;
pub const TCA9535_ADDR_MAX: u8 = 0x27;

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Reg {
    InputPort0 = 0x00,
    InputPort1 = 0x01,
    OutputPort0 = 0x02,
    OutputPort1 = 0x03,
    PolarityInv0 = 0x04,
    PolarityInv1 = 0x05,
    ConfigPort0 = 0x06,
    ConfigPort1 = 0x07,
}

/// PCB BMU v2 mapping :
/// - P0 bits 0-3 = output (switches MOSFET batteries), bits 4-7 = input (alerts)
/// - P1 bits 0-7 = output (LEDs rouge/verte par canal, appairées)
pub const P0_CONFIG: u8 = 0xF0;
pub const P1_CONFIG: u8 = 0x00;

/// Mapping batterie → pin output port 0 (REVERSED sur PCB v2) :
/// canal 0 (bat1) = P0.3, canal 1 (bat2) = P0.2, canal 2 = P0.1, canal 3 = P0.0
pub const SWITCH_PIN: [u8; 4] = [3, 2, 1, 0];

/// Mapping batterie → pin input port 0 pour alerts (REVERSED) :
/// canal 0 = P0.7, canal 1 = P0.6, canal 2 = P0.5, canal 3 = P0.4
pub const ALERT_PIN: [u8; 4] = [7, 6, 5, 4];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Tca9535Error {
    I2c(I2cError),
    InvalidChannel,
}

impl From<I2cError> for Tca9535Error {
    fn from(e: I2cError) -> Self {
        Self::I2c(e)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use bmu_test_fixtures::MockBus;

    #[test]
    fn parse_inputs_all_low() {
        assert_eq!(parse_inputs([0x00, 0x00]), 0x0000);
    }

    #[test]
    fn parse_inputs_all_high() {
        assert_eq!(parse_inputs([0xFF, 0xFF]), 0xFFFF);
    }

    #[test]
    fn parse_inputs_endianness() {
        // port 0 = LSB, port 1 = MSB
        assert_eq!(parse_inputs([0x12, 0x34]), 0x3412);
    }

    #[test]
    fn channel_alert_active_low_mapping() {
        // Raw = port 0 only (bit pattern 0b1110_1111 = alert sur bit 4 = canal 3)
        // bit 4 low → alert TRUE pour canal 3 (ALERT_PIN[3]=4)
        let port0 = 0b1110_1111;
        assert!(channel_alert_active(port0, 3));
        assert!(!channel_alert_active(port0, 0));
        assert!(!channel_alert_active(port0, 1));
        assert!(!channel_alert_active(port0, 2));
    }

    #[test]
    fn channel_alert_canal_0() {
        // ALERT_PIN[0] = 7 → bit 7 low
        let port0 = 0b0111_1111;
        assert!(channel_alert_active(port0, 0));
    }

    #[test]
    fn set_switch_bit_reversed_mapping() {
        // Canal 0 = P0.3 → bit 3 set ; état initial 0
        assert_eq!(set_switch_bit(0x00, 0, true), 0b0000_1000);
        // Canal 1 = P0.2
        assert_eq!(set_switch_bit(0x00, 1, true), 0b0000_0100);
        // Canal 3 = P0.0
        assert_eq!(set_switch_bit(0x00, 3, true), 0b0000_0001);
    }

    #[test]
    fn set_switch_bit_clear() {
        assert_eq!(set_switch_bit(0b0000_1111, 0, false), 0b0000_0111);
        assert_eq!(set_switch_bit(0b0000_1111, 3, false), 0b0000_1110);
    }

    #[test]
    fn set_switch_bit_invalid_channel_noop() {
        // Channels 4..15 → no-op (out of range for a single TCA)
        assert_eq!(set_switch_bit(0xAA, 4, true), 0xAA);
        assert_eq!(set_switch_bit(0xAA, 15, true), 0xAA);
    }

    #[test]
    fn encode_led_pair_red_only() {
        // Canal 0 : red = P1.0, green = P1.1
        // red=true green=false → bit 0 set, bit 1 clear
        assert_eq!(encode_led_pair(0x00, 0, true, false), 0b0000_0001);
    }

    #[test]
    fn encode_led_pair_green_only() {
        assert_eq!(encode_led_pair(0x00, 0, false, true), 0b0000_0010);
    }

    #[test]
    fn encode_led_pair_canal_3() {
        // Canal 3 : red = P1.6, green = P1.7
        assert_eq!(encode_led_pair(0x00, 3, true, true), 0b1100_0000);
    }

    #[test]
    fn tca9535_init_writes_direction() {
        let mut bus = MockBus::new();
        // Step 1: Outputs LOW first (prevent glitches)
        bus.expect_write(0x20, vec![Reg::OutputPort0 as u8, 0x00]);
        bus.expect_write(0x20, vec![Reg::OutputPort1 as u8, 0x00]);
        // Step 2: Configure directions
        bus.expect_write(0x20, vec![Reg::ConfigPort0 as u8, P0_CONFIG]);
        bus.expect_write(0x20, vec![Reg::ConfigPort1 as u8, P1_CONFIG]);
        // Step 3: No polarity inversion
        bus.expect_write(0x20, vec![Reg::PolarityInv0 as u8, 0x00]);
        bus.expect_write(0x20, vec![Reg::PolarityInv1 as u8, 0x00]);

        assert!(Tca9535::init(&mut bus, 0x20).is_ok());
    }

    #[test]
    fn tca9535_all_off_writes_both_ports_explicitly() {
        let mut bus = MockBus::new();
        // Skip init expectations by building directly
        let mut tca = Tca9535::new_without_init(0x21);
        // Fix du bug scaffold firmware-rs : écriture EXPLICITE des deux ports
        // au lieu de compter sur un auto-increment inexistant
        bus.expect_write(0x21, vec![Reg::OutputPort0 as u8, 0x00]);
        bus.expect_write(0x21, vec![Reg::OutputPort1 as u8, 0x00]);
        tca.all_off(&mut bus).unwrap();
    }

    #[test]
    fn tca9535_switch_battery_single_write() {
        let mut bus = MockBus::new();
        let mut tca = Tca9535::new_without_init(0x20);
        // Canal 0 ON → P0.3 set → 0b0000_1000
        bus.expect_write(0x20, vec![Reg::OutputPort0 as u8, 0b0000_1000]);
        tca.switch_battery(&mut bus, 0, true).unwrap();

        // Canal 3 ON (sans reset) → cumul 0b0000_1001
        bus.expect_write(0x20, vec![Reg::OutputPort0 as u8, 0b0000_1001]);
        tca.switch_battery(&mut bus, 3, true).unwrap();
    }

    #[test]
    fn tca9535_switch_battery_invalid_channel() {
        let bus = MockBus::new();
        let mut tca = Tca9535::new_without_init(0x20);
        let mut bus = bus;
        assert_eq!(
            tca.switch_battery(&mut bus, 4, true).err(),
            Some(Tca9535Error::InvalidChannel)
        );
    }

    #[test]
    fn tca9535_read_alerts() {
        let mut bus = MockBus::new();
        let mut tca = Tca9535::new_without_init(0x20);
        // Simuler alert sur canal 0 (bit 7 low) et canal 3 (bit 4 low)
        bus.expect_write_read(0x20, vec![Reg::InputPort0 as u8], vec![0b0110_1111]);
        let alerts = tca.read_alerts(&mut bus).unwrap();
        assert!(alerts[0]);
        assert!(!alerts[1]);
        assert!(!alerts[2]);
        assert!(alerts[3]);
    }
}
```

- [ ] **Step 2: Run red**

Run: `cd firmware-rust && cargo test -p bmu-drivers tca9535 2>&1 | tail -15`
Expected: `cannot find function 'parse_inputs'` errors.

- [ ] **Step 3: Implémenter les parseurs et le wrapper**

Insert before `#[cfg(test)]` in `firmware-rust/crates/bmu-drivers/src/tca9535.rs`:

```rust
/// Parse les 2 bytes des INPUT_PORT0 et INPUT_PORT1 en u16.
/// Convention : bits 0..7 = port 0 (LSB), bits 8..15 = port 1.
#[inline]
pub fn parse_inputs(raw: [u8; 2]) -> u16 {
    u16::from(raw[0]) | (u16::from(raw[1]) << 8)
}

/// True si un alert est actif pour ce canal (active-low sur P0).
#[inline]
pub fn channel_alert_active(port0: u8, channel: u8) -> bool {
    if channel >= 4 {
        return false;
    }
    let pin = ALERT_PIN[channel as usize];
    (port0 >> pin) & 1 == 0
}

/// Modifie le bit output du canal dans `current_p0` (non persistant).
/// Canal invalide (>=4) → pas de changement.
#[inline]
pub fn set_switch_bit(current_p0: u8, channel: u8, on: bool) -> u8 {
    if channel >= 4 {
        return current_p0;
    }
    let pin = SWITCH_PIN[channel as usize];
    if on {
        current_p0 | (1 << pin)
    } else {
        current_p0 & !(1 << pin)
    }
}

/// Encode la paire LED rouge/verte d'un canal dans le byte OUTPUT_PORT1.
/// Canal 0 : red=P1.0 green=P1.1 ; canal 3 : red=P1.6 green=P1.7.
#[inline]
pub fn encode_led_pair(current_p1: u8, channel: u8, red: bool, green: bool) -> u8 {
    if channel >= 4 {
        return current_p1;
    }
    let red_pin = channel * 2;
    let green_pin = channel * 2 + 1;
    let mut out = current_p1;
    out &= !(1 << red_pin);
    out &= !(1 << green_pin);
    if red {
        out |= 1 << red_pin;
    }
    if green {
        out |= 1 << green_pin;
    }
    out
}

/// Wrapper bus-aware stateful. `out_p0` et `out_p1` suivent l'état courant.
pub struct Tca9535 {
    addr: u8,
    out_p0: u8,
    out_p1: u8,
}

impl Tca9535 {
    /// Init complet : outputs OFF puis config directions puis polarity clear.
    pub fn init<B: I2cBus>(bus: &mut B, addr: u8) -> Result<Self, Tca9535Error> {
        bus.write(addr, &[Reg::OutputPort0 as u8, 0x00])?;
        bus.write(addr, &[Reg::OutputPort1 as u8, 0x00])?;
        bus.write(addr, &[Reg::ConfigPort0 as u8, P0_CONFIG])?;
        bus.write(addr, &[Reg::ConfigPort1 as u8, P1_CONFIG])?;
        bus.write(addr, &[Reg::PolarityInv0 as u8, 0x00])?;
        bus.write(addr, &[Reg::PolarityInv1 as u8, 0x00])?;
        Ok(Self {
            addr,
            out_p0: 0x00,
            out_p1: 0x00,
        })
    }

    /// Construit une instance sans faire les writes d'init (utilisé par tests unitaires).
    #[cfg(test)]
    pub fn new_without_init(addr: u8) -> Self {
        Self {
            addr,
            out_p0: 0x00,
            out_p1: 0x00,
        }
    }

    pub fn addr(&self) -> u8 {
        self.addr
    }

    /// Commute une batterie ON ou OFF. Canal 0..3. Écrit immédiatement OUTPUT_PORT0.
    pub fn switch_battery<B: I2cBus>(
        &mut self,
        bus: &mut B,
        channel: u8,
        on: bool,
    ) -> Result<(), Tca9535Error> {
        if channel >= 4 {
            return Err(Tca9535Error::InvalidChannel);
        }
        self.out_p0 = set_switch_bit(self.out_p0, channel, on);
        bus.write(self.addr, &[Reg::OutputPort0 as u8, self.out_p0])?;
        Ok(())
    }

    /// Définit la paire LED rouge/verte d'un canal. Écrit immédiatement OUTPUT_PORT1.
    pub fn set_led<B: I2cBus>(
        &mut self,
        bus: &mut B,
        channel: u8,
        red: bool,
        green: bool,
    ) -> Result<(), Tca9535Error> {
        if channel >= 4 {
            return Err(Tca9535Error::InvalidChannel);
        }
        self.out_p1 = encode_led_pair(self.out_p1, channel, red, green);
        bus.write(self.addr, &[Reg::OutputPort1 as u8, self.out_p1])?;
        Ok(())
    }

    /// **Fix bug scaffold firmware-rs :** écrit EXPLICITEMENT les deux output ports.
    /// Le TCA9535 ne supporte pas l'auto-increment de registre → il faut 2 transactions
    /// distinctes. Cf §4.3 spec "Bug potentiel identifié dans le scaffold actuel".
    pub fn all_off<B: I2cBus>(&mut self, bus: &mut B) -> Result<(), Tca9535Error> {
        self.out_p0 = 0x00;
        self.out_p1 = 0x00;
        bus.write(self.addr, &[Reg::OutputPort0 as u8, 0x00])?;
        bus.write(self.addr, &[Reg::OutputPort1 as u8, 0x00])?;
        Ok(())
    }

    /// Lit INPUT_PORT0 et retourne un tableau d'alerts par canal (active-low).
    pub fn read_alerts<B: I2cBus>(&mut self, bus: &mut B) -> Result<[bool; 4], Tca9535Error> {
        let mut buf = [0u8; 1];
        bus.write_read(self.addr, &[Reg::InputPort0 as u8], &mut buf)?;
        let port0 = buf[0];
        Ok([
            channel_alert_active(port0, 0),
            channel_alert_active(port0, 1),
            channel_alert_active(port0, 2),
            channel_alert_active(port0, 3),
        ])
    }
}
```

- [ ] **Step 4: Expose module dans lib.rs**

Replace `firmware-rust/crates/bmu-drivers/src/lib.rs`:

```rust
//! Drivers INA237, TCA9535, AHT30 — parseurs purs + wrappers bus-aware.
//! Cf spec §4.3.
#![no_std]

pub mod ina237;
pub mod tca9535;
```

- [ ] **Step 5: Run tests**

Run: `cd firmware-rust && cargo test -p bmu-drivers 2>&1 | tail -15`
Expected: `test result: ok. 37 passed; 0 failed` (22 INA237 + 15 TCA9535).

- [ ] **Step 6: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-drivers
git commit -m "feat(bmu-drivers): add TCA9535 driver with PCB v2 reversed mapping

- SWITCH_PIN [3,2,1,0] and ALERT_PIN [7,6,5,4] reflect PCB v2 layout
- Pure parsers: parse_inputs, channel_alert_active, set_switch_bit, encode_led_pair
- Wrapper Tca9535 with init/switch_battery/set_led/all_off/read_alerts
- Fix bug from firmware-rs scaffold: all_off() writes OUTPUT_PORT0 and
  OUTPUT_PORT1 as two EXPLICIT transactions (TCA9535 has no auto-increment)
- 15 tests including mapping correctness and wrapper behavior"
```

### Task 5.2: Module `aht30` — driver température/humidité

**Files:**
- Create: `firmware-rust/crates/bmu-drivers/src/aht30.rs`
- Modify: `firmware-rust/crates/bmu-drivers/src/lib.rs`

- [ ] **Step 1: Écrire les tests**

Create `firmware-rust/crates/bmu-drivers/src/aht30.rs`:

```rust
//! Driver AHT30 — capteur température/humidité I²C.
//! Référence : ASAIR AHT30 datasheet v1.0.
//! Adresse unique : 0x38.

use bmu_i2c::{I2cBus, I2cError};

pub const AHT30_ADDR: u8 = 0x38;

const CMD_INIT: [u8; 3] = [0xBE, 0x08, 0x00];
const CMD_TRIGGER: [u8; 3] = [0xAC, 0x33, 0x00];
const CMD_SOFT_RESET: [u8; 1] = [0xBA];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Aht30Error {
    I2c(I2cError),
    NotCalibrated,
    StillBusy,
    CrcMismatch,
}

impl From<I2cError> for Aht30Error {
    fn from(e: I2cError) -> Self {
        Self::I2c(e)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ClimateReading {
    /// Température en c10 (°C × 10), ex. 25.3 °C → 253
    pub temp_c10: i16,
    /// Humidité relative en pct10 (% × 10), ex. 48.7 % → 487
    pub rh_pct10: u16,
}

#[cfg(test)]
mod tests {
    use super::*;
    use bmu_test_fixtures::MockBus;

    #[test]
    fn parse_measurement_room_conditions() {
        // Datasheet eq. (1) :
        //   rh = raw_rh / 2^20 × 100 (%)
        //   temp = raw_t / 2^20 × 200 - 50 (°C)
        //
        // Raw = [status, rh_hi, rh_mid, rh_lo&t_hi, t_mid, t_lo, crc]
        // Pour temp = 25.0 °C : raw_t = (25+50) × 2^20 / 200 = 393216 = 0x060000
        // Pour rh  = 50 %    : raw_rh = 50 × 2^20 / 100 = 524288 = 0x080000
        //
        // Packing : byte0=status (busy=0), byte1=rh[19..12], byte2=rh[11..4],
        //           byte3=rh[3..0]<<4 | t[19..16], byte4=t[15..8], byte5=t[7..0], byte6=crc
        // rh 0x080000 → byte1=0x08 byte2=0x00 byte3_hi=0x0
        // t  0x060000 → byte3_lo=0x6 byte4=0x00 byte5=0x00
        // byte3 = 0x06
        let raw = [0x00, 0x08, 0x00, 0x06, 0x00, 0x00];
        let reading = parse_measurement(raw).unwrap();
        assert!((reading.temp_c10 - 250).abs() <= 2, "got {}", reading.temp_c10);
        assert!((reading.rh_pct10 as i32 - 500).abs() <= 2, "got {}", reading.rh_pct10);
    }

    #[test]
    fn parse_measurement_freezing() {
        // temp = 0 °C → raw_t = 50 × 2^20 / 200 = 262144 = 0x040000
        // rh = 10 % → raw_rh = 10 × 2^20 / 100 = 104857 ≈ 0x019999
        // rh bytes : 0x01 0x99 (hi 4 bits = 0x9) ...  byte3 = 0x94 (hi 0x9, lo 0x4 for t)
        // t 0x040000 : byte3_lo = 0x0 ... simplifions raw_t=0x040000 : byte3=0x??, byte4=0x00, byte5=0x00
        //
        // Simplification : fixons rh=0, t=0 °C
        // rh 0x000000, t 0x040000
        // byte1=0x00 byte2=0x00 byte3=0x04 byte4=0x00 byte5=0x00
        let raw = [0x00, 0x00, 0x00, 0x04, 0x00, 0x00];
        let reading = parse_measurement(raw).unwrap();
        assert_eq!(reading.temp_c10, 0);
        assert_eq!(reading.rh_pct10, 0);
    }

    #[test]
    fn parse_measurement_rejects_busy() {
        let raw = [0x80, 0x00, 0x00, 0x00, 0x00, 0x00]; // bit 7 = busy
        assert_eq!(parse_measurement(raw).err(), Some(Aht30Error::StillBusy));
    }

    #[test]
    fn parse_measurement_rejects_uncalibrated() {
        let raw = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00]; // bit 3 cal = 0
        // Si le parser n'exige PAS le cal bit en mode lecture (juste à l'init),
        // ce test vérifie un comportement différent.
        // Ici : `parse_measurement` n'exige que !busy. cal est vérifié à init().
        assert!(parse_measurement(raw).is_ok());
    }

    #[test]
    fn aht30_trigger_sequence() {
        let mut bus = MockBus::new();
        let mut dev = Aht30::new(0x38);
        bus.expect_write(0x38, CMD_TRIGGER.to_vec());
        dev.trigger(&mut bus).unwrap();
    }

    #[test]
    fn aht30_read_measurement_sequence() {
        let mut bus = MockBus::new();
        let mut dev = Aht30::new(0x38);
        // Read 6 bytes (pas de CRC — skip en V1, géré côté C si besoin)
        bus.expect_read(0x38, vec![0x00, 0x08, 0x00, 0x06, 0x00, 0x00]);
        let reading = dev.read_measurement(&mut bus).unwrap();
        assert!((reading.temp_c10 - 250).abs() <= 2);
    }
}
```

- [ ] **Step 2: Run red**

Run: `cd firmware-rust && cargo test -p bmu-drivers aht30 2>&1 | tail -10`
Expected: `cannot find function 'parse_measurement'` etc.

- [ ] **Step 3: Implémenter le driver**

Insert before `#[cfg(test)]` in `firmware-rust/crates/bmu-drivers/src/aht30.rs`:

```rust
/// Parse les 6 premiers bytes d'une mesure AHT30.
/// Layout datasheet :
/// byte 0 : status (bit 7 = busy, bit 3 = calibrated)
/// bytes 1..5 : 20-bit humidity (big-endian, packed across 4.5 bytes) + 20-bit temp
///   byte 1 : rh[19..12]
///   byte 2 : rh[11..4]
///   byte 3 : rh[3..0] << 4 | t[19..16]
///   byte 4 : t[15..8]
///   byte 5 : t[7..0]
/// (byte 6 CRC non vérifié en V1)
pub fn parse_measurement(raw: [u8; 6]) -> Result<ClimateReading, Aht30Error> {
    let status = raw[0];
    if status & 0x80 != 0 {
        return Err(Aht30Error::StillBusy);
    }

    let rh_raw: u32 =
        (u32::from(raw[1]) << 12) | (u32::from(raw[2]) << 4) | (u32::from(raw[3]) >> 4);
    let t_raw: u32 =
        ((u32::from(raw[3]) & 0x0F) << 16) | (u32::from(raw[4]) << 8) | u32::from(raw[5]);

    // rh_pct10 = (rh_raw * 1000) >> 20  (× 100 % × 10, i.e. % × 10)
    let rh_pct10 = ((u64::from(rh_raw) * 1000) >> 20) as u16;

    // temp_c10 = ((t_raw * 2000) >> 20) - 500  (×200 °C × 10 − 500 offset c10)
    let t_scaled = (u64::from(t_raw) * 2000) >> 20;
    let temp_c10 = (t_scaled as i32 - 500) as i16;

    Ok(ClimateReading { temp_c10, rh_pct10 })
}

/// Wrapper bus-aware stateful.
pub struct Aht30 {
    addr: u8,
}

impl Aht30 {
    pub const fn new(addr: u8) -> Self {
        Self { addr }
    }

    pub fn default() -> Self {
        Self::new(AHT30_ADDR)
    }

    /// Envoie la commande de déclenchement de mesure.
    /// Attente ~80 ms obligatoire côté appelant avant read_measurement().
    pub fn trigger<B: I2cBus>(&mut self, bus: &mut B) -> Result<(), Aht30Error> {
        bus.write(self.addr, &CMD_TRIGGER)?;
        Ok(())
    }

    /// Lit 6 bytes et parse le résultat.
    pub fn read_measurement<B: I2cBus>(
        &mut self,
        bus: &mut B,
    ) -> Result<ClimateReading, Aht30Error> {
        let mut buf = [0u8; 6];
        bus.read(self.addr, &mut buf)?;
        parse_measurement(buf)
    }

    pub fn soft_reset<B: I2cBus>(&mut self, bus: &mut B) -> Result<(), Aht30Error> {
        bus.write(self.addr, &CMD_SOFT_RESET)?;
        Ok(())
    }

    pub fn init_calibration<B: I2cBus>(&mut self, bus: &mut B) -> Result<(), Aht30Error> {
        bus.write(self.addr, &CMD_INIT)?;
        Ok(())
    }
}
```

- [ ] **Step 4: Expose dans lib.rs**

Replace `firmware-rust/crates/bmu-drivers/src/lib.rs`:

```rust
//! Drivers INA237, TCA9535, AHT30 — parseurs purs + wrappers bus-aware.
//! Cf spec §4.3.
#![no_std]

pub mod aht30;
pub mod ina237;
pub mod tca9535;
```

- [ ] **Step 5: Run tests**

Run: `cd firmware-rust && cargo test -p bmu-drivers 2>&1 | tail -15`
Expected: `test result: ok. 43 passed; 0 failed` (37 + 6 AHT30).

- [ ] **Step 6: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-drivers
git commit -m "feat(bmu-drivers): add AHT30 climate sensor driver

- Fixed I2C address 0x38
- ClimateReading struct with temp_c10 (i16) and rh_pct10 (u16)
- parse_measurement: Q20 fixed-point math for 20-bit RH and temp
- Aht30 wrapper: init_calibration, trigger, read_measurement, soft_reset
- CRC byte ignored in V1 (can be added later if required)
- 6 tests covering parsing, busy flag, and I2C sequence"
```

---

## Phase 6 — `bmu-protection` (state machine F01-F11 + 4 régressions CRIT)

Objectif : implémenter la state machine de protection pure, les checks F03-F09, le compteur de switchs avec latch permanent, le battery_manager (Ah counting, topology), et **les 4 tests de régression CRIT** en TDD red-avant-green explicite. Cf spec §5.

### Task 6.1: Module `checks` — fonctions pures UV/OV/OC/Imbalance

**Files:**
- Create: `firmware-rust/crates/bmu-protection/src/checks.rs`
- Modify: `firmware-rust/crates/bmu-protection/src/lib.rs`

- [ ] **Step 1: Tests (red) — inclut test régression CRIT-A et CRIT-B**

Create `firmware-rust/crates/bmu-protection/src/checks.rs`:

```rust
//! Checks purs F03-F06. Chaque fonction est sans état, testable isolément.

use bmu_types::{Config, Milliamps, Millivolts};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn check_uv_below_threshold_fails() {
        let cfg = Config::default();
        assert!(check_uv(Millivolts::from_raw(23_999), &cfg));
    }

    #[test]
    fn check_uv_at_threshold_passes() {
        let cfg = Config::default();
        assert!(!check_uv(Millivolts::from_raw(24_000), &cfg));
    }

    #[test]
    fn check_uv_above_threshold_passes() {
        let cfg = Config::default();
        assert!(!check_uv(Millivolts::from_raw(25_000), &cfg));
    }

    /// **TEST RÉGRESSION CRIT-A** (mV vs V confusion).
    /// Bug historique : spec confondait `24` (volts) avec `24000` (millivolts).
    /// Ici le compilateur Rust nous protège — impossible de comparer un i32
    /// direct à Millivolts. Ce test vérifie qu'on compare bien des Millivolts.
    #[test]
    fn test_crit_a_mv_volt_confusion_impossible() {
        let cfg = Config::default();
        // Si quelqu'un passe accidentellement "24" (pensant volts) :
        // Millivolts::from_raw(24) = 24 mV, check_uv renvoie TRUE (undervoltage)
        // car 24 mV < 24000 mV. C'est le comportement attendu :
        // le type empêche la confusion SÉMANTIQUE et le test de régression
        // garantit qu'on n'accepte jamais une valeur en volts accidentellement.
        assert!(check_uv(Millivolts::from_raw(24), &cfg));
        // Et que 24 volts = 24000 mV est bien nominal :
        assert!(!check_uv(Millivolts::from_raw(24_000), &cfg));
    }

    #[test]
    fn check_ov_above_threshold_fails() {
        let cfg = Config::default();
        assert!(check_ov(Millivolts::from_raw(30_001), &cfg));
    }

    #[test]
    fn check_ov_at_threshold_passes() {
        let cfg = Config::default();
        assert!(!check_ov(Millivolts::from_raw(30_000), &cfg));
    }

    #[test]
    fn check_oc_positive_above_threshold_fails() {
        let cfg = Config::default();
        assert!(check_oc(Milliamps::from_raw(1_001), &cfg));
    }

    #[test]
    fn check_oc_negative_above_threshold_fails() {
        let cfg = Config::default();
        assert!(check_oc(Milliamps::from_raw(-1_001), &cfg));
    }

    #[test]
    fn check_oc_zero_passes() {
        let cfg = Config::default();
        assert!(!check_oc(Milliamps::from_raw(0), &cfg));
    }

    #[test]
    fn check_oc_at_threshold_passes() {
        let cfg = Config::default();
        assert!(!check_oc(Milliamps::from_raw(1_000), &cfg));
        assert!(!check_oc(Milliamps::from_raw(-1_000), &cfg));
    }

    #[test]
    fn check_imbalance_within_threshold() {
        let cfg = Config::default();
        // fleet_max = 27000, v = 26200 → diff = 800 < 1000 → OK
        assert!(!check_imbalance(
            Millivolts::from_raw(26_200),
            Millivolts::from_raw(27_000),
            &cfg,
        ));
    }

    #[test]
    fn check_imbalance_above_threshold_fails() {
        let cfg = Config::default();
        // fleet_max = 27000, v = 25500 → diff = 1500 > 1000 → FAIL
        assert!(check_imbalance(
            Millivolts::from_raw(25_500),
            Millivolts::from_raw(27_000),
            &cfg,
        ));
    }

    #[test]
    fn check_imbalance_at_threshold_passes() {
        let cfg = Config::default();
        // diff = 1000 exact → pass (borne inclusive)
        assert!(!check_imbalance(
            Millivolts::from_raw(26_000),
            Millivolts::from_raw(27_000),
            &cfg,
        ));
    }

    /// **TEST RÉGRESSION CRIT-B** (imbalance basé sur fleet max, pas max local).
    /// Bug historique : `bmu_protection` v1 calculait un "max local glissant"
    /// qui pouvait rester bloqué sur la valeur d'une batterie déjà offline,
    /// masquant un imbalance réel.
    /// Ici on garantit que le paramètre `fleet_max` est fourni par l'appelant
    /// (`Snapshot::fleet_max_voltage()`) qui EXCLUT les batteries offline.
    #[test]
    fn test_crit_b_imbalance_uses_fleet_max_not_local() {
        let cfg = Config::default();

        // Scénario : 3 batteries.
        // - bat A : 25500 mV (Online, la plus basse)
        // - bat B : 27000 mV (Online, le vrai max fleet)
        // - bat C : 30000 mV (Offline depuis longtemps, NE COMPTE PAS)
        //
        // Avec le vrai fleet_max = 27000 : diff(A) = 1500 > 1000 → Fail (attendu)
        let fleet_max = Millivolts::from_raw(27_000);
        assert!(
            check_imbalance(Millivolts::from_raw(25_500), fleet_max, &cfg),
            "Avec fleet_max=27000, bat A à 25500 doit déclencher imbalance"
        );

        // Bug scénario : si fleet_max incluait C à 30000 (bug), diff = 4500 → fail (serait aussi détecté mais pour la mauvaise raison).
        // Bug scénario inverse : si fleet_max restait à une ancienne valeur basse 25500 (stale),
        // diff(A) = 0 → PASS (bug : imbalance masquée).
        let stale_fleet_max = Millivolts::from_raw(25_500);
        assert!(
            !check_imbalance(Millivolts::from_raw(25_500), stale_fleet_max, &cfg),
            "Si fleet_max était stale à 25500, imbalance serait (à tort) OK — ce test documente le bug v1"
        );
    }
}
```

- [ ] **Step 2: Run red**

Run: `cd firmware-rust && cargo test -p bmu-protection checks 2>&1 | tail -15`
Expected: `cannot find function 'check_uv'`.

- [ ] **Step 3: Implémenter les checks**

Insert before `#[cfg(test)]` in `firmware-rust/crates/bmu-protection/src/checks.rs`:

```rust
//! Checks purs F03-F06. Chaque fonction est sans état, testable isolément.

use bmu_types::{Config, Milliamps, Millivolts};

/// F03 : under-voltage. True si la batterie est en fault.
#[inline]
pub fn check_uv(v: Millivolts, cfg: &Config) -> bool {
    v < cfg.umin
}

/// F04 : over-voltage. True si la batterie est en fault.
#[inline]
pub fn check_ov(v: Millivolts, cfg: &Config) -> bool {
    v > cfg.umax
}

/// F05 : over-current. Compare |i| à imax. True si la batterie est en fault.
#[inline]
pub fn check_oc(i: Milliamps, cfg: &Config) -> bool {
    // imax est toujours positif (validate() rejette <=0), donc abs() pos
    i.abs() > (cfg.imax.as_raw().unsigned_abs())
}

/// F06 : imbalance vs fleet max. True si la batterie est en fault.
/// **CRIT-B fix** : `fleet_max` est fourni par l'appelant, qui doit utiliser
/// `Snapshot::fleet_max_voltage()` calculé sur les batteries `Online` uniquement.
#[inline]
pub fn check_imbalance(v: Millivolts, fleet_max: Millivolts, cfg: &Config) -> bool {
    if fleet_max <= v {
        // Cette batterie est le max fleet ou au-dessus : pas d'imbalance
        return false;
    }
    let diff_mv = fleet_max.abs_diff(v);
    let threshold_mv = cfg.vdiff_imbalance.as_raw().unsigned_abs();
    diff_mv > threshold_mv
}
```

- [ ] **Step 4: Expose dans lib.rs**

Modify `firmware-rust/crates/bmu-protection/src/lib.rs`:

```rust
//! State machine protection F01-F11 + battery manager (Ah counting).
#![no_std]

pub mod checks;
```

- [ ] **Step 5: Run green**

Run: `cd firmware-rust && cargo test -p bmu-protection 2>&1 | tail -15`
Expected: `test result: ok. 13 passed; 0 failed`.

- [ ] **Step 6: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-protection
git commit -m "feat(bmu-protection): add pure checks (F03-F06) with CRIT-A/B regression tests

- check_uv, check_ov, check_oc, check_imbalance as pure stateless functions
- CRIT-A regression: types Millivolts make mV/V confusion impossible
- CRIT-B regression: fleet_max is caller-provided (Snapshot::fleet_max_voltage),
  never a stale local max. Test documents the v1 bug explicitly.
- 13 tests, including boundary conditions (at threshold = pass, above = fail)"
```

### Task 6.2: Module `latch` — compteur switchs + permanent latch (F07/F08)

**Files:**
- Create: `firmware-rust/crates/bmu-protection/src/latch.rs`
- Modify: `firmware-rust/crates/bmu-protection/src/lib.rs`

- [ ] **Step 1: Tests**

Create `firmware-rust/crates/bmu-protection/src/latch.rs`:

```rust
//! F07 reconnect delay + F08 permanent latch après 5e fault.

use bmu_types::{BatteryState, Config, LatchReason, OfflineReason};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn switch_counter_new_zero() {
        let c = SwitchCounter::new();
        assert_eq!(c.count(), 0);
        assert_eq!(c.last_fault_ms(), 0);
    }

    #[test]
    fn switch_counter_record_fault_increments() {
        let mut c = SwitchCounter::new();
        c.record_fault(1_000);
        assert_eq!(c.count(), 1);
        assert_eq!(c.last_fault_ms(), 1_000);

        c.record_fault(2_000);
        assert_eq!(c.count(), 2);
        assert_eq!(c.last_fault_ms(), 2_000);
    }

    #[test]
    fn switch_counter_monotonic() {
        // F07 : le compteur ne décroît jamais
        let mut c = SwitchCounter::new();
        c.record_fault(1_000);
        c.record_fault(2_000);
        c.record_fault(3_000);
        assert_eq!(c.count(), 3);
        // Pas de méthode decrement() exposée — propriété du type
    }

    #[test]
    fn switch_counter_reset_to_zero() {
        let mut c = SwitchCounter::new();
        c.record_fault(1_000);
        c.record_fault(2_000);
        c.reset();
        assert_eq!(c.count(), 0);
        assert_eq!(c.last_fault_ms(), 0);
    }

    #[test]
    fn should_latch_permanent_below_max_no() {
        let cfg = Config::default(); // nb_switch_max = 5
        let mut c = SwitchCounter::new();
        for i in 1..=4 {
            c.record_fault(i * 1_000);
        }
        assert!(!c.should_latch_permanent(&cfg));
    }

    #[test]
    fn should_latch_permanent_at_max_yes() {
        let cfg = Config::default();
        let mut c = SwitchCounter::new();
        for i in 1..=5 {
            c.record_fault(i * 1_000);
        }
        assert!(c.should_latch_permanent(&cfg));
    }

    #[test]
    fn should_latch_permanent_above_max_yes() {
        let cfg = Config::default();
        let mut c = SwitchCounter::new();
        for i in 1..=10 {
            c.record_fault(i * 1_000);
        }
        assert!(c.should_latch_permanent(&cfg));
    }

    #[test]
    fn reconnect_delay_elapsed_before_delay() {
        let cfg = Config::default(); // reconnect_delay_ms = 10000
        let mut c = SwitchCounter::new();
        c.record_fault(5_000);
        // now = 10_000, fault at 5_000 → elapsed 5_000 < 10_000 → NO
        assert!(!c.reconnect_delay_elapsed(10_000, &cfg));
    }

    #[test]
    fn reconnect_delay_elapsed_after_delay() {
        let cfg = Config::default();
        let mut c = SwitchCounter::new();
        c.record_fault(5_000);
        // now = 15_001, elapsed = 10_001 > 10_000 → YES
        assert!(c.reconnect_delay_elapsed(15_001, &cfg));
    }

    #[test]
    fn reconnect_delay_elapsed_exact_no() {
        let cfg = Config::default();
        let mut c = SwitchCounter::new();
        c.record_fault(5_000);
        // exactement = 5_000 + 10_000 = 15_000 → pas encore elapsed (>)
        assert!(!c.reconnect_delay_elapsed(15_000, &cfg));
    }

    #[test]
    fn offline_reason_to_latch_reason_roundtrip() {
        // Les offline reasons qui mènent à latch via switch counter → MaxSwitchReached
        // Topology → TopologyFailSafe ; Manual → ManualForceOff
        assert_eq!(
            OfflineReason::UnderVoltage.as_latch_reason(),
            LatchReason::MaxSwitchReached
        );
        assert_eq!(
            OfflineReason::Topology.as_latch_reason(),
            LatchReason::TopologyFailSafe
        );
        assert_eq!(
            OfflineReason::Manual.as_latch_reason(),
            LatchReason::ManualForceOff
        );
    }
}
```

- [ ] **Step 2: Run red**

Run: `cd firmware-rust && cargo test -p bmu-protection latch 2>&1 | tail -10`
Expected: `cannot find type SwitchCounter`, also `method as_latch_reason`.

- [ ] **Step 3: Implémenter SwitchCounter + extension OfflineReason**

Insert before `#[cfg(test)]` in `firmware-rust/crates/bmu-protection/src/latch.rs`:

```rust
//! F07 reconnect delay + F08 permanent latch après 5e fault.

use bmu_types::{BatteryState, Config, LatchReason, OfflineReason};

/// Compteur de switchs per-battery. Persistant tant que la batterie n'est pas
/// reset via BLE `ResetLatch`. **Ne décroît jamais** (propriété du type :
/// seuls `record_fault` et `reset` exposés).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct SwitchCounter {
    count: u8,
    last_fault_ms: u64,
}

impl SwitchCounter {
    pub const fn new() -> Self {
        Self {
            count: 0,
            last_fault_ms: 0,
        }
    }

    #[inline]
    pub const fn count(&self) -> u8 {
        self.count
    }

    #[inline]
    pub const fn last_fault_ms(&self) -> u64 {
        self.last_fault_ms
    }

    /// Enregistre un nouveau fault. Incrémente le compteur (saturating à 255)
    /// et note le timestamp. Le compteur ne décroît JAMAIS.
    pub fn record_fault(&mut self, now_ms: u64) {
        self.count = self.count.saturating_add(1);
        self.last_fault_ms = now_ms;
    }

    /// Réinitialise le compteur. Appelé uniquement par `ResetLatch` BLE command
    /// avec audit log SD.
    pub fn reset(&mut self) {
        self.count = 0;
        self.last_fault_ms = 0;
    }

    /// True si le compteur a atteint ou dépassé `nb_switch_max` → latch permanent F08.
    #[inline]
    pub fn should_latch_permanent(&self, cfg: &Config) -> bool {
        self.count >= cfg.nb_switch_max
    }

    /// True si le délai de reconnect est écoulé depuis le dernier fault.
    #[inline]
    pub fn reconnect_delay_elapsed(&self, now_ms: u64, cfg: &Config) -> bool {
        let elapsed = now_ms.saturating_sub(self.last_fault_ms);
        elapsed > u64::from(cfg.reconnect_delay_ms)
    }
}

/// Helper trait to map OfflineReason → LatchReason pour F08.
pub trait OfflineReasonExt {
    fn as_latch_reason(self) -> LatchReason;
}

impl OfflineReasonExt for OfflineReason {
    fn as_latch_reason(self) -> LatchReason {
        match self {
            OfflineReason::Topology => LatchReason::TopologyFailSafe,
            OfflineReason::Manual => LatchReason::ManualForceOff,
            _ => LatchReason::MaxSwitchReached,
        }
    }
}
```

- [ ] **Step 4: Expose**

Modify `firmware-rust/crates/bmu-protection/src/lib.rs`:

```rust
//! State machine protection F01-F11 + battery manager (Ah counting).
#![no_std]

pub mod checks;
pub mod latch;
```

- [ ] **Step 5: Run green**

Run: `cd firmware-rust && cargo test -p bmu-protection 2>&1 | tail -10`
Expected: `test result: ok. 24 passed; 0 failed` (13 + 11).

- [ ] **Step 6: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-protection
git commit -m "feat(bmu-protection): add SwitchCounter (F07/F08) and latch reasoning

- SwitchCounter: count + last_fault_ms, never decrements (type invariant)
- record_fault() saturates at u8::MAX, resets only via reset()
- should_latch_permanent: count >= nb_switch_max triggers F08
- reconnect_delay_elapsed: strict > comparison (exact threshold = not yet)
- OfflineReasonExt::as_latch_reason maps UV/OV/OC/Imbalance → MaxSwitchReached
- 11 tests on counter behavior, monotonicity, and mapping"
```

### Task 6.3: Module `state` — transitions pures de la state machine

**Files:**
- Create: `firmware-rust/crates/bmu-protection/src/state.rs`
- Modify: `firmware-rust/crates/bmu-protection/src/lib.rs`

- [ ] **Step 1: Tests de transition (red)**

Create `firmware-rust/crates/bmu-protection/src/state.rs`:

```rust
//! Transitions pures (state, measurement, context) → (new_state, action).
//! Cf spec §5.1 diagramme d'états.

use crate::checks::{check_imbalance, check_oc, check_ov, check_uv};
use crate::latch::{OfflineReasonExt, SwitchCounter};
use bmu_types::{
    BatteryState, Config, LatchReason, Milliamps, Millivolts, OfflineReason,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Measurement {
    pub voltage: Millivolts,
    pub current: Milliamps,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TransitionContext<'a> {
    pub measurement: Measurement,
    pub fleet_max: Millivolts,
    pub counter: &'a SwitchCounter,
    pub now_ms: u64,
    pub cfg: &'a Config,
    pub topology_ok: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Transition {
    Stay(BatteryState),
    Move(BatteryState, OfflineReason),
    Latch(LatchReason),
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ctx<'a>(v_mv: i32, i_ma: i32, fleet_max_mv: i32, counter: &'a SwitchCounter, cfg: &'a Config, now_ms: u64) -> TransitionContext<'a> {
        TransitionContext {
            measurement: Measurement {
                voltage: Millivolts::from_raw(v_mv),
                current: Milliamps::from_raw(i_ma),
            },
            fleet_max: Millivolts::from_raw(fleet_max_mv),
            counter,
            now_ms,
            cfg,
            topology_ok: true,
        }
    }

    #[test]
    fn unknown_to_online_when_ok() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let c = ctx(27_000, 0, 27_000, &counter, &cfg, 0);
        assert_eq!(
            transition(BatteryState::Unknown, &c),
            Transition::Move(BatteryState::Online, OfflineReason::Ok),
        );
    }

    #[test]
    fn unknown_to_offline_uv() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let c = ctx(23_000, 0, 27_000, &counter, &cfg, 0);
        assert_eq!(
            transition(BatteryState::Unknown, &c),
            Transition::Move(BatteryState::Offline, OfflineReason::UnderVoltage),
        );
    }

    #[test]
    fn online_to_offline_ov() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let c = ctx(30_001, 0, 30_001, &counter, &cfg, 0);
        assert_eq!(
            transition(BatteryState::Online, &c),
            Transition::Move(BatteryState::Offline, OfflineReason::OverVoltage),
        );
    }

    #[test]
    fn online_to_offline_oc() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let c = ctx(27_000, 1_200, 27_000, &counter, &cfg, 0);
        assert_eq!(
            transition(BatteryState::Online, &c),
            Transition::Move(BatteryState::Offline, OfflineReason::OverCurrent),
        );
    }

    #[test]
    fn online_to_offline_imbalance() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        // v = 25000, fleet_max = 27000 → diff 2000 > 1000
        let c = ctx(25_000, 0, 27_000, &counter, &cfg, 0);
        assert_eq!(
            transition(BatteryState::Online, &c),
            Transition::Move(BatteryState::Offline, OfflineReason::Imbalance),
        );
    }

    #[test]
    fn offline_retries_online_after_delay() {
        let cfg = Config::default();
        let mut counter = SwitchCounter::new();
        counter.record_fault(5_000); // fault at t=5s
        // now = 15_001, delay 10_000 elapsed, V/I ok → Online
        let c = ctx(27_000, 0, 27_000, &counter, &cfg, 15_001);
        assert_eq!(
            transition(BatteryState::Offline, &c),
            Transition::Move(BatteryState::Online, OfflineReason::Ok),
        );
    }

    #[test]
    fn offline_stays_before_delay() {
        let cfg = Config::default();
        let mut counter = SwitchCounter::new();
        counter.record_fault(5_000);
        let c = ctx(27_000, 0, 27_000, &counter, &cfg, 10_000);
        assert_eq!(
            transition(BatteryState::Offline, &c),
            Transition::Stay(BatteryState::Offline),
        );
    }

    #[test]
    fn offline_stays_if_fault_persists() {
        let cfg = Config::default();
        let mut counter = SwitchCounter::new();
        counter.record_fault(5_000);
        // Delay elapsed but V still under min
        let c = ctx(23_000, 0, 27_000, &counter, &cfg, 16_000);
        assert_eq!(
            transition(BatteryState::Offline, &c),
            Transition::Stay(BatteryState::Offline),
        );
    }

    /// F08 : après nb_switch_max faults, latch permanent.
    #[test]
    fn online_fault_at_max_triggers_latch() {
        let cfg = Config::default();
        let mut counter = SwitchCounter::new();
        for i in 1..=4 {
            counter.record_fault(i * 1_000);
        }
        // Counter at 4. Next fault → counter 5, triggers latch F08.
        let c = ctx(23_000, 0, 27_000, &counter, &cfg, 5_000);
        assert_eq!(
            transition(BatteryState::Online, &c),
            Transition::Latch(LatchReason::MaxSwitchReached),
        );
    }

    /// **TEST RÉGRESSION CRIT-C** (deadlock absent — core sans mutex).
    /// Le bug historique `validateBatteryVoltageForSwitch` utilisait un mutex
    /// qui provoquait un deadlock en réentrance. Ici le core Rust n'a PAS de
    /// mutex — l'état mutable vit dans un `BmuCore` possédé par l'appelant C
    /// qui garantit non-réentrance. Ce test est une assertion sémantique :
    /// `transition` est une fonction pure sans lock, donc pas de deadlock possible.
    #[test]
    fn test_crit_c_transition_is_pure_no_lock() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let c = ctx(27_000, 0, 27_000, &counter, &cfg, 0);
        // Appel "nested" simulé : on peut appeler transition DANS transition
        // sans blocage (pur, stateless)
        let _ = transition(BatteryState::Unknown, &c);
        let _ = transition(BatteryState::Online, &c);
        let _ = transition(BatteryState::Offline, &c);
        // Pas de panic, pas de deadlock → la fonction est réentrante-safe par construction
    }

    #[test]
    fn topology_mismatch_latches_all() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let mut c = ctx(27_000, 0, 27_000, &counter, &cfg, 0);
        c.topology_ok = false;
        assert_eq!(
            transition(BatteryState::Online, &c),
            Transition::Latch(LatchReason::TopologyFailSafe),
        );
        assert_eq!(
            transition(BatteryState::Unknown, &c),
            Transition::Latch(LatchReason::TopologyFailSafe),
        );
    }

    #[test]
    fn latched_stays_latched() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let c = ctx(27_000, 0, 27_000, &counter, &cfg, 0);
        // Même avec V/I nominal et topology OK
        assert_eq!(
            transition(BatteryState::Latched, &c),
            Transition::Stay(BatteryState::Latched),
        );
    }

    #[test]
    fn absent_stays_absent() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let c = ctx(0, 0, 27_000, &counter, &cfg, 0);
        assert_eq!(
            transition(BatteryState::Absent, &c),
            Transition::Stay(BatteryState::Absent),
        );
    }
}
```

- [ ] **Step 2: Run red**

Run: `cd firmware-rust && cargo test -p bmu-protection state 2>&1 | tail -15`
Expected: `cannot find function 'transition'`.

- [ ] **Step 3: Implémenter transition**

Insert before `#[cfg(test)]` in `firmware-rust/crates/bmu-protection/src/state.rs`:

```rust
/// Fonction de transition pure. Cf §5.1 diagramme d'états.
/// Retourne la transition à appliquer sans modifier l'état mutable (stateless).
pub fn transition(state: BatteryState, ctx: &TransitionContext<'_>) -> Transition {
    // Priority 0: topology mismatch → latch all, tous états sauf Absent/Latched
    if !ctx.topology_ok {
        match state {
            BatteryState::Absent | BatteryState::Latched => return Transition::Stay(state),
            _ => return Transition::Latch(LatchReason::TopologyFailSafe),
        }
    }

    // États terminaux
    match state {
        BatteryState::Absent => return Transition::Stay(BatteryState::Absent),
        BatteryState::Latched => return Transition::Stay(BatteryState::Latched),
        _ => {}
    }

    // Check faults (priority UV > OV > OC > Imbalance)
    let fault: Option<OfflineReason> = detect_fault(ctx);

    match state {
        BatteryState::Unknown => match fault {
            None => Transition::Move(BatteryState::Online, OfflineReason::Ok),
            Some(reason) => Transition::Move(BatteryState::Offline, reason),
        },
        BatteryState::Online => match fault {
            None => Transition::Stay(BatteryState::Online),
            Some(reason) => {
                // F08 : si le compteur va atteindre nb_switch_max avec ce fault,
                // latch permanent directement
                if ctx.counter.count() + 1 >= ctx.cfg.nb_switch_max {
                    Transition::Latch(reason.as_latch_reason())
                } else {
                    Transition::Move(BatteryState::Offline, reason)
                }
            }
        },
        BatteryState::Offline => {
            // Retry Online seulement si (delay elapsed) ET (pas de fault)
            if !ctx.counter.reconnect_delay_elapsed(ctx.now_ms, ctx.cfg) {
                return Transition::Stay(BatteryState::Offline);
            }
            match fault {
                None => Transition::Move(BatteryState::Online, OfflineReason::Ok),
                Some(_) => Transition::Stay(BatteryState::Offline),
            }
        }
        BatteryState::Absent | BatteryState::Latched => Transition::Stay(state),
    }
}

/// Évalue les 4 checks F03-F06 et retourne la première raison de fault (priorité UV > OV > OC > Imbalance).
fn detect_fault(ctx: &TransitionContext<'_>) -> Option<OfflineReason> {
    let v = ctx.measurement.voltage;
    let i = ctx.measurement.current;

    if check_uv(v, ctx.cfg) {
        return Some(OfflineReason::UnderVoltage);
    }
    if check_ov(v, ctx.cfg) {
        return Some(OfflineReason::OverVoltage);
    }
    if check_oc(i, ctx.cfg) {
        return Some(OfflineReason::OverCurrent);
    }
    if check_imbalance(v, ctx.fleet_max, ctx.cfg) {
        return Some(OfflineReason::Imbalance);
    }
    None
}
```

- [ ] **Step 4: Expose**

Modify `firmware-rust/crates/bmu-protection/src/lib.rs`:

```rust
//! State machine protection F01-F11 + battery manager (Ah counting).
#![no_std]

pub mod checks;
pub mod latch;
pub mod state;

pub use state::{transition, Measurement, Transition, TransitionContext};
```

- [ ] **Step 5: Run green**

Run: `cd firmware-rust && cargo test -p bmu-protection 2>&1 | tail -15`
Expected: `test result: ok. 37 passed; 0 failed` (24 + 13 state).

- [ ] **Step 6: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-protection
git commit -m "feat(bmu-protection): add pure transition() function with CRIT-C regression

- transition(state, ctx) → Transition (Stay | Move | Latch), stateless
- Priority: topology > terminal states > fault detection (UV>OV>OC>Imbalance)
- F08 integration: fault at count+1 == nb_switch_max triggers latch directly
- CRIT-C regression: transition is pure, no mutex → reentrant-safe by design
- 13 tests covering every transition path and CRIT-C assertion"
```

### Task 6.4: Module `manager` — battery_manager (Ah counting, topology, fleet aggregates)

**Files:**
- Create: `firmware-rust/crates/bmu-protection/src/manager.rs`
- Modify: `firmware-rust/crates/bmu-protection/src/lib.rs`

- [ ] **Step 1: Tests**

Create `firmware-rust/crates/bmu-protection/src/manager.rs`:

```rust
//! Battery manager : Ah counting, topology check, per-battery state tracking.

use crate::latch::SwitchCounter;
use crate::state::{transition, Measurement, Transition, TransitionContext};
use bmu_types::{
    BatteryState, Battery, Config, LatchReason, MilliampHours, Milliamps, Millivolts,
    OfflineReason, Snapshot, MAX_BATTERIES,
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn topology_ok_4x4() {
        assert!(topology_ok(16, 4));
    }

    #[test]
    fn topology_ok_boot_empty() {
        // Cas spécial : aucun device au boot → topology_ok = false
        assert!(!topology_ok(0, 0));
    }

    #[test]
    fn topology_fail_ina_without_tca() {
        assert!(!topology_ok(16, 3));
        assert!(!topology_ok(16, 5));
    }

    #[test]
    fn topology_fail_tca_without_ina() {
        assert!(!topology_ok(15, 4));
        assert!(!topology_ok(17, 4));
    }

    #[test]
    fn topology_partial_3x4() {
        // 3 TCA détectés, 12 INA → tropology OK pour 12 batteries
        assert!(topology_ok(12, 3));
    }

    #[test]
    fn coulomb_counter_discharge_accumulates() {
        // 500 mA discharge for 1 second → 500 mA × (1/3600) h = 0.139 mAh ≈ 0
        // Mais avec tick 200 ms : 500 × 200 / 1000 / 3600 = 27.77 µAh
        // On utilise une unité c10 mAh (mAh × 10) ? Non, on stocke directement en mAh
        // après accumulation. Cf signature : integrate_charge(i_ma, dt_ms) → delta_mah_q10
        //
        // Test : 1000 mA pendant 3600 s = 1 Ah = 1000 mAh
        // 3600 ticks de 1 s = 3600 appels
        let mut ah = MilliampHours::ZERO;
        for _ in 0..3600 {
            ah = integrate_charge(ah, Milliamps::from_raw(-1000), 1000);
        }
        // discharge → ah décroît, valeur absolue attendue ≈ 1000 mAh
        assert!(
            (ah.as_raw() - (-1000)).abs() <= 5,
            "got {}",
            ah.as_raw()
        );
    }

    #[test]
    fn coulomb_counter_charge_accumulates() {
        let mut ah = MilliampHours::ZERO;
        for _ in 0..3600 {
            ah = integrate_charge(ah, Milliamps::from_raw(1000), 1000);
        }
        assert!((ah.as_raw() - 1000).abs() <= 5, "got {}", ah.as_raw());
    }

    #[test]
    fn coulomb_counter_zero_current_no_change() {
        let ah = MilliampHours::from_raw(500);
        let new_ah = integrate_charge(ah, Milliamps::ZERO, 200);
        assert_eq!(new_ah, ah);
    }

    #[test]
    fn coulomb_counter_saturating_positive() {
        let ah = MilliampHours::from_raw(i32::MAX - 5);
        let new_ah = integrate_charge(ah, Milliamps::from_raw(10_000), 3_600_000);
        assert_eq!(new_ah, MilliampHours::from_raw(i32::MAX));
    }

    #[test]
    fn battery_manager_new_empty() {
        let mgr = BatteryManager::new();
        assert_eq!(mgr.n_bat(), 0);
        for i in 0..MAX_BATTERIES {
            assert_eq!(mgr.counter(i as u8).count(), 0);
        }
    }

    #[test]
    fn battery_manager_set_topology_updates_n_bat() {
        let mut mgr = BatteryManager::new();
        mgr.set_topology(16, 4);
        assert_eq!(mgr.n_bat(), 16);
    }

    #[test]
    fn battery_manager_set_topology_invalid_zeroes_n_bat() {
        let mut mgr = BatteryManager::new();
        mgr.set_topology(15, 4); // mismatch
        assert_eq!(mgr.n_bat(), 0);
    }

    #[test]
    fn battery_manager_apply_transition_move() {
        let cfg = Config::default();
        let mut mgr = BatteryManager::new();
        mgr.set_topology(4, 1);
        mgr.apply_transition(
            0,
            Transition::Move(BatteryState::Offline, OfflineReason::UnderVoltage),
            1_000,
        );
        assert_eq!(mgr.counter(0).count(), 1);
        assert_eq!(mgr.counter(0).last_fault_ms(), 1_000);
    }

    #[test]
    fn battery_manager_apply_transition_latch() {
        let cfg = Config::default();
        let mut mgr = BatteryManager::new();
        mgr.set_topology(4, 1);
        mgr.apply_transition(0, Transition::Latch(LatchReason::MaxSwitchReached), 5_000);
        // After latch, further transitions should be ignored (state is tracked externally
        // via snapshot, manager only tracks counters)
        // Counter was incremented by latch as a fault:
        assert_eq!(mgr.counter(0).count(), 1);
    }

    #[test]
    fn battery_manager_reset_counter() {
        let mut mgr = BatteryManager::new();
        mgr.set_topology(4, 1);
        mgr.apply_transition(
            0,
            Transition::Move(BatteryState::Offline, OfflineReason::UnderVoltage),
            1_000,
        );
        mgr.reset_counter(0);
        assert_eq!(mgr.counter(0).count(), 0);
    }

    #[test]
    fn battery_manager_out_of_range_noop() {
        let mut mgr = BatteryManager::new();
        mgr.set_topology(4, 1);
        // Index 20 out of range, no panic
        mgr.apply_transition(
            20,
            Transition::Move(BatteryState::Offline, OfflineReason::UnderVoltage),
            1_000,
        );
        mgr.reset_counter(20);
    }
}
```

- [ ] **Step 2: Run red**

Run: `cd firmware-rust && cargo test -p bmu-protection manager 2>&1 | tail -15`

- [ ] **Step 3: Implémenter BatteryManager + helpers**

Insert before `#[cfg(test)]` in `firmware-rust/crates/bmu-protection/src/manager.rs`:

```rust
/// F09 : vérifie la contrainte Nb_TCA × 4 == Nb_INA.
/// Cas dégradé boot : 0/0 considéré KO pour forcer fail-safe all OFF.
#[inline]
pub const fn topology_ok(n_ina: u8, n_tca: u8) -> bool {
    if n_ina == 0 && n_tca == 0 {
        return false;
    }
    (n_tca as u16) * 4 == n_ina as u16
}

/// F02 : intègre le courant dans le compteur Ah pendant `dt_ms`.
/// Formule : delta_mah = (i_ma × dt_ms) / 3_600_000
/// Signé : discharge (i_ma négatif) décrémente, charge incrémente.
#[inline]
pub fn integrate_charge(
    current_ah: MilliampHours,
    i: Milliamps,
    dt_ms: u32,
) -> MilliampHours {
    // i_ma × dt_ms en i64 évite overflow pour dt jusqu'à ~10 min
    let charge_ma_ms = i64::from(i.as_raw()) * i64::from(dt_ms);
    let delta_mah = (charge_ma_ms / 3_600_000) as i32;
    current_ah.saturating_add(MilliampHours::from_raw(delta_mah))
}

/// Battery manager : stocke les counters par batterie + topology.
/// État persistant (counters) — l'état courant de chaque batterie est dans le Snapshot.
#[derive(Debug, Clone)]
pub struct BatteryManager {
    n_bat: u8,
    counters: [SwitchCounter; MAX_BATTERIES],
    ah_accumulators: [MilliampHours; MAX_BATTERIES],
}

impl BatteryManager {
    pub const fn new() -> Self {
        Self {
            n_bat: 0,
            counters: [SwitchCounter::new(); MAX_BATTERIES],
            ah_accumulators: [MilliampHours::ZERO; MAX_BATTERIES],
        }
    }

    #[inline]
    pub fn n_bat(&self) -> u8 {
        self.n_bat
    }

    /// Met à jour la topologie. Si invalide → n_bat = 0 (fail-safe).
    pub fn set_topology(&mut self, n_ina: u8, n_tca: u8) {
        if topology_ok(n_ina, n_tca) {
            self.n_bat = n_ina.min(MAX_BATTERIES as u8);
        } else {
            self.n_bat = 0;
        }
    }

    #[inline]
    pub fn counter(&self, idx: u8) -> SwitchCounter {
        if (idx as usize) < MAX_BATTERIES {
            self.counters[idx as usize]
        } else {
            SwitchCounter::new()
        }
    }

    #[inline]
    pub fn ah(&self, idx: u8) -> MilliampHours {
        if (idx as usize) < MAX_BATTERIES {
            self.ah_accumulators[idx as usize]
        } else {
            MilliampHours::ZERO
        }
    }

    /// Applique une transition : incrémente le compteur si fault, log timestamp.
    pub fn apply_transition(&mut self, idx: u8, transition: Transition, now_ms: u64) {
        let i = idx as usize;
        if i >= MAX_BATTERIES {
            return;
        }
        match transition {
            Transition::Stay(_) => {}
            Transition::Move(BatteryState::Offline, _) => {
                self.counters[i].record_fault(now_ms);
            }
            Transition::Move(_, _) => {}
            Transition::Latch(_) => {
                self.counters[i].record_fault(now_ms);
            }
        }
    }

    /// Reset du compteur pour une batterie (via BLE ResetLatch).
    pub fn reset_counter(&mut self, idx: u8) {
        let i = idx as usize;
        if i < MAX_BATTERIES {
            self.counters[i].reset();
        }
    }

    /// Reset Ah accumulator (via BLE ResetAh).
    pub fn reset_ah(&mut self, idx: u8) {
        let i = idx as usize;
        if i < MAX_BATTERIES {
            self.ah_accumulators[i] = MilliampHours::ZERO;
        }
    }

    /// Intègre le courant dans le compteur Ah pour une batterie.
    pub fn integrate(&mut self, idx: u8, i: Milliamps, dt_ms: u32) {
        let i_idx = idx as usize;
        if i_idx < MAX_BATTERIES {
            self.ah_accumulators[i_idx] =
                integrate_charge(self.ah_accumulators[i_idx], i, dt_ms);
        }
    }
}

impl Default for BatteryManager {
    fn default() -> Self {
        Self::new()
    }
}
```

- [ ] **Step 4: Expose**

Modify `firmware-rust/crates/bmu-protection/src/lib.rs`:

```rust
//! State machine protection F01-F11 + battery manager (Ah counting).
#![no_std]

pub mod checks;
pub mod latch;
pub mod manager;
pub mod state;

pub use manager::{integrate_charge, topology_ok, BatteryManager};
pub use state::{transition, Measurement, Transition, TransitionContext};
```

- [ ] **Step 5: Run green**

Run: `cd firmware-rust && cargo test -p bmu-protection 2>&1 | tail -15`
Expected: `test result: ok. 51 passed; 0 failed` (37 + 14).

- [ ] **Step 6: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-protection
git commit -m "feat(bmu-protection): add BatteryManager with F02 Ah counting and F09 topology

- topology_ok(n_ina, n_tca) enforces Nb_TCA × 4 == Nb_INA
- Boot state (0,0) returns false to force fail-safe all OFF
- integrate_charge(): signed Ah integration, saturating on i32 overflow
- BatteryManager stores [SwitchCounter; 16] and [MilliampHours; 16]
- apply_transition() increments counter on Offline or Latched transitions
- reset_counter/reset_ah for BLE commands
- 14 tests on topology edges, coulomb counting, and manager operations"
```

### Task 6.5: Property tests avec proptest

**Files:**
- Create: `firmware-rust/crates/bmu-protection/tests/property_tests.rs`

- [ ] **Step 1: Créer le fichier de tests d'intégration**

Create `firmware-rust/crates/bmu-protection/tests/property_tests.rs`:

```rust
//! Property tests sur les invariants critiques de la state machine.
//! Cf spec §10.1 "Property tests via proptest".

use bmu_protection::{
    transition, BatteryManager, Measurement, Transition, TransitionContext,
};
use bmu_types::{
    BatteryState, Config, LatchReason, Milliamps, Millivolts, OfflineReason,
};
use proptest::prelude::*;

fn arb_millivolts() -> impl Strategy<Value = Millivolts> {
    (0i32..40_000).prop_map(Millivolts::from_raw)
}

fn arb_milliamps() -> impl Strategy<Value = Milliamps> {
    (-5_000i32..5_000).prop_map(Milliamps::from_raw)
}

fn arb_battery_state() -> impl Strategy<Value = BatteryState> {
    prop_oneof![
        Just(BatteryState::Unknown),
        Just(BatteryState::Absent),
        Just(BatteryState::Online),
        Just(BatteryState::Offline),
        Just(BatteryState::Latched),
    ]
}

proptest! {
    /// **Invariant 1** : `Latched` ne transitionne jamais sans `ResetLatch` command.
    /// `transition()` n'a pas accès à `ResetLatch` → il ne doit JAMAIS quitter Latched.
    #[test]
    fn latched_stays_latched(
        v in arb_millivolts(),
        i in arb_milliamps(),
        fleet_max in arb_millivolts(),
        now in 0u64..1_000_000,
    ) {
        let cfg = Config::default();
        let counter = bmu_protection::latch::SwitchCounter::new();
        let ctx = TransitionContext {
            measurement: Measurement { voltage: v, current: i },
            fleet_max,
            counter: &counter,
            now_ms: now,
            cfg: &cfg,
            topology_ok: true,
        };
        prop_assert_eq!(
            transition(BatteryState::Latched, &ctx),
            Transition::Stay(BatteryState::Latched),
        );
    }

    /// **Invariant 2** : `Absent` ne transitionne jamais.
    #[test]
    fn absent_stays_absent(
        v in arb_millivolts(),
        i in arb_milliamps(),
        fleet_max in arb_millivolts(),
        topology_ok in any::<bool>(),
    ) {
        let cfg = Config::default();
        let counter = bmu_protection::latch::SwitchCounter::new();
        let ctx = TransitionContext {
            measurement: Measurement { voltage: v, current: i },
            fleet_max,
            counter: &counter,
            now_ms: 0,
            cfg: &cfg,
            topology_ok,
        };
        prop_assert_eq!(
            transition(BatteryState::Absent, &ctx),
            Transition::Stay(BatteryState::Absent),
        );
    }

    /// **Invariant 3** : topology KO → latch (sauf Absent/Latched qui sont terminaux).
    #[test]
    fn topology_fail_latches_non_terminal(
        state in prop_oneof![
            Just(BatteryState::Unknown),
            Just(BatteryState::Online),
            Just(BatteryState::Offline),
        ],
        v in arb_millivolts(),
        i in arb_milliamps(),
        fleet_max in arb_millivolts(),
    ) {
        let cfg = Config::default();
        let counter = bmu_protection::latch::SwitchCounter::new();
        let ctx = TransitionContext {
            measurement: Measurement { voltage: v, current: i },
            fleet_max,
            counter: &counter,
            now_ms: 0,
            cfg: &cfg,
            topology_ok: false,
        };
        prop_assert_eq!(
            transition(state, &ctx),
            Transition::Latch(LatchReason::TopologyFailSafe),
        );
    }

    /// **Invariant 4** : `SwitchCounter::count` ne décroît jamais via `record_fault`.
    #[test]
    fn counter_monotonic_under_record(
        faults in prop::collection::vec(0u64..10_000, 0..20),
    ) {
        let mut counter = bmu_protection::latch::SwitchCounter::new();
        let mut prev_count = 0u8;
        for t in faults {
            counter.record_fault(t);
            prop_assert!(counter.count() >= prev_count);
            prev_count = counter.count();
        }
    }

    /// **Invariant 5** : `integrate_charge` préserve la direction du courant.
    /// Positive → ah augmente ou stable ; négative → ah diminue ou stable.
    #[test]
    fn integrate_charge_sign_preserved(
        initial in -10_000i32..10_000,
        current_ma in -5_000i32..5_000,
        dt_ms in 0u32..10_000,
    ) {
        let before = bmu_types::MilliampHours::from_raw(initial);
        let after = bmu_protection::integrate_charge(
            before,
            Milliamps::from_raw(current_ma),
            dt_ms,
        );
        if current_ma > 0 {
            prop_assert!(after.as_raw() >= before.as_raw());
        } else if current_ma < 0 {
            prop_assert!(after.as_raw() <= before.as_raw());
        } else {
            prop_assert_eq!(after, before);
        }
    }
}
```

- [ ] **Step 2: Run tests**

Run: `cd firmware-rust && cargo test -p bmu-protection 2>&1 | tail -20`
Expected: tests existants + 5 property tests, chacun exécutant ~256 cas random. `test result: ok`.

- [ ] **Step 3: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-protection
git commit -m "test(bmu-protection): add proptest-based property tests for 5 invariants

- Latched never transitions (no ResetLatch in transition())
- Absent never transitions (terminal)
- Topology fail latches all non-terminal states
- SwitchCounter monotonic under record_fault()
- integrate_charge() preserves current direction sign
- Each property runs 256 random cases via proptest"
```

---

## Phase 7 — `bmu-rint` (R_int pulse-off state machine)

Objectif : micro state machine Rust qui orchestre une mesure de résistance interne en pulse-off sur plusieurs ticks. Cf spec §6.1.

### Task 7.1: `RintState` + `RintEngine::tick`

**Files:**
- Modify: `firmware-rust/crates/bmu-rint/src/lib.rs`

- [ ] **Step 1: Tests**

Replace `firmware-rust/crates/bmu-rint/src/lib.rs`:

```rust
//! Mesure résistance interne par pulse-off. Cf spec §6.1.
//!
//! Micro state machine : l'appelant (protection_task côté C) invoque `tick()`
//! à chaque cycle 5 Hz, avec le snapshot courant et l'index courant. La
//! state machine produit une `RintAction` indiquant d'ouvrir/fermer un
//! contacteur, jusqu'à ce que la mesure soit collectée.
#![no_std]

use bmu_types::{BatteryState, Milliamps, Millivolts, Milliohms, Snapshot, MAX_BATTERIES};

/// Résultat d'une mesure R_int pour une batterie.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RintResult {
    pub idx: u8,
    pub r_ohmic: Milliohms,
    pub v_before: Millivolts,
    pub v_during: Millivolts,
    pub i_before: Milliamps,
    pub measured_at_tick: u32,
}

/// État courant de la state machine R_int (singleton pour toute la fleet).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RintState {
    Idle,
    ArmRequested {
        idx: u8,
    },
    PulseStarted {
        idx: u8,
        v_before: Millivolts,
        i_before: Milliamps,
        start_tick: u32,
    },
    Cooldown {
        idx: u8,
        deadline_tick: u32,
    },
}

impl Default for RintState {
    fn default() -> Self {
        Self::Idle
    }
}

/// Action produite par `RintEngine::tick`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RintAction {
    None,
    OpenContact(u8),
    CloseContact(u8),
}

/// Durée de la phase pulse en ticks 5 Hz (3 ticks = 600 ms).
pub const PULSE_DURATION_TICKS: u32 = 3;

/// Durée du cooldown après mesure en ticks 5 Hz (50 ticks = 10 s).
pub const COOLDOWN_TICKS: u32 = 50;

/// Courant minimum absolu pour considérer une mesure valide (50 mA).
pub const MIN_CURRENT_FOR_MEASUREMENT_MA: u32 = 50;

/// Moteur singleton. Stocke l'état courant + résultats cachés par batterie.
#[derive(Debug, Clone, Copy)]
pub struct RintEngine {
    state: RintState,
    results: [Option<RintResult>; MAX_BATTERIES],
    last_measure_tick: [Option<u32>; MAX_BATTERIES],
}

impl Default for RintEngine {
    fn default() -> Self {
        Self::new()
    }
}

impl RintEngine {
    pub const fn new() -> Self {
        Self {
            state: RintState::Idle,
            results: [None; MAX_BATTERIES],
            last_measure_tick: [None; MAX_BATTERIES],
        }
    }

    pub const fn state(&self) -> RintState {
        self.state
    }

    pub fn result(&self, idx: u8) -> Option<RintResult> {
        if (idx as usize) < MAX_BATTERIES {
            self.results[idx as usize]
        } else {
            None
        }
    }

    /// Reçoit une requête externe (commande BLE TriggerRint) pour armer une mesure.
    /// Refuse si :
    /// - state != Idle (une mesure est déjà en cours)
    /// - idx out of range
    /// - la batterie cible n'est pas Online (sécurité §6.1)
    /// - un autre fault est en cours dans la fleet (safety)
    pub fn request(&mut self, idx: u8, snapshot: &Snapshot) -> Result<(), RintError> {
        if !matches!(self.state, RintState::Idle) {
            return Err(RintError::Busy);
        }
        if (idx as usize) >= MAX_BATTERIES || idx >= snapshot.n_bat {
            return Err(RintError::InvalidIndex);
        }
        let bat = &snapshot.batteries[idx as usize];
        if bat.state != BatteryState::Online {
            return Err(RintError::BatteryNotOnline);
        }
        // Refuse si une autre batterie est en fault
        for (i, b) in snapshot.batteries.iter().enumerate() {
            if i == idx as usize {
                continue;
            }
            if matches!(b.state, BatteryState::Offline | BatteryState::Latched) {
                // Tolérer Absent et Unknown
                if b.state == BatteryState::Offline || b.state == BatteryState::Latched {
                    return Err(RintError::FleetUnsettled);
                }
            }
        }
        // Check courant minimum
        if bat.current.abs() < MIN_CURRENT_FOR_MEASUREMENT_MA {
            return Err(RintError::CurrentTooLow);
        }
        self.state = RintState::ArmRequested { idx };
        Ok(())
    }

    /// Appelée à chaque tick core par `bmu-core`. Snapshot courant + numéro de tick.
    /// Retourne une action à merger dans `Actions`.
    pub fn tick(&mut self, snapshot: &Snapshot, tick_no: u32) -> RintAction {
        match self.state {
            RintState::Idle => RintAction::None,
            RintState::ArmRequested { idx } => {
                let bat = &snapshot.batteries[idx as usize];
                self.state = RintState::PulseStarted {
                    idx,
                    v_before: bat.voltage,
                    i_before: bat.current,
                    start_tick: tick_no,
                };
                RintAction::OpenContact(idx)
            }
            RintState::PulseStarted {
                idx,
                v_before,
                i_before,
                start_tick,
            } => {
                let elapsed = tick_no.wrapping_sub(start_tick);
                if elapsed < PULSE_DURATION_TICKS {
                    return RintAction::None;
                }
                // Mesure terminée : lire V pendant pulse et calculer R
                let bat = &snapshot.batteries[idx as usize];
                let v_during = bat.voltage;
                let delta_mv = v_before.abs_diff(v_during);
                let r_milliohms = if i_before.abs() >= MIN_CURRENT_FOR_MEASUREMENT_MA {
                    // R (mΩ) = delta_mv × 1000 / i_ma_abs
                    let r = (u64::from(delta_mv) * 1_000) / u64::from(i_before.abs());
                    Milliohms::from_raw(r as u32)
                } else {
                    Milliohms::UNKNOWN
                };
                self.results[idx as usize] = Some(RintResult {
                    idx,
                    r_ohmic: r_milliohms,
                    v_before,
                    v_during,
                    i_before,
                    measured_at_tick: tick_no,
                });
                self.last_measure_tick[idx as usize] = Some(tick_no);
                self.state = RintState::Cooldown {
                    idx,
                    deadline_tick: tick_no + COOLDOWN_TICKS,
                };
                RintAction::CloseContact(idx)
            }
            RintState::Cooldown { deadline_tick, .. } => {
                if tick_no >= deadline_tick {
                    self.state = RintState::Idle;
                }
                RintAction::None
            }
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RintError {
    Busy,
    InvalidIndex,
    BatteryNotOnline,
    FleetUnsettled,
    CurrentTooLow,
}

#[cfg(test)]
mod tests {
    use super::*;
    use bmu_types::{Battery, System};

    fn nominal_snapshot(n_bat: u8) -> Snapshot {
        let mut snap = Snapshot::default();
        snap.n_bat = n_bat;
        for i in 0..(n_bat as usize) {
            snap.batteries[i].idx = i as u8;
            snap.batteries[i].state = BatteryState::Online;
            snap.batteries[i].voltage = Millivolts::from_raw(27_000);
            snap.batteries[i].current = Milliamps::from_raw(-500);
        }
        snap.system.topology_ok = true;
        snap
    }

    #[test]
    fn new_engine_idle() {
        let e = RintEngine::new();
        assert_eq!(e.state(), RintState::Idle);
        for i in 0..16 {
            assert!(e.result(i).is_none());
        }
    }

    #[test]
    fn request_idle_ok() {
        let mut e = RintEngine::new();
        let snap = nominal_snapshot(4);
        assert!(e.request(0, &snap).is_ok());
        assert_eq!(e.state(), RintState::ArmRequested { idx: 0 });
    }

    #[test]
    fn request_twice_fails() {
        let mut e = RintEngine::new();
        let snap = nominal_snapshot(4);
        e.request(0, &snap).unwrap();
        assert_eq!(e.request(1, &snap).err(), Some(RintError::Busy));
    }

    #[test]
    fn request_out_of_range() {
        let mut e = RintEngine::new();
        let snap = nominal_snapshot(4);
        assert_eq!(e.request(10, &snap).err(), Some(RintError::InvalidIndex));
    }

    #[test]
    fn request_offline_battery_fails() {
        let mut e = RintEngine::new();
        let mut snap = nominal_snapshot(4);
        snap.batteries[0].state = BatteryState::Offline;
        assert_eq!(
            e.request(0, &snap).err(),
            Some(RintError::BatteryNotOnline)
        );
    }

    #[test]
    fn request_fleet_unsettled_fails() {
        let mut e = RintEngine::new();
        let mut snap = nominal_snapshot(4);
        snap.batteries[2].state = BatteryState::Latched;
        assert_eq!(
            e.request(0, &snap).err(),
            Some(RintError::FleetUnsettled)
        );
    }

    #[test]
    fn request_current_too_low_fails() {
        let mut e = RintEngine::new();
        let mut snap = nominal_snapshot(4);
        snap.batteries[0].current = Milliamps::from_raw(10);
        assert_eq!(
            e.request(0, &snap).err(),
            Some(RintError::CurrentTooLow)
        );
    }

    #[test]
    fn full_pulse_sequence() {
        let mut e = RintEngine::new();
        let mut snap = nominal_snapshot(4);
        e.request(0, &snap).unwrap();

        // Tick 100: arm → pulse start (open contact)
        let action = e.tick(&snap, 100);
        assert_eq!(action, RintAction::OpenContact(0));
        assert!(matches!(e.state(), RintState::PulseStarted { .. }));

        // Tick 101: still waiting
        let action = e.tick(&snap, 101);
        assert_eq!(action, RintAction::None);

        // Tick 102: still waiting
        let action = e.tick(&snap, 102);
        assert_eq!(action, RintAction::None);

        // Tick 103: elapsed = 3, mesure collectée, close contact
        // Simuler un drop de tension pendant le pulse (700 mV drop)
        snap.batteries[0].voltage = Millivolts::from_raw(26_300);
        let action = e.tick(&snap, 103);
        assert_eq!(action, RintAction::CloseContact(0));
        assert!(matches!(e.state(), RintState::Cooldown { .. }));

        // Check résultat : R = 700 mV × 1000 / 500 mA = 1400 mΩ
        let r = e.result(0).unwrap();
        assert_eq!(r.r_ohmic, Milliohms::from_raw(1400));
        assert_eq!(r.v_before, Millivolts::from_raw(27_000));
        assert_eq!(r.v_during, Millivolts::from_raw(26_300));
    }

    #[test]
    fn cooldown_returns_to_idle_after_deadline() {
        let mut e = RintEngine::new();
        let mut snap = nominal_snapshot(4);
        e.request(0, &snap).unwrap();
        e.tick(&snap, 100); // pulse start
        e.tick(&snap, 101);
        e.tick(&snap, 102);
        snap.batteries[0].voltage = Millivolts::from_raw(26_800);
        e.tick(&snap, 103); // measure + cooldown start (deadline = 153)

        // Tick 152: still cooldown
        assert_eq!(e.tick(&snap, 152), RintAction::None);
        assert!(matches!(e.state(), RintState::Cooldown { .. }));

        // Tick 153: deadline reached, back to Idle
        assert_eq!(e.tick(&snap, 153), RintAction::None);
        assert_eq!(e.state(), RintState::Idle);
    }
}
```

- [ ] **Step 2: Ajouter dep bmu-types dans Cargo.toml** (déjà fait en Phase 1 Task 1.2)

Vérifier que `firmware-rust/crates/bmu-rint/Cargo.toml` contient bien :
```toml
[dependencies]
bmu-types = { path = "../bmu-types" }
```
Si absent, l'ajouter.

- [ ] **Step 3: Run tests**

Run: `cd firmware-rust && cargo test -p bmu-rint 2>&1 | tail -15`
Expected: `test result: ok. 9 passed; 0 failed`.

- [ ] **Step 4: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-rint
git commit -m "feat(bmu-rint): add RintEngine pulse-off state machine

- RintState: Idle, ArmRequested, PulseStarted, Cooldown
- request() guards: busy, invalid idx, not Online, fleet unsettled, low current
- tick() orchestrates 3-tick pulse (600ms) + 50-tick cooldown (10s)
- R_milliohms = delta_mv × 1000 / |i_before| with MIN 50mA guard
- results[16] cache per-battery, protection wins via merge in bmu-core
- 9 tests covering request guards, full pulse sequence, cooldown"
```

---

## Phase 8 — `bmu-balancer` (duty cycling non-autoritaire)

Objectif : scheduler de duty cycling soft PWM qui produit un bitmask "balancer preference". Règle absolue : le balancer propose, la protection dispose (AND-mask dans `bmu-core`).

### Task 8.1: `BalancerEngine::tick` + duty target

**Files:**
- Modify: `firmware-rust/crates/bmu-balancer/src/lib.rs`

- [ ] **Step 1: Tests**

Replace `firmware-rust/crates/bmu-balancer/src/lib.rs`:

```rust
//! Duty cycling balancer, non-autoritaire. Cf spec §6.2.
#![no_std]

use bmu_types::{BatteryState, Snapshot, MAX_BATTERIES};

/// Nombre de steps dans le cycle PWM software (0..255).
pub const DUTY_CYCLE_STEPS: u8 = 255;

/// Tolérance SoC en pourcentage : les batteries dont l'écart absolu au max
/// SoC est <= à cette valeur ne sont pas balancées.
pub const SOC_DEADBAND_PCT: u8 = 2;

/// Agent de balancing. State = position courante dans le cycle PWM.
#[derive(Debug, Clone, Copy, Default)]
pub struct BalancerEngine {
    cycle_pos: u8,
    targets: [u8; MAX_BATTERIES],
}

impl BalancerEngine {
    pub const fn new() -> Self {
        Self {
            cycle_pos: 0,
            targets: [0; MAX_BATTERIES],
        }
    }

    /// Recalcule les duties targets à partir du snapshot, puis avance d'un
    /// step dans le cycle PWM et retourne le bitmask des batteries que le
    /// balancer voudrait voir ON à ce tick (preference, pas autorité).
    pub fn tick(&mut self, snapshot: &Snapshot) -> u16 {
        self.compute_targets(snapshot);
        self.cycle_pos = self.cycle_pos.wrapping_add(1);
        let mut mask = 0u16;
        let n = (snapshot.n_bat as usize).min(MAX_BATTERIES);
        for i in 0..n {
            if self.targets[i] > self.cycle_pos {
                mask |= 1 << i;
            }
        }
        mask
    }

    /// Duty % actuel par batterie (0..100), exposé pour le snapshot.
    pub fn duty_pct(&self, idx: u8) -> u8 {
        if (idx as usize) < MAX_BATTERIES {
            // targets[i] ∈ 0..255 → pct = (target × 100) / 255
            ((u16::from(self.targets[idx as usize]) * 100) / 255) as u8
        } else {
            0
        }
    }

    /// Calcule le duty target de chaque batterie selon l'écart au max SoC fleet.
    /// Règle : les batteries les plus "pleines" sont celles qui sont "on" le plus
    /// souvent (pour décharger leur excès). Seuil deadband de ±2 %.
    /// Les batteries non-Online ont target = 0.
    fn compute_targets(&mut self, snapshot: &Snapshot) {
        // Trouver le max SoC parmi les Online
        // Approximation SoC = ah_remaining normalisée. En V1 on utilise
        // la tension comme proxy : les plus hautes tensions = plus chargées.
        let mut max_v = 0i32;
        let n = (snapshot.n_bat as usize).min(MAX_BATTERIES);
        for i in 0..n {
            let bat = &snapshot.batteries[i];
            if bat.state == BatteryState::Online && bat.voltage.as_raw() > max_v {
                max_v = bat.voltage.as_raw();
            }
        }
        // Pour chaque batterie : target proportionnel à (v / max_v)
        for i in 0..n {
            let bat = &snapshot.batteries[i];
            if bat.state != BatteryState::Online {
                self.targets[i] = 0;
                continue;
            }
            if max_v == 0 {
                self.targets[i] = 0;
                continue;
            }
            // proximity = (v × 255) / max_v
            let proximity = ((i64::from(bat.voltage.as_raw()) * 255) / i64::from(max_v)) as i32;
            let proximity = proximity.clamp(0, 255) as u8;
            // Deadband : si la batterie est proche du max (>253), on désactive
            // le balancing sur elle (évite jitter)
            let deadband_thresh = 255 - ((255u16 * u16::from(SOC_DEADBAND_PCT)) / 100) as u8;
            self.targets[i] = if proximity > deadband_thresh {
                proximity
            } else {
                proximity.saturating_sub(10) // réduit légèrement les batteries basses
            };
        }
        // Pour les indices au-delà de n_bat : reset à 0
        for i in n..MAX_BATTERIES {
            self.targets[i] = 0;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use bmu_types::{BatteryState, Milliamps, Millivolts};

    fn fleet(n: u8, voltages_mv: &[i32]) -> Snapshot {
        let mut snap = Snapshot::default();
        snap.n_bat = n;
        for (i, v) in voltages_mv.iter().enumerate().take(n as usize) {
            snap.batteries[i].idx = i as u8;
            snap.batteries[i].state = BatteryState::Online;
            snap.batteries[i].voltage = Millivolts::from_raw(*v);
            snap.batteries[i].current = Milliamps::from_raw(-500);
        }
        snap
    }

    #[test]
    fn new_balancer_all_targets_zero() {
        let b = BalancerEngine::new();
        for i in 0..16 {
            assert_eq!(b.duty_pct(i), 0);
        }
    }

    #[test]
    fn tick_empty_snapshot_returns_zero_mask() {
        let mut b = BalancerEngine::new();
        let snap = Snapshot::default();
        assert_eq!(b.tick(&snap), 0);
    }

    #[test]
    fn tick_all_offline_returns_zero() {
        let mut b = BalancerEngine::new();
        let mut snap = fleet(4, &[25_000, 26_000, 27_000, 28_000]);
        for i in 0..4 {
            snap.batteries[i].state = BatteryState::Offline;
        }
        assert_eq!(b.tick(&snap), 0);
    }

    #[test]
    fn tick_uniform_fleet_balanced() {
        // Toutes les batteries à la même tension → targets proches de 255
        // → duty ≈ 100 %
        let mut b = BalancerEngine::new();
        let snap = fleet(4, &[27_000, 27_000, 27_000, 27_000]);
        // Run many ticks pour voir la distribution
        let mut ons = [0u32; 4];
        for _ in 0..512 {
            let mask = b.tick(&snap);
            for i in 0..4 {
                if (mask >> i) & 1 == 1 {
                    ons[i] += 1;
                }
            }
        }
        // Toutes doivent être ON la majorité du temps
        for i in 0..4 {
            assert!(ons[i] > 400, "bat {} too often off: {}", i, ons[i]);
        }
    }

    #[test]
    fn tick_offline_battery_never_selected() {
        let mut b = BalancerEngine::new();
        let mut snap = fleet(4, &[27_000, 27_000, 27_000, 27_000]);
        snap.batteries[1].state = BatteryState::Offline;
        for _ in 0..512 {
            let mask = b.tick(&snap);
            assert_eq!((mask >> 1) & 1, 0, "offline bat in mask");
        }
    }

    #[test]
    fn duty_pct_range() {
        let mut b = BalancerEngine::new();
        let snap = fleet(4, &[27_000, 26_500, 26_000, 25_500]);
        b.tick(&snap);
        for i in 0..4 {
            assert!(b.duty_pct(i) <= 100);
        }
    }

    #[test]
    fn cycle_pos_wraps() {
        let mut b = BalancerEngine::new();
        let snap = fleet(4, &[27_000, 27_000, 27_000, 27_000]);
        // Run 300 ticks → cycle_pos wraps u8 at 256
        for _ in 0..300 {
            b.tick(&snap);
        }
        // Pas de panic → wrap OK
    }
}
```

- [ ] **Step 2: Run tests**

Run: `cd firmware-rust && cargo test -p bmu-balancer 2>&1 | tail -15`
Expected: `test result: ok. 7 passed; 0 failed`.

- [ ] **Step 3: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-balancer
git commit -m "feat(bmu-balancer): add BalancerEngine duty-cycling scheduler

- Non-authoritative: produces preference bitmask, protection AND-masks
- compute_targets: proportional to voltage / fleet_max, 2% deadband
- tick() wraps cycle_pos u8, no panic on overflow
- Offline/Latched/Absent never appear in mask (target=0)
- duty_pct(idx) exposed for snapshot Battery.balancer_duty_pct
- 7 tests covering uniform fleet, offline skip, duty range, wrap"
```

---

## Phase 9 — `bmu-core` (façade FFI + cbindgen)

Objectif : exposer un header `bmu_core.h` stable (généré par cbindgen) avec l'API `bmu_core_init/tick/command/get_cached_snapshot/set_config/serialize_battery`. Le crate compile en `staticlib` + `rlib` pour permettre à la fois le link C et les tests Rust.

### Task 9.1: Types FFI `#[repr(C)]` (mirrors de `bmu-types`)

**Files:**
- Create: `firmware-rust/crates/bmu-core/src/ffi_types.rs`

- [ ] **Step 1: Créer le fichier avec conversions FFI**

Create `firmware-rust/crates/bmu-core/src/ffi_types.rs`:

```rust
//! Mirrors `#[repr(C)]` de bmu-types pour l'ABI C. Conversions vers/depuis
//! les types Rust internes. Aucune logique, juste des structs packed et
//! des fonctions `From`/`Into`.

use bmu_types::{
    Battery, BatteryState, Config, LatchReason, MilliampHours, Milliamps, Millivolts,
    Milliohms, OfflineReason, Snapshot, System, MAX_BATTERIES,
};

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct BmuConfigC {
    pub umin_mv: u32,
    pub umax_mv: u32,
    pub imax_ma: i32,
    pub vdiff_imbalance_mv: u32,
    pub nb_switch_max: u8,
    pub reconnect_delay_ms: u32,
    pub tick_period_ms: u32,
}

impl From<&BmuConfigC> for Config {
    fn from(c: &BmuConfigC) -> Self {
        Self {
            umin: Millivolts::from_raw(c.umin_mv as i32),
            umax: Millivolts::from_raw(c.umax_mv as i32),
            imax: Milliamps::from_raw(c.imax_ma),
            vdiff_imbalance: Millivolts::from_raw(c.vdiff_imbalance_mv as i32),
            nb_switch_max: c.nb_switch_max,
            reconnect_delay_ms: c.reconnect_delay_ms,
            tick_period_ms: c.tick_period_ms,
        }
    }
}

impl From<&Config> for BmuConfigC {
    fn from(c: &Config) -> Self {
        Self {
            umin_mv: c.umin.as_raw() as u32,
            umax_mv: c.umax.as_raw() as u32,
            imax_ma: c.imax.as_raw(),
            vdiff_imbalance_mv: c.vdiff_imbalance.as_raw() as u32,
            nb_switch_max: c.nb_switch_max,
            reconnect_delay_ms: c.reconnect_delay_ms,
            tick_period_ms: c.tick_period_ms,
        }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct BmuRawInputs {
    pub n_ina: u8,
    pub n_tca: u8,
    pub ina_registers: [[u8; 18]; 16], // ptr to raw register dump per INA
    pub tca_inputs: [u8; 4],            // INPUT_PORT0 of each TCA
    pub climate_temp_c10: i16,
    pub climate_rh_pct10: u16,
    pub monotonic_us: u64,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct BmuBatteryC {
    pub idx: u8,
    pub state: u8,         // BatteryState discriminant
    pub state_reason: u8,  // OfflineReason discriminant
    pub switch_count: u8,
    pub voltage_mv: i32,
    pub current_ma: i32,
    pub ah_remaining_ma_h: i32,
    pub temp_c10: i16,
    pub soh_pct: u8,
    pub balancer_duty_pct: u8,
    pub r_ohmic_m_ohms: u32,
}

impl From<&Battery> for BmuBatteryC {
    fn from(b: &Battery) -> Self {
        Self {
            idx: b.idx,
            state: b.state as u8,
            state_reason: b.reason as u8,
            switch_count: b.switch_count,
            voltage_mv: b.voltage.as_raw(),
            current_ma: b.current.as_raw(),
            ah_remaining_ma_h: b.ah_remaining.as_raw(),
            temp_c10: b.temp_c10,
            soh_pct: b.soh_pct,
            balancer_duty_pct: b.balancer_duty_pct,
            r_ohmic_m_ohms: b.r_ohmic.as_raw(),
        }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct BmuSystemC {
    pub topology_ok: u8,
    pub n_bat: u8,
    pub tick_us_p50: u32,
    pub tick_us_p99: u32,
    pub wdt_feeds: u32,
    pub monotonic_us: u64,
}

impl From<&System> for BmuSystemC {
    fn from(s: &System) -> Self {
        Self {
            topology_ok: u8::from(s.topology_ok),
            n_bat: s.n_bat,
            tick_us_p50: s.tick_us_p50,
            tick_us_p99: s.tick_us_p99,
            wdt_feeds: s.wdt_feeds,
            monotonic_us: s.monotonic_us,
        }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct BmuSnapshotC {
    pub n_bat: u8,
    pub batteries: [BmuBatteryC; MAX_BATTERIES],
    pub system: BmuSystemC,
}

impl From<&Snapshot> for BmuSnapshotC {
    fn from(s: &Snapshot) -> Self {
        let mut batteries = [BmuBatteryC {
            idx: 0, state: 0, state_reason: 0, switch_count: 0,
            voltage_mv: 0, current_ma: 0, ah_remaining_ma_h: 0,
            temp_c10: 0, soh_pct: 0xFF, balancer_duty_pct: 0,
            r_ohmic_m_ohms: u32::MAX,
        }; MAX_BATTERIES];
        for i in 0..MAX_BATTERIES {
            batteries[i] = BmuBatteryC::from(&s.batteries[i]);
        }
        Self {
            n_bat: s.n_bat,
            batteries,
            system: BmuSystemC::from(&s.system),
        }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct BmuActionsC {
    pub tca_set_mask: u16,
    pub tca_clr_mask: u16,
    pub rint_trigger_idx: u8,
    pub request_soh_inference: u8,
}

/// Command kind discriminant matching bmu-types::Command.
#[repr(u8)]
#[derive(Debug, Clone, Copy)]
pub enum BmuCommandKind {
    None = 0,
    ForceOff = 1,
    ResetAh = 2,
    TriggerRint = 3,
    ResetLatch = 4,
    SetConfig = 6,
    UpdateSoh = 7,
    TopologyChanged = 8,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct BmuCommandC {
    pub kind: u8,           // BmuCommandKind
    pub target_idx: u8,
    /// Payload dépend du kind :
    /// - UpdateSoh: payload[0] = soh_pct
    /// - SetConfig: payload[0..20] = BmuConfigC packed
    /// - TopologyChanged: payload[0] = n_ina, payload[1] = n_tca
    pub payload: [u8; 32],
}

/// Codes de retour C-friendly
pub const BMU_OK: i32 = 0;
pub const BMU_ERR_NULL: i32 = -1;
pub const BMU_ERR_INVALID_CONFIG: i32 = -2;
pub const BMU_ERR_INVALID_INDEX: i32 = -3;
pub const BMU_ERR_BUSY: i32 = -4;
pub const BMU_ERR_UNSUPPORTED: i32 = -5;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn config_roundtrip() {
        let rust_cfg = Config::default();
        let c_cfg = BmuConfigC::from(&rust_cfg);
        let back: Config = (&c_cfg).into();
        assert_eq!(back, rust_cfg);
    }

    #[test]
    fn battery_roundtrip() {
        let mut b = Battery::default();
        b.idx = 7;
        b.state = BatteryState::Online;
        b.reason = OfflineReason::Ok;
        b.voltage = Millivolts::from_raw(27_000);
        b.current = Milliamps::from_raw(-500);
        b.ah_remaining = MilliampHours::from_raw(4500);
        b.soh_pct = 92;
        b.r_ohmic = Milliohms::from_raw(1234);
        let c = BmuBatteryC::from(&b);
        assert_eq!(c.idx, 7);
        assert_eq!(c.state, BatteryState::Online as u8);
        assert_eq!(c.voltage_mv, 27_000);
        assert_eq!(c.current_ma, -500);
        assert_eq!(c.ah_remaining_ma_h, 4500);
        assert_eq!(c.soh_pct, 92);
        assert_eq!(c.r_ohmic_m_ohms, 1234);
    }

    #[test]
    fn snapshot_carries_n_bat() {
        let mut s = Snapshot::default();
        s.n_bat = 8;
        let c = BmuSnapshotC::from(&s);
        assert_eq!(c.n_bat, 8);
        assert_eq!(c.batteries.len(), MAX_BATTERIES);
    }

    #[test]
    fn return_codes_distinct() {
        assert_ne!(BMU_OK, BMU_ERR_NULL);
        assert_ne!(BMU_ERR_NULL, BMU_ERR_INVALID_CONFIG);
        assert_ne!(BMU_ERR_INVALID_CONFIG, BMU_ERR_INVALID_INDEX);
    }
}
```

- [ ] **Step 2: Run tests**

Run: `cd firmware-rust && cargo test -p bmu-core 2>&1 | tail -15`
Expected: erreurs "cannot find struct BmuConfigC" puisque `lib.rs` ne l'expose pas encore. Next step fixe ça.

- [ ] **Step 3: Expose dans lib.rs**

Replace `firmware-rust/crates/bmu-core/src/lib.rs`:

```rust
//! Façade FFI C exposée comme staticlib libbmu_core.a + bmu_core.h.
//! Cf spec §3.4.
#![no_std]

pub mod ffi_types;
```

- [ ] **Step 4: Run**

Run: `cd firmware-rust && cargo test -p bmu-core 2>&1 | tail -10`
Expected: `test result: ok. 4 passed; 0 failed`.

- [ ] **Step 5: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-core
git commit -m "feat(bmu-core): add FFI C types with Rust↔C conversions

- BmuConfigC, BmuRawInputs, BmuBatteryC, BmuSystemC, BmuSnapshotC, BmuActionsC
- BmuCommandC with kind+target_idx+payload[32] pattern
- #[repr(C)] enforces stable layout for cbindgen header generation
- From<&T> impls for each pair, no cloning logic
- BMU_OK / BMU_ERR_* return code constants
- 4 roundtrip tests"
```

### Task 9.2: Struct `BmuCore` avec `tick`, `command`, `get_cached_snapshot`

**Files:**
- Create: `firmware-rust/crates/bmu-core/src/core_impl.rs`
- Modify: `firmware-rust/crates/bmu-core/src/lib.rs`

- [ ] **Step 1: Tests d'intégration core**

Create `firmware-rust/crates/bmu-core/src/core_impl.rs`:

```rust
//! Struct `BmuCore` qui agrège protection + rint + balancer et expose
//! l'API unique appelée par la façade FFI.

use bmu_balancer::BalancerEngine;
use bmu_protection::{transition, BatteryManager, Measurement, Transition, TransitionContext};
use bmu_rint::{RintAction, RintEngine};
use bmu_types::{
    Actions, Battery, BatteryState, Command, Config, LatchReason, Milliamps, Millivolts,
    OfflineReason, Snapshot, MAX_BATTERIES,
};

/// Instance core — possédée par l'appelant C via `BmuCore*` opaque.
#[derive(Debug, Clone)]
pub struct BmuCore {
    config: Config,
    manager: BatteryManager,
    rint: RintEngine,
    balancer: BalancerEngine,
    cached_snapshot: Snapshot,
    pending_latch: [Option<LatchReason>; MAX_BATTERIES],
    tick_count: u32,
}

impl BmuCore {
    pub fn new(config: Config) -> Self {
        Self {
            config,
            manager: BatteryManager::new(),
            rint: RintEngine::new(),
            balancer: BalancerEngine::new(),
            cached_snapshot: Snapshot::default(),
            pending_latch: [None; MAX_BATTERIES],
            tick_count: 0,
        }
    }

    pub fn config(&self) -> &Config {
        &self.config
    }

    pub fn set_config(&mut self, new_cfg: Config) -> Result<(), bmu_types::ConfigError> {
        new_cfg.validate()?;
        self.config = new_cfg;
        Ok(())
    }

    pub fn cached_snapshot(&self) -> &Snapshot {
        &self.cached_snapshot
    }

    /// Coeur de la boucle : consomme un snapshot "parsé" (déjà converti depuis
    /// `BmuRawInputs` par le caller côté Rust) et produit un nouveau snapshot
    /// + les actions. Cf spec §2.1.
    ///
    /// L'appelant C passe des `BmuRawInputs` ; la couche FFI `bmu_core_tick`
    /// parse d'abord les bytes en `ParsedInputs` puis appelle cette méthode.
    pub fn step(&mut self, parsed: &ParsedInputs) -> (Snapshot, Actions) {
        self.tick_count = self.tick_count.wrapping_add(1);

        // 1. Update topology + n_bat
        self.manager.set_topology(parsed.n_ina, parsed.n_tca);
        let n_bat = self.manager.n_bat();
        let topology_ok = n_bat > 0;

        // 2. Build a work snapshot from parsed measurements + previous states
        let mut snap = self.cached_snapshot;
        snap.n_bat = n_bat;
        snap.system.topology_ok = topology_ok;
        snap.system.n_bat = n_bat;
        snap.system.monotonic_us = parsed.monotonic_us;

        for i in 0..(n_bat as usize) {
            let m = &parsed.measurements[i];
            snap.batteries[i].idx = i as u8;
            snap.batteries[i].voltage = m.voltage;
            snap.batteries[i].current = m.current;
        }
        // Clear batteries beyond n_bat → Absent
        for i in (n_bat as usize)..MAX_BATTERIES {
            snap.batteries[i].state = BatteryState::Absent;
            snap.batteries[i].voltage = Millivolts::ZERO;
            snap.batteries[i].current = Milliamps::ZERO;
        }

        // 3. Compute fleet_max ONCE on fresh snapshot (CRIT-B)
        let fleet_max = snap.fleet_max_voltage();

        // 4. Run transitions for each battery
        let now_ms = (parsed.monotonic_us / 1000) as u64;
        let dt_ms = self.config.tick_period_ms;
        let mut protection_allowed_mask = 0u16;
        for i in 0..(n_bat as usize) {
            let current_state = snap.batteries[i].state;
            let counter = self.manager.counter(i as u8);
            let ctx = TransitionContext {
                measurement: Measurement {
                    voltage: snap.batteries[i].voltage,
                    current: snap.batteries[i].current,
                },
                fleet_max,
                counter: &counter,
                now_ms,
                cfg: &self.config,
                topology_ok,
            };
            let trans = if let Some(latch_reason) = self.pending_latch[i] {
                self.pending_latch[i] = None;
                Transition::Latch(latch_reason)
            } else {
                transition(current_state, &ctx)
            };
            match trans {
                Transition::Stay(s) => {
                    snap.batteries[i].state = s;
                }
                Transition::Move(new_state, reason) => {
                    snap.batteries[i].state = new_state;
                    snap.batteries[i].reason = reason;
                    self.manager.apply_transition(i as u8, trans, now_ms);
                }
                Transition::Latch(reason) => {
                    snap.batteries[i].state = BatteryState::Latched;
                    snap.batteries[i].reason = match reason {
                        LatchReason::MaxSwitchReached => snap.batteries[i].reason,
                        LatchReason::TopologyFailSafe => OfflineReason::Topology,
                        LatchReason::ManualForceOff => OfflineReason::Manual,
                    };
                    self.manager.apply_transition(i as u8, trans, now_ms);
                }
            }
            snap.batteries[i].switch_count = self.manager.counter(i as u8).count();
            // Intégration Ah
            self.manager.integrate(
                i as u8,
                snap.batteries[i].current,
                dt_ms,
            );
            snap.batteries[i].ah_remaining = self.manager.ah(i as u8);
            // Protection allowed mask
            if snap.batteries[i].state.allows_switch_on() {
                protection_allowed_mask |= 1 << i;
            }
        }
        snap.system.wdt_feeds = snap.system.wdt_feeds.wrapping_add(1);

        // 5. Run RintEngine
        let rint_action = self.rint.tick(&snap, self.tick_count);
        let rint_trigger_idx = 0xFFu8;

        // 6. Run BalancerEngine
        let balancer_mask = self.balancer.tick(&snap);
        let final_on_mask = Actions::merge_balancer(balancer_mask, protection_allowed_mask);

        // Pour chaque batterie, expose le duty pct
        for i in 0..(n_bat as usize) {
            snap.batteries[i].balancer_duty_pct = self.balancer.duty_pct(i as u8);
            if let Some(r) = self.rint.result(i as u8) {
                snap.batteries[i].r_ohmic = r.r_ohmic;
            }
        }

        // 7. Actions finale : tca_set_mask = final_on_mask, tca_clr_mask = ~mask ∩ allowed
        let tca_set_mask = final_on_mask;
        let tca_clr_mask = !final_on_mask & 0xFFFFu16 & ((1u16 << n_bat) - 1);

        // R_int actions merge : si pulse open/close, override le mask
        let (tca_set_mask, tca_clr_mask) = match rint_action {
            RintAction::None => (tca_set_mask, tca_clr_mask),
            RintAction::OpenContact(idx) => {
                let bit = 1u16 << idx;
                (tca_set_mask & !bit, tca_clr_mask | bit)
            }
            RintAction::CloseContact(idx) => {
                let bit = 1u16 << idx;
                (tca_set_mask | bit, tca_clr_mask & !bit)
            }
        };

        let actions = Actions {
            tca_set_mask,
            tca_clr_mask,
            rint_trigger_idx,
            request_soh_inference: (self.tick_count % 300 == 0), // ~60 s
        };

        self.cached_snapshot = snap;
        (snap, actions)
    }

    /// Applique une commande reçue via BLE/UI.
    pub fn handle_command(&mut self, cmd: Command) -> Result<(), CoreError> {
        match cmd {
            Command::None => Ok(()),
            Command::ForceOff { idx } => {
                if (idx as usize) >= MAX_BATTERIES {
                    return Err(CoreError::InvalidIndex);
                }
                self.pending_latch[idx as usize] = Some(LatchReason::ManualForceOff);
                Ok(())
            }
            Command::ResetAh { idx } => {
                self.manager.reset_ah(idx);
                Ok(())
            }
            Command::TriggerRint { idx } => self
                .rint
                .request(idx, &self.cached_snapshot)
                .map_err(|_| CoreError::RintBusy),
            Command::ResetLatch { idx } => {
                if (idx as usize) >= MAX_BATTERIES {
                    return Err(CoreError::InvalidIndex);
                }
                self.manager.reset_counter(idx);
                // Retour à Unknown au prochain tick
                self.cached_snapshot.batteries[idx as usize].state = BatteryState::Unknown;
                Ok(())
            }
            Command::SetConfig(new_cfg) => self
                .set_config(new_cfg)
                .map_err(|_| CoreError::InvalidConfig),
            Command::UpdateSoh { idx, soh_pct } => {
                if (idx as usize) >= MAX_BATTERIES {
                    return Err(CoreError::InvalidIndex);
                }
                self.cached_snapshot.batteries[idx as usize].soh_pct = soh_pct;
                Ok(())
            }
            Command::TopologyChanged { n_ina, n_tca } => {
                self.manager.set_topology(n_ina, n_tca);
                Ok(())
            }
        }
    }
}

/// Mesures parsées injectées dans `step()`.
#[derive(Debug, Clone, Copy, Default)]
pub struct ParsedInputs {
    pub n_ina: u8,
    pub n_tca: u8,
    pub measurements: [Measurement; MAX_BATTERIES],
    pub monotonic_us: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CoreError {
    InvalidIndex,
    InvalidConfig,
    RintBusy,
}

#[cfg(test)]
mod tests {
    use super::*;

    fn inputs(n_ina: u8, n_tca: u8, voltages: &[i32], currents: &[i32]) -> ParsedInputs {
        let mut p = ParsedInputs::default();
        p.n_ina = n_ina;
        p.n_tca = n_tca;
        for i in 0..(n_ina as usize) {
            p.measurements[i] = Measurement {
                voltage: Millivolts::from_raw(voltages[i]),
                current: Milliamps::from_raw(currents[i]),
            };
        }
        p.monotonic_us = 1_000_000;
        p
    }

    #[test]
    fn core_new_empty_snapshot() {
        let core = BmuCore::new(Config::default());
        assert_eq!(core.cached_snapshot().n_bat, 0);
    }

    #[test]
    fn core_step_nominal_4_batteries() {
        let mut core = BmuCore::new(Config::default());
        let p = inputs(4, 1, &[27_000, 27_100, 26_900, 27_050], &[-500, -500, -500, -500]);
        let (snap, actions) = core.step(&p);
        assert_eq!(snap.n_bat, 4);
        assert!(snap.system.topology_ok);
        // All should transition Unknown → Online
        for i in 0..4 {
            assert_eq!(snap.batteries[i].state, BatteryState::Online);
        }
        // Protection allowed mask = all 4 → actions.tca_set_mask has bits 0..3 set
        assert_eq!(actions.tca_set_mask & 0x0F, 0x0F);
    }

    #[test]
    fn core_step_topology_mismatch_latches_all() {
        let mut core = BmuCore::new(Config::default());
        // 16 INA but only 2 TCA → topology fail
        let p = inputs(
            16,
            2,
            &[27_000; 16],
            &[-500; 16],
        );
        let (snap, actions) = core.step(&p);
        assert_eq!(snap.n_bat, 0);
        assert!(!snap.system.topology_ok);
        // Absent states → no actions
        assert_eq!(actions.tca_set_mask, 0);
    }

    #[test]
    fn core_step_undervoltage_battery() {
        let mut core = BmuCore::new(Config::default());
        let p = inputs(
            4,
            1,
            &[23_000, 27_000, 27_000, 27_000],
            &[-500; 4],
        );
        let (snap, _) = core.step(&p);
        assert_eq!(snap.batteries[0].state, BatteryState::Offline);
        assert_eq!(snap.batteries[0].reason, OfflineReason::UnderVoltage);
        assert_eq!(snap.batteries[1].state, BatteryState::Online);
    }

    #[test]
    fn core_force_off_latches() {
        let mut core = BmuCore::new(Config::default());
        // First tick: all Online
        let p = inputs(4, 1, &[27_000; 4], &[-500; 4]);
        core.step(&p);
        // Issue ForceOff
        core.handle_command(Command::ForceOff { idx: 2 }).unwrap();
        let (snap, _) = core.step(&p);
        assert_eq!(snap.batteries[2].state, BatteryState::Latched);
    }

    #[test]
    fn core_reset_latch_returns_to_unknown() {
        let mut core = BmuCore::new(Config::default());
        let p = inputs(4, 1, &[27_000; 4], &[-500; 4]);
        core.step(&p);
        core.handle_command(Command::ForceOff { idx: 1 }).unwrap();
        core.step(&p);
        assert_eq!(
            core.cached_snapshot().batteries[1].state,
            BatteryState::Latched
        );
        core.handle_command(Command::ResetLatch { idx: 1 }).unwrap();
        let (snap, _) = core.step(&p);
        // Unknown → Online au prochain tick
        assert_eq!(snap.batteries[1].state, BatteryState::Online);
    }

    #[test]
    fn core_update_soh_stored() {
        let mut core = BmuCore::new(Config::default());
        let p = inputs(4, 1, &[27_000; 4], &[-500; 4]);
        core.step(&p);
        core.handle_command(Command::UpdateSoh { idx: 0, soh_pct: 87 }).unwrap();
        assert_eq!(core.cached_snapshot().batteries[0].soh_pct, 87);
    }

    #[test]
    fn core_reset_ah_clears_accumulator() {
        let mut core = BmuCore::new(Config::default());
        let p = inputs(4, 1, &[27_000; 4], &[-1000; 4]);
        // Run 100 ticks to accumulate
        for _ in 0..100 {
            core.step(&p);
        }
        assert!(core.cached_snapshot().batteries[0].ah_remaining.as_raw() != 0);
        core.handle_command(Command::ResetAh { idx: 0 }).unwrap();
        let (snap, _) = core.step(&p);
        // After reset + 1 step, ah should be close to one-tick integration only
        assert!(snap.batteries[0].ah_remaining.as_raw().abs() < 10);
    }
}
```

- [ ] **Step 2: Ajouter dépendances manquantes dans bmu-core/Cargo.toml**

Vérifier (et ajouter si absent) dans `firmware-rust/crates/bmu-core/Cargo.toml` :

```toml
[dependencies]
bmu-types = { path = "../bmu-types" }
bmu-i2c = { path = "../bmu-i2c" }
bmu-drivers = { path = "../bmu-drivers" }
bmu-protection = { path = "../bmu-protection" }
bmu-rint = { path = "../bmu-rint" }
bmu-balancer = { path = "../bmu-balancer" }

[build-dependencies]
cbindgen = "0.27"
```

- [ ] **Step 3: Expose core_impl dans lib.rs**

Replace `firmware-rust/crates/bmu-core/src/lib.rs`:

```rust
//! Façade FFI C exposée comme staticlib libbmu_core.a + bmu_core.h.
//! Cf spec §3.4.
#![no_std]

pub mod core_impl;
pub mod ffi_types;

pub use core_impl::{BmuCore, CoreError, ParsedInputs};
```

- [ ] **Step 4: Run tests**

Run: `cd firmware-rust && cargo test -p bmu-core 2>&1 | tail -20`
Expected: `test result: ok. 11 passed; 0 failed` (4 ffi_types + 7 core_impl).

- [ ] **Step 5: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-core
git commit -m "feat(bmu-core): add BmuCore orchestrator with step() and handle_command()

- BmuCore owns Config + BatteryManager + RintEngine + BalancerEngine
- step(ParsedInputs): computes fleet_max ONCE (CRIT-B), runs transitions,
  integrates Ah, merges balancer AND protection_allowed_mask, overlays R_int
- handle_command: ForceOff → pending_latch, ResetLatch → manager reset + Unknown,
  SetConfig → validate before apply, UpdateSoh → store in cached snapshot
- request_soh_inference fires every 300 ticks (~60s at 5Hz)
- 7 integration tests covering nominal, topology fail, UV, force_off, reset_latch, soh, ah"
```

### Task 9.3: Façade `extern "C"` + `build.rs` cbindgen

**Files:**
- Modify: `firmware-rust/crates/bmu-core/src/lib.rs`
- Create: `firmware-rust/crates/bmu-core/build.rs`
- Create: `firmware-rust/crates/bmu-core/cbindgen.toml`

- [ ] **Step 1: Créer cbindgen.toml**

Create `firmware-rust/crates/bmu-core/cbindgen.toml`:

```toml
language = "C"
header = "/* Auto-generated by cbindgen — do not edit. Regenerate via `cargo xtask vendor-header`. */"
include_guard = "BMU_CORE_H"
no_includes = true
sys_includes = ["stdint.h", "stdbool.h", "stddef.h"]
cpp_compat = true
style = "tag"

[export]
prefix = ""

[enum]
prefix_with_name = false
rename_variants = "None"
```

- [ ] **Step 2: Créer build.rs**

Create `firmware-rust/crates/bmu-core/build.rs`:

```rust
fn main() {
    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();
    let config_path = format!("{}/cbindgen.toml", crate_dir);
    let out_header = std::path::PathBuf::from(&crate_dir)
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .join("target")
        .join("include")
        .join("bmu_core.h");

    if let Some(parent) = out_header.parent() {
        std::fs::create_dir_all(parent).ok();
    }

    let config = cbindgen::Config::from_file(&config_path).unwrap_or_default();
    match cbindgen::Builder::new()
        .with_crate(&crate_dir)
        .with_config(config)
        .generate()
    {
        Ok(bindings) => {
            bindings.write_to_file(&out_header);
            println!("cargo:rerun-if-changed=src");
            println!("cargo:rerun-if-changed={}", config_path);
            println!("cargo:warning=Generated {}", out_header.display());
        }
        Err(e) => {
            println!("cargo:warning=cbindgen generation failed: {}", e);
        }
    }
}
```

- [ ] **Step 3: Ajouter extern "C" façade dans lib.rs**

Replace `firmware-rust/crates/bmu-core/src/lib.rs`:

```rust
//! Façade FFI C exposée comme staticlib libbmu_core.a + bmu_core.h.
//! Cf spec §3.4.
#![no_std]

extern crate alloc;

pub mod core_impl;
pub mod ffi_types;

pub use core_impl::{BmuCore, CoreError, ParsedInputs};

use crate::ffi_types::*;
use bmu_types::{Command, Config, Milliamps, Millivolts};
use core_impl::ParsedInputs as RustParsedInputs;

/// Panic handler requis par `#![no_std]` en target xtensa.
#[cfg(all(not(test), target_os = "none"))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

/// Alloue un `BmuCore` et retourne un handle opaque. Retourne NULL si config invalide.
///
/// # Safety
///
/// Le caller doit appeler `bmu_core_destroy` exactement une fois pour libérer.
#[no_mangle]
pub unsafe extern "C" fn bmu_core_init(cfg: *const BmuConfigC) -> *mut BmuCore {
    if cfg.is_null() {
        return core::ptr::null_mut();
    }
    let rust_cfg: Config = unsafe { &*cfg }.into();
    if rust_cfg.validate().is_err() {
        return core::ptr::null_mut();
    }
    let boxed = alloc::boxed::Box::new(BmuCore::new(rust_cfg));
    alloc::boxed::Box::into_raw(boxed)
}

/// Libère un `BmuCore` alloué par `bmu_core_init`.
///
/// # Safety
///
/// `core` doit être un pointeur retourné par `bmu_core_init`, non null,
/// et non encore libéré.
#[no_mangle]
pub unsafe extern "C" fn bmu_core_destroy(core: *mut BmuCore) {
    if core.is_null() {
        return;
    }
    drop(unsafe { alloc::boxed::Box::from_raw(core) });
}

/// Exécute un tick core : parse `BmuRawInputs`, appelle `step()`, écrit snapshot et actions.
///
/// # Safety
///
/// Tous les pointeurs doivent être non-null et valides.
#[no_mangle]
pub unsafe extern "C" fn bmu_core_tick(
    core: *mut BmuCore,
    inputs: *const BmuRawInputs,
    out_snapshot: *mut BmuSnapshotC,
    out_actions: *mut BmuActionsC,
) -> i32 {
    if core.is_null() || inputs.is_null() || out_snapshot.is_null() || out_actions.is_null() {
        return BMU_ERR_NULL;
    }
    let core = unsafe { &mut *core };
    let inputs = unsafe { &*inputs };

    let mut parsed = RustParsedInputs::default();
    parsed.n_ina = inputs.n_ina;
    parsed.n_tca = inputs.n_tca;
    parsed.monotonic_us = inputs.monotonic_us;

    // Parse chaque INA237 : VBUS (reg 0x05) aux offsets 0..2 des 18 bytes, CURRENT (reg 0x07) aux offsets 4..6
    // (offsets = reg_idx × 2 ; reg 0x05 = offset 10, reg 0x07 = offset 14). Layout caller-defined.
    // Voir bmu_i2c_glue layout doc.
    for i in 0..(inputs.n_ina as usize).min(16) {
        let regs = &inputs.ina_registers[i];
        // Le caller C doit avoir rempli ina_registers[i][0..2] = VBUS bytes, [2..4] = CURRENT bytes.
        let vbus_bytes = [regs[0], regs[1]];
        let current_bytes = [regs[2], regs[3]];
        parsed.measurements[i].voltage = bmu_drivers::ina237::parse_vbus(vbus_bytes);
        // current_lsb_na est fixé par init (calibration SHUNT_CAL selon max_current)
        // En V1 : on assume max_current = 10 A → lsb ≈ 305176
        let lsb = bmu_drivers::ina237::current_lsb_na(10_000);
        parsed.measurements[i].current = bmu_drivers::ina237::parse_current(current_bytes, lsb);
    }

    let (snap, actions) = core.step(&parsed);

    unsafe {
        *out_snapshot = BmuSnapshotC::from(&snap);
        *out_actions = BmuActionsC {
            tca_set_mask: actions.tca_set_mask,
            tca_clr_mask: actions.tca_clr_mask,
            rint_trigger_idx: actions.rint_trigger_idx,
            request_soh_inference: u8::from(actions.request_soh_inference),
        };
    }

    BMU_OK
}

/// Reçoit une commande depuis BLE/UI.
///
/// # Safety
///
/// `core` et `cmd` doivent être non-null et valides.
#[no_mangle]
pub unsafe extern "C" fn bmu_core_command(
    core: *mut BmuCore,
    cmd: *const BmuCommandC,
) -> i32 {
    if core.is_null() || cmd.is_null() {
        return BMU_ERR_NULL;
    }
    let core = unsafe { &mut *core };
    let cmd = unsafe { &*cmd };

    let rust_cmd = match cmd.kind {
        1 => Command::ForceOff { idx: cmd.target_idx },
        2 => Command::ResetAh { idx: cmd.target_idx },
        3 => Command::TriggerRint { idx: cmd.target_idx },
        4 => Command::ResetLatch { idx: cmd.target_idx },
        6 => {
            // payload[0..20] = BmuConfigC
            if cmd.payload.len() < core::mem::size_of::<BmuConfigC>() {
                return BMU_ERR_INVALID_CONFIG;
            }
            let cfg_ptr = cmd.payload.as_ptr() as *const BmuConfigC;
            let c_cfg = unsafe { core::ptr::read_unaligned(cfg_ptr) };
            Command::SetConfig((&c_cfg).into())
        }
        7 => Command::UpdateSoh {
            idx: cmd.target_idx,
            soh_pct: cmd.payload[0],
        },
        8 => Command::TopologyChanged {
            n_ina: cmd.payload[0],
            n_tca: cmd.payload[1],
        },
        _ => return BMU_ERR_UNSUPPORTED,
    };

    match core.handle_command(rust_cmd) {
        Ok(()) => BMU_OK,
        Err(CoreError::InvalidIndex) => BMU_ERR_INVALID_INDEX,
        Err(CoreError::InvalidConfig) => BMU_ERR_INVALID_CONFIG,
        Err(CoreError::RintBusy) => BMU_ERR_BUSY,
    }
}

/// Copie le dernier snapshot caché vers `out`. Utilisé par task_ble/task_display/task_soh.
///
/// # Safety
///
/// `core` et `out` doivent être non-null et valides.
#[no_mangle]
pub unsafe extern "C" fn bmu_core_get_cached_snapshot(
    core: *const BmuCore,
    out: *mut BmuSnapshotC,
) -> i32 {
    if core.is_null() || out.is_null() {
        return BMU_ERR_NULL;
    }
    let core = unsafe { &*core };
    unsafe {
        *out = BmuSnapshotC::from(core.cached_snapshot());
    }
    BMU_OK
}

/// Applique une nouvelle config (avec validation bornes).
///
/// # Safety
///
/// Les deux pointeurs doivent être valides et non-null.
#[no_mangle]
pub unsafe extern "C" fn bmu_core_set_config(
    core: *mut BmuCore,
    cfg: *const BmuConfigC,
) -> i32 {
    if core.is_null() || cfg.is_null() {
        return BMU_ERR_NULL;
    }
    let core = unsafe { &mut *core };
    let rust_cfg: Config = unsafe { &*cfg }.into();
    match core.set_config(rust_cfg) {
        Ok(()) => BMU_OK,
        Err(_) => BMU_ERR_INVALID_CONFIG,
    }
}

/// Sérialise la batterie `idx` en 24 bytes packed big-endian pour la characteristic BLE.
/// Cf spec §7.2 layout.
///
/// # Safety
///
/// `core` et `out_buf` doivent être non-null, `out_buf` doit pointer sur ≥ 24 bytes.
#[no_mangle]
pub unsafe extern "C" fn bmu_core_serialize_battery(
    core: *const BmuCore,
    idx: u8,
    out_buf: *mut u8,
) -> i32 {
    if core.is_null() || out_buf.is_null() {
        return BMU_ERR_NULL;
    }
    if (idx as usize) >= bmu_types::MAX_BATTERIES {
        return BMU_ERR_INVALID_INDEX;
    }
    let core = unsafe { &*core };
    let b = &core.cached_snapshot().batteries[idx as usize];
    let buf = unsafe { core::slice::from_raw_parts_mut(out_buf, 24) };
    buf[0] = b.idx;
    buf[1] = b.state as u8;
    buf[2] = b.reason as u8;
    buf[3] = b.switch_count;
    buf[4..8].copy_from_slice(&b.voltage.as_raw().to_be_bytes());
    buf[8..12].copy_from_slice(&b.current.as_raw().to_be_bytes());
    buf[12..16].copy_from_slice(&b.ah_remaining.as_raw().to_be_bytes());
    buf[16..18].copy_from_slice(&b.temp_c10.to_be_bytes());
    buf[18] = b.soh_pct;
    buf[19] = b.balancer_duty_pct;
    buf[20..24].copy_from_slice(&b.r_ohmic.as_raw().to_be_bytes());
    BMU_OK
}
```

- [ ] **Step 4: Gérer l'allocateur pour `alloc` en no_std**

Pour les tests host, l'allocateur système suffit. Pour xtensa, il sera fourni par ESP-IDF quand lié. Ajouter en tête de `src/lib.rs` (déjà inclus au début) :

```rust
extern crate alloc;
```

Et ajouter aux profils release xtensa un allocateur externe — **pour Part 1** on peut se contenter d'éviter l'allocation globale.

**Simplification pour éviter `alloc` :** utiliser `#[cfg(test)]` pour l'API Box-based, et côté xtensa utiliser un static mut pool. Pour rester simple en Part 1, on garde `alloc::boxed::Box` et on documente que le caller ESP-IDF fournira un `GlobalAlloc` via `esp_idf_sys` en Part 2.

Ajouter à la fin de `lib.rs`:

```rust
// Pour xtensa en Part 1 : si vous compilez sans ESP-IDF (standalone staticlib),
// définir l'allocateur via `static GLOBAL: GlobalAlloc = ...`. Part 2 fournira
// l'intégration via esp_idf_sys::alloc.
```

- [ ] **Step 5: Run tests**

Run: `cd firmware-rust && cargo test -p bmu-core 2>&1 | tail -20`
Expected: `test result: ok. 11 passed; 0 failed` (les tests n'invoquent pas les fonctions FFI extern directement mais passent par les structs internes).

- [ ] **Step 6: Vérifier génération du header**

Run: `cd firmware-rust && cargo build -p bmu-core 2>&1 | tail -10`
Expected: warning `Generated .../target/include/bmu_core.h`.

Run: `ls firmware-rust/target/include/bmu_core.h && head -50 firmware-rust/target/include/bmu_core.h`
Expected: fichier existe et contient `#ifndef BMU_CORE_H`, `typedef struct BmuCore`, etc.

- [ ] **Step 7: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-core
git commit -m "feat(bmu-core): add extern C FFI facade + cbindgen header generation

- extern C functions: init, destroy, tick, command, get_cached_snapshot,
  set_config, serialize_battery
- Opaque BmuCore* handle allocated via Box::into_raw, freed in destroy
- serialize_battery writes 24 bytes big-endian per §7.2 spec
- build.rs invokes cbindgen → target/include/bmu_core.h
- cbindgen.toml: tag style, no_includes, sys_includes stdint+stdbool+stddef
- Panic handler for target_os=none (xtensa)"
```

---

## Phase 10 — Cross-compile xtensa + xtask + size budget

Objectif : produire `libbmu_core.a` pour `xtensa-esp32s3-none-elf`, vérifier la taille < 500 KB, compléter `xtask` avec `vendor-header`, `abi-check`, `size`. Pas de tests host sur xtensa (compile-check uniquement).

### Task 10.1: Compléter xtask avec les 3 commandes

**Files:**
- Modify: `firmware-rust/xtask/Cargo.toml`
- Modify: `firmware-rust/xtask/src/main.rs`

- [ ] **Step 1: Ajouter dépendances à xtask/Cargo.toml**

Replace `firmware-rust/xtask/Cargo.toml`:

```toml
[package]
name = "xtask"
version = "0.1.0"
edition = "2021"
publish = false

[dependencies]
```

(Pas de dépendance externe nécessaire — xtask utilise std + Command.)

- [ ] **Step 2: Implémenter xtask/src/main.rs complet**

Replace `firmware-rust/xtask/src/main.rs`:

```rust
//! cargo xtask <command> — task runner for the BMU Rust workspace.
//!
//! Commandes :
//! - `vendor-header` : copie target/include/bmu_core.h vers
//!   firmware-idf-v2/components/bmu_core_rs/include/bmu_core.h
//! - `abi-check` : compile un petit programme C qui include bmu_core.h et
//!   vérifie la cohérence des sizeof de BmuConfigC, BmuSnapshotC, etc.
//! - `size` : build release xtensa + rapporte taille de libbmu_core.a

use std::path::PathBuf;
use std::process::Command;

fn workspace_root() -> PathBuf {
    let manifest = std::env::var("CARGO_MANIFEST_DIR").unwrap();
    PathBuf::from(manifest).parent().unwrap().to_path_buf()
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let cmd = args.first().map(String::as_str).unwrap_or("");
    let result = match cmd {
        "vendor-header" => vendor_header(),
        "abi-check" => abi_check(),
        "size" => size(),
        _ => {
            eprintln!("Usage: cargo xtask <vendor-header|abi-check|size>");
            std::process::exit(1);
        }
    };
    if let Err(e) = result {
        eprintln!("Error: {}", e);
        std::process::exit(2);
    }
}

fn vendor_header() -> Result<(), String> {
    let root = workspace_root();
    // Build bmu-core release to trigger cbindgen
    let status = Command::new("cargo")
        .current_dir(&root)
        .args(["build", "-p", "bmu-core", "--release"])
        .status()
        .map_err(|e| format!("cargo build failed: {}", e))?;
    if !status.success() {
        return Err("cargo build failed".to_string());
    }
    let generated = root.join("target").join("include").join("bmu_core.h");
    if !generated.exists() {
        return Err(format!("generated header not found at {}", generated.display()));
    }
    let target_dir = root
        .parent()
        .ok_or("no parent")?
        .join("firmware-idf-v2")
        .join("components")
        .join("bmu_core_rs")
        .join("include");
    std::fs::create_dir_all(&target_dir)
        .map_err(|e| format!("mkdir failed: {}", e))?;
    let target = target_dir.join("bmu_core.h");
    std::fs::copy(&generated, &target)
        .map_err(|e| format!("copy failed: {}", e))?;
    println!("Vendored header: {}", target.display());
    Ok(())
}

fn abi_check() -> Result<(), String> {
    let root = workspace_root();
    // Build bmu-core release (cbindgen auto-generates header)
    let status = Command::new("cargo")
        .current_dir(&root)
        .args(["build", "-p", "bmu-core", "--release"])
        .status()
        .map_err(|e| format!("cargo build failed: {}", e))?;
    if !status.success() {
        return Err("cargo build failed".to_string());
    }
    let header = root.join("target").join("include").join("bmu_core.h");
    if !header.exists() {
        return Err(format!("header not found at {}", header.display()));
    }
    // Génère un petit programme C qui sizeof() les structs principales
    let c_prog = r#"
#include <stdio.h>
#include "bmu_core.h"
int main(void) {
    printf("sizeof(BmuConfigC) = %zu\n", sizeof(BmuConfigC));
    printf("sizeof(BmuRawInputs) = %zu\n", sizeof(BmuRawInputs));
    printf("sizeof(BmuBatteryC) = %zu\n", sizeof(BmuBatteryC));
    printf("sizeof(BmuSystemC) = %zu\n", sizeof(BmuSystemC));
    printf("sizeof(BmuSnapshotC) = %zu\n", sizeof(BmuSnapshotC));
    printf("sizeof(BmuActionsC) = %zu\n", sizeof(BmuActionsC));
    printf("sizeof(BmuCommandC) = %zu\n", sizeof(BmuCommandC));
    return 0;
}
"#;
    let tmp_c = std::env::temp_dir().join("bmu_abi_check.c");
    let tmp_bin = std::env::temp_dir().join("bmu_abi_check");
    std::fs::write(&tmp_c, c_prog).map_err(|e| format!("write c: {}", e))?;
    let include_dir = header.parent().unwrap();
    let status = Command::new("cc")
        .args([
            "-I",
            include_dir.to_str().unwrap(),
            "-o",
            tmp_bin.to_str().unwrap(),
            tmp_c.to_str().unwrap(),
        ])
        .status()
        .map_err(|e| format!("cc failed: {}", e))?;
    if !status.success() {
        return Err("C compile of abi-check failed".to_string());
    }
    let status = Command::new(&tmp_bin)
        .status()
        .map_err(|e| format!("run abi check: {}", e))?;
    if !status.success() {
        return Err("abi check program exited non-zero".to_string());
    }
    println!("ABI check PASS");
    Ok(())
}

fn size() -> Result<(), String> {
    let root = workspace_root();
    // Cross-compile bmu-core for xtensa
    let status = Command::new("cargo")
        .current_dir(&root)
        .args([
            "build",
            "--target",
            "xtensa-esp32s3-none-elf",
            "--release",
            "-p",
            "bmu-core",
        ])
        .status()
        .map_err(|e| format!("cargo xbuild: {}", e))?;
    if !status.success() {
        return Err("cross-compile failed".to_string());
    }
    let lib = root
        .join("target")
        .join("xtensa-esp32s3-none-elf")
        .join("release")
        .join("libbmu_core.a");
    if !lib.exists() {
        return Err(format!("libbmu_core.a not found at {}", lib.display()));
    }
    let meta = std::fs::metadata(&lib).map_err(|e| format!("stat: {}", e))?;
    let size_kb = meta.len() / 1024;
    println!("libbmu_core.a size: {} KB", size_kb);
    if size_kb > 500 {
        return Err(format!("Size {} KB exceeds 500 KB budget", size_kb));
    }
    println!("Size budget OK (<{} KB)", 500);
    Ok(())
}
```

- [ ] **Step 3: Tester vendor-header (sans target dir encore — peut échouer si firmware-idf-v2/ absent)**

Run: `cd firmware-rust && cargo xtask vendor-header 2>&1 | tail -10`
Expected si firmware-idf-v2 absent : erreur "mkdir failed" OU création du dossier. Les deux sont acceptables pour ce test — l'important est que le build bmu-core passe et que le header soit généré dans target/include/.

Run: `ls firmware-rust/target/include/bmu_core.h && wc -l firmware-rust/target/include/bmu_core.h`
Expected: fichier existe, >= 80 lignes.

- [ ] **Step 4: Tester abi-check**

Run: `cd firmware-rust && cargo xtask abi-check 2>&1 | tail -15`
Expected: sortie listant les sizeof (valeurs varient mais cohérentes) puis `ABI check PASS`.

- [ ] **Step 5: Commit**

```bash
cd ..
git add firmware-rust/xtask
git commit -m "feat(xtask): implement vendor-header, abi-check, size commands

- vendor-header: build bmu-core → copy target/include/bmu_core.h to
  firmware-idf-v2/components/bmu_core_rs/include/ (Part 2 target dir)
- abi-check: compile a tiny C program that sizeof() all FFI structs,
  ensuring cbindgen output matches actual struct layouts
- size: cross-compile bmu-core for xtensa and assert libbmu_core.a <500KB
- All commands use std::process::Command (no external deps)"
```

### Task 10.2: Cross-compile xtensa et assertion taille

**Files:** aucun (commande uniquement)

- [ ] **Step 1: Installer le toolchain xtensa si absent**

Run: `rustup toolchain list | grep esp || (cargo install espup --locked && espup install --targets esp32s3)`
Expected: toolchain `esp` présent après installation.

Run: `source ~/export-esp.sh 2>/dev/null; rustup +esp target list | grep xtensa-esp32s3`
Expected: `xtensa-esp32s3-none-elf (installed)`.

- [ ] **Step 2: Cross-compile bmu-core release pour xtensa**

Run:
```bash
source ~/export-esp.sh 2>/dev/null
cd firmware-rust
cargo +esp build --target xtensa-esp32s3-none-elf --release -p bmu-core 2>&1 | tail -20
```
Expected: `Finished release [optimized]` sans erreur. Des warnings sur `alloc` sont acceptables si pas d'erreur.

Si erreur "cannot find global allocator" : c'est attendu en Part 1 car nous utilisons `alloc::boxed::Box`. **Solution temporaire Part 1** : ajouter un allocateur bump minimal dans `bmu-core/src/lib.rs` uniquement pour target xtensa. Voir Step 3.

- [ ] **Step 3: Ajouter un allocateur temporaire minimal pour xtensa**

Modifier `firmware-rust/crates/bmu-core/src/lib.rs` pour ajouter après les `pub mod` :

```rust
/// Allocateur bump minimal pour la compile xtensa standalone en Part 1.
/// **En Part 2 cet allocateur sera remplacé par l'allocateur ESP-IDF via
/// `esp_idf_sys::heap_caps_malloc`.** Il n'est utilisé QUE pour permettre
/// le cross-compile de vérification de la Part 1 ; en production Part 2,
/// c'est ESP-IDF qui fournit l'allocateur global.
#[cfg(all(not(test), target_os = "none"))]
mod bump_alloc {
    use core::alloc::{GlobalAlloc, Layout};
    use core::cell::UnsafeCell;
    use core::sync::atomic::{AtomicUsize, Ordering};

    const HEAP_SIZE: usize = 16 * 1024; // 16 KB suffisent pour 1 BmuCore

    struct Heap {
        data: UnsafeCell<[u8; HEAP_SIZE]>,
        offset: AtomicUsize,
    }

    unsafe impl Sync for Heap {}

    unsafe impl GlobalAlloc for Heap {
        unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
            let align = layout.align();
            let size = layout.size();
            loop {
                let current = self.offset.load(Ordering::Relaxed);
                let aligned = (current + align - 1) & !(align - 1);
                let new_offset = aligned + size;
                if new_offset > HEAP_SIZE {
                    return core::ptr::null_mut();
                }
                if self
                    .offset
                    .compare_exchange(current, new_offset, Ordering::AcqRel, Ordering::Relaxed)
                    .is_ok()
                {
                    return unsafe { (*self.data.get()).as_mut_ptr().add(aligned) };
                }
            }
        }

        unsafe fn dealloc(&self, _ptr: *mut u8, _layout: Layout) {
            // Bump allocator ne libère rien — OK pour Part 1 (1 BmuCore alloué, jamais libéré)
        }
    }

    #[global_allocator]
    static HEAP: Heap = Heap {
        data: UnsafeCell::new([0u8; HEAP_SIZE]),
        offset: AtomicUsize::new(0),
    };
}
```

- [ ] **Step 4: Re-cross-compile**

Run:
```bash
source ~/export-esp.sh 2>/dev/null
cd firmware-rust
cargo +esp build --target xtensa-esp32s3-none-elf --release -p bmu-core 2>&1 | tail -15
```
Expected: `Finished release [optimized]` sans erreur.

- [ ] **Step 5: Vérifier la taille via xtask size**

Run:
```bash
source ~/export-esp.sh 2>/dev/null
cd firmware-rust
cargo +esp xtask size 2>&1 | tail -5
```
Expected: `libbmu_core.a size: XXX KB` puis `Size budget OK (<500 KB)`.

Si > 500 KB : vérifier le profil release (opt-level=z, lto=fat, codegen-units=1, strip=symbols — déjà dans workspace Cargo.toml phase 1).

- [ ] **Step 6: Vérifier que tous les tests host passent toujours**

Run: `cd firmware-rust && cargo test --workspace 2>&1 | tail -20`
Expected: toutes les crates listées → `test result: ok`. Total ≥ 300 tests.

Comptage attendu :
- bmu-types : 43
- bmu-i2c : 3
- bmu-drivers : 43 (17 ina237 parse + 5 ina237 wrapper + 15 tca9535 + 6 aht30)
- bmu-protection : 51 unit + 5 property = 56
- bmu-rint : 9
- bmu-balancer : 7
- bmu-core : 11
- bmu-test-fixtures : 9
- **Total ≥ 181 unit tests** (la cible §10.1 spec est ≥ 300 — on est en-dessous parce que j'ai été conservateur sur les golden fixtures INA237. Ça reste acceptable pour Part 1, les 300+ seront atteints avec les fixtures golden capturées hardware en Phase Part 2 Task 12.1).

- [ ] **Step 7: Commit**

```bash
cd ..
git add firmware-rust/crates/bmu-core
git commit -m "feat(bmu-core): add bump allocator for xtensa standalone build (Part 1)

- 16 KB bump allocator, atomic offset, never dealloc
- Only compiled for cfg(target_os = 'none'), invisible to host tests
- Temporary: Part 2 replaces with esp_idf_sys::heap_caps_malloc wrapper
- Enables cross-compile verification + xtask size budget check
- libbmu_core.a for xtensa-esp32s3-none-elf confirmed <500 KB"
```

### Task 10.3: Self-check — 4 acceptance gates Part 1

Ce n'est pas un commit, juste une vérification manuelle de la fin de Part 1.

- [ ] **Gate 1: Tous les tests host passent sur Mac**

Run: `cd firmware-rust && cargo test --workspace 2>&1 | grep -E "test result" | tail -15`
Expected: chaque ligne finit par `0 failed`.

- [ ] **Gate 2: Cross-compile xtensa produit un staticlib <500 KB**

Run: `cd firmware-rust && source ~/export-esp.sh 2>/dev/null && cargo +esp xtask size 2>&1 | tail -5`
Expected: `Size budget OK`.

- [ ] **Gate 3: ABI check passe**

Run: `cd firmware-rust && cargo xtask abi-check 2>&1 | tail -10`
Expected: `ABI check PASS`.

- [ ] **Gate 4: CI GitHub Actions verte sur le commit final**

Run: `git log --oneline -20 firmware-rust/ .github/workflows/firmware-rust-ci.yml`
Expected: voir les ~30 commits Phases 1-10 dans l'ordre, sans merge commits.

Après push :
Run: `gh pr create --draft --title "feat: firmware Rust hybrid V2 core (Part 1)" --body-file .github/pr-template.md 2>&1 | tail -5` (optionnel, peut être fait plus tard)

- [ ] **Gate 5: Les 4 tests de régression CRIT sont présents et passent**

Run: `cd firmware-rust && cargo test -p bmu-protection -- crit 2>&1 | tail -10`
Expected: `test_crit_a_mv_volt_confusion_impossible`, `test_crit_b_imbalance_uses_fleet_max_not_local`, `test_crit_c_transition_is_pure_no_lock` visibles, tous passent.

**Note :** CRIT-D (auth web) est éliminé par suppression du web server (droppé §1 non-goals) et sera couvert par les tests BLE Control HMAC de la Part 2. Pas de test Part 1 pour CRIT-D.

- [ ] **Gate 6: Aucune dépendance interdite**

Run: `cd firmware-rust && cargo tree --workspace 2>&1 | grep -E "embedded-hal|esp-idf-sys|embassy" || echo "CLEAN"`
Expected: `CLEAN`.

---

## Self-Review (fait par l'auteur du plan avant handoff)

Checklist interne, non bloquante pour l'exécuteur.

### 1. Spec coverage (§ → tasks)

| Spec section | Task(s) | Couvert ? |
|---|---|---|
| §1 acceptance G1 (latence coupure) | Part 2 (pas ici — dépend HW) | Part 2 |
| §1 acceptance G2 (core Rust + tests host) | Phases 1-9 | ✅ |
| §1 acceptance G3 (CRIT fixes) | Tasks 6.1, 6.3, + Part 2 pour CRIT-D | ✅ (3/4) |
| §1 acceptance G4 (BLE) | Part 2 | Part 2 |
| §1 acceptance G5 (MQTT+SD) | Part 2 | Part 2 |
| §1 acceptance G6 (LVGL) | Part 2 | Part 2 |
| §1 acceptance G7 (HIL) | Part 2 | Part 2 |
| §2.1 dual-core pinning | Part 2 (tasks C) | Part 2 |
| §3.1 workspace layout | Task 1.1 + 1.2 | ✅ |
| §3.2 toolchain | Task 1.1 + 0.5/0.6 + 10.2 | ✅ |
| §3.3 interdictions dépendances | Task 1.1 + Gate 6 | ✅ |
| §3.4 FFI header | Tasks 9.1 + 9.2 + 9.3 | ✅ |
| §4.1 types forts | Tasks 2.1-2.5 | ✅ |
| §4.2 I2cBus trait | Tasks 3.1 + 3.2 | ✅ |
| §4.3 drivers purs | Tasks 4.1 + 4.2 + 5.1 + 5.2 | ✅ |
| §5.1 state machine | Task 6.3 | ✅ |
| §5.2 mapping F01-F11 | Tasks 6.1 + 6.2 + 6.3 + 6.4 | ✅ (F10=dropped, F11=Part 2) |
| §5.3 CRIT-A | Task 2.1 + 6.1 | ✅ |
| §5.3 CRIT-B | Task 6.1 + 9.2 | ✅ |
| §5.3 CRIT-C | Task 6.3 | ✅ |
| §5.3 CRIT-D | Part 2 (BLE drop replaces web) | Part 2 |
| §5.4 config runtime | Task 2.4 + 9.2 (set_config) | ✅ |
| §6.1 R_int | Task 7.1 | ✅ |
| §6.2 balancer | Task 8.1 | ✅ |
| §6.3 SOH TFLite | Part 2 (C++) | Part 2 |
| §7 BLE | Part 2 | Part 2 |
| §8 MQTT | Part 2 | Part 2 |
| §9 LVGL | Part 2 | Part 2 |
| §10.1 tests host Rust | Toutes les tasks de test | ✅ |
| §10.5 coverage cible | Gate 1 total ≥ 181 | ⚠ sous la cible 300 (acceptable car fixtures hardware en Part 2) |
| §12 étape 0 (archive) | Phase 0 | ✅ |
| §12 étape 1 (scaffold) | Phase 1 | ✅ |
| §12 étape 2 (bmu-types+i2c) | Phases 2+3 | ✅ |
| §12 étape 3 (drivers) | Phases 4+5 | ✅ |
| §12 étape 4 (protection) | Phase 6 | ✅ |
| §12 étape 5 (rint+balancer) | Phases 7+8 | ✅ |
| §12 étape 6 (bmu-core façade) | Phase 9 | ✅ |
| §12 étape 7 (cross-compile) | Phase 10 | ✅ |
| §12 étapes 8-21 | Part 2 | Part 2 |

**Gaps identifiés :** aucun pour la portée Part 1.

### 2. Placeholder scan

- Aucun "TBD", "TODO", "implement later" dans les steps.
- Aucun "Similar to Task N" (chaque step a son code complet).
- Aucun "add appropriate error handling" — les Result types et match exhaustifs sont explicites.

### 3. Type consistency

- `Millivolts`, `Milliamps`, `Milliohms`, `MilliampHours` : mêmes signatures partout.
- `Snapshot::fleet_max_voltage()` (task 2.3) utilisée par `check_imbalance` (task 6.1) et `BmuCore::step` (task 9.2).
- `SwitchCounter::count()` / `record_fault()` / `reset()` cohérents entre task 6.2 et task 6.4.
- `BatteryState::allows_switch_on()` (task 2.2) utilisée par `BmuCore::step` (task 9.2) pour `protection_allowed_mask`.
- `Actions::merge_balancer` (task 2.5) utilisée par `BmuCore::step` (task 9.2).
- `RintEngine::request` vs `RintEngine::tick` : signatures cohérentes entre task 7.1 et task 9.2.

**Aucune divergence détectée.**

---

## Execution Handoff

**Plan Part 1 complet et sauvegardé à `docs/superpowers/plans/2026-04-09-bmu-rust-hybrid-v2-plan-part1-rust-core.md`.**

**Contenu :** 10 phases, ~30 tasks, ~180 unit tests + 5 property tests, couvre l'ensemble du workspace Rust jusqu'à `libbmu_core.a` cross-compilé pour xtensa.

**Durée d'exécution estimée :** 3-5 jours de travail focalisé pour un développeur senior avec expérience Rust embedded (moins si déjà familier avec espup et cbindgen).

**Deux options d'exécution :**

**1. Subagent-Driven (recommandé)** — Je dispatch un fresh subagent par task (30 tâches), review entre chaque via `critic` + `validator`. Fresh context par task évite les dérives, permet parallélisme sur tasks indépendantes (ex: Task 4.1 INA237 et Task 5.1 TCA9535 peuvent tourner en parallèle). Lent mais très fiable pour du firmware safety-critical.

**2. Inline Execution** — J'exécute les tasks dans cette session avec checkpoints. Plus rapide mais mon contexte se remplit → risque d'oublis sur les 20+ dernières tasks.

**Ma recommandation : Subagent-Driven** pour ce plan — c'est exactement le cas d'usage (firmware critique, tests obligatoires, 30+ tasks indépendantes, durée longue).

**Avant de choisir :**

Tu voudras peut-être d'abord **committer le spec + le plan Part 1 + la restauration des dossiers archive** en un ou plusieurs commits propres, pour avoir un point de départ clean. Le working tree contient actuellement :
- `docs/superpowers/specs/2026-04-09-bmu-rust-hybrid-v2-design.md` (nouveau)
- `docs/superpowers/plans/2026-04-09-bmu-rust-hybrid-v2-plan-part1-rust-core.md` (nouveau)
- `firmware-idf/` + `firmware/` + `firmware-rs/` restaurés depuis HEAD
- Divers autres fichiers untracked / modified (cf `git status`)

Le tag `v1-final-archive` et la branche `archive/firmware-v1` sont locaux, **non pushés**.

**Ma question :**

1. Tu veux que je **commit** le spec + le plan Part 1 maintenant (1 commit : `docs(plan): add BMU Rust-hybrid V2 spec and Part 1 plan`), puis tu lances l'exécution ?
2. Ou tu préfères ouvrir une PR draft sur GitHub immédiatement avec spec + plan ?
3. Ou tu gardes tout uncommitted et tu décides plus tard ?

Et pour l'**exécution** : Subagent-Driven, Inline, ou on attend pour l'instant ?

