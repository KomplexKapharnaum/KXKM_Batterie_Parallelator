# Plan Part 2 — BMU Rust Hybrid V2 — ESP-IDF Integration

> **Ce plan est la partie 2 sur 2.** Il hérite de Part 1 (`2026-04-09-bmu-rust-hybrid-v2-plan-part1-rust-core.md`)
> et prend le staticlib `libbmu_core.a` + header `bmu_core.h` déjà produits pour construire le firmware
> ESP-IDF complet dans un nouveau projet `firmware-idf-v2/`.
>
> **Scope de cette itération (Phases 11-14 uniquement)** : scaffold ESP-IDF → FFI bridge via corrosion →
> I²C glue réel (INA237 + TCA9535 + AHT30) → `task_bmu_core` scheduling avec WDT et bus recovery.
> Après exécution + validation hardware de ces 4 phases, les Phases 15-22 (climate/SOH/SD,
> Wi-Fi/MQTT/replay, LVGL, BLE read-only, BLE Control + HMAC, Wi-Fi prov, Victron emul, HIL +
> bench 72h + merge) seront planifiées dans un plan de continuation avec les enseignements du bench.
>
> **Branche** : `feat/rust-hybrid-v2` (continue Part 1, 31 commits à date).
> **Architecture** : cf spec `2026-04-09-bmu-rust-hybrid-v2-design.md`, sections 2, 3.4, 4.2, 11.3.

---

## Héritages critiques depuis Part 1

Part 1 a produit :

- ✅ `firmware-rust/` workspace Rust 8 crates, **189 tests host** (+ 5 proptest)
- ✅ `libbmu_core.a` cross-compilé `xtensa-esp32s3-none-elf` (text+data = **282 KB / 500 KB budget**)
- ✅ `bmu_core.h` auto-généré par cbindgen (209 lignes, self-contained avec `#define MAX_BATTERIES 16`)
- ✅ `cargo xtask vendor-header / abi-check / size` tooling
- ✅ 3 régressions CRIT validées par design (A mV/V, B fleet_max, C no-mutex) + proptest invariants
- ⚠️ **Bump allocator temporaire** 16 KiB dans `bmu-core/src/lib.rs` (cfg `target_os = "none"`) — Phase 12 le remplace par un allocateur `heap_caps_malloc` wrapper
- ⚠️ **`current_lsb_na(100_000)` hardcodé** dans `bmu_core_tick` INA237 parse — Phase 12 ne le corrige pas (reste hardcodé V2 Part 2 pour ce plan ; passera dans `BmuConfigC` en Part 2 bis)
- ⚠️ **CRIT-D non résolu** — Part 2 Phase 19 l'adresse (pairing SC + HMAC), hors scope de ce plan

---

## Execution Notes & Known Deviations (Part 2)

Les 12 deviations documentées en Part 1 restent pertinentes. Nouveaux éléments propres à Part 2 :

1. **Commit-par-phase** (pas par task) — spec §12 l'exige explicitement pour la partie ESP-IDF.
   Contrairement à Part 1 où chaque Task X.Y était un commit, les tasks de Part 2 regroupent leurs
   changements et le commit est fait à la fin de la phase avec un sujet `feat(phase-NN): <titre>`.

2. **Hardware requis à partir de Phase 13** (I²C glue réel). Phases 11 et 12 sont testables en simu
   (splash LVGL visible en flash, tick factice loggué en monitor série). Phase 13 exige un bench
   avec au moins 1 TCA9535 + 1 INA237 pour valider les lectures live. Phase 14 exige le bench complet
   pour le stability test 1h.

3. **`corrosion-rs` via `FetchContent_Declare`** au lieu d'un submodule ou d'une install système —
   c'est une dépendance réseau au premier `idf.py build`. Documenté dans §11.3 spec. Risk CI : si
   offline, le build échoue. Alternative : vendoriser `corrosion/` sous `firmware-idf-v2/cmake/corrosion/`
   via `git subtree` si CI hors-ligne requis.

4. **`heap_caps_malloc` FFI pattern** pour le `#[global_allocator]` — spec interdit `esp-idf-sys`
   et `esp-hal`, donc on déclare les symboles ESP-IDF en `extern "C"` directement. Pattern en Task 12.1.

5. **Priority du protection task pinné PRO_CPU** — v1 était non-pinné (`xTaskCreate` simple).
   Phase 14 pinne explicitement sur PRO_CPU (core 0) avec `xTaskCreatePinnedToCore`. Mesure attendue :
   latence p99 `bmu_core_tick` ≤ 50 ms sur 1h de bench.

6. **WDT timeout 3 s au lieu de 15 s** (v1 avait 15 s pour compenser LVGL+I²C sous charge).
   Le core Rust étant pur et la latence cible ≤ 50 ms, un WDT 3 s est largement suffisant et
   catch les pathologies bus. Override dans `sdkconfig.defaults` de `firmware-idf-v2/`.

7. **INA237 read path = raw register dump 18 bytes** (pas de parsing C). V1 parsait les floats
   côté C ; V2 lit `regs[0..18]` bruts et les pousse dans `BmuRawInputs.ina_registers[16][18]`.
   Le parsing est fait par `bmu_drivers::ina237::parse_vbus/parse_current` dans le Rust core.
   Cette réarchitecture est le cœur de Phase 13.

8. **`firmware-idf/` reste intact** comme référence archivée v1 — aucun fichier n'est modifié dans
   ce répertoire. Part 2 Phase 22 (dans le plan futur) renommera `firmware-idf-v2` → `firmware-idf`
   après validation HIL, dans un commit dédié post-merge.

---

## File Structure Overview — Part 2 delta

```
KXKM_Batterie_Parallelator/
├── firmware-rust/                  # Part 1 — inchangé sauf allocator.rs (Phase 12)
│   └── crates/bmu-core/src/lib.rs   # bump allocator remplacé par heap_caps wrapper
├── firmware-idf/                    # Archive v1 — jamais touché
├── firmware-idf-v2/                 # NOUVEAU — Part 2
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   ├── partitions.csv
│   ├── idf_component.yml            # IDF version pin ≥5.4 <5.5
│   ├── main/
│   │   ├── main.cpp                 # Init chain : NVS → heap → LVGL splash → bmu_core_init → task spawns
│   │   ├── task_bmu_core.cpp        # Phase 14 — pin PRO_CPU, 200 ms, WDT 3 s, retry + recovery
│   │   └── CMakeLists.txt
│   ├── components/
│   │   ├── bmu_core_rs/             # Phase 12 — corrosion wrapper + header vendoré
│   │   │   ├── CMakeLists.txt       # corrosion_import_crate(...)
│   │   │   └── include/bmu_core.h   # vendoré depuis firmware-rust/target/include/
│   │   └── bmu_i2c_glue/            # Phase 13 — glue C + lecture raw INA237/TCA9535/AHT30
│   │       ├── CMakeLists.txt
│   │       ├── include/bmu_i2c_glue.h
│   │       └── src/bmu_i2c_glue.c
│   └── cmake/                       # optionnel — si on vendorise corrosion
│       └── corrosion/               # git subtree si CI offline
```

Aucune modification hors `firmware-rust/crates/bmu-core/src/lib.rs` (Phase 12) et `firmware-idf-v2/` (nouveau).

---

## Phase Plan

| # | Phase | Tasks | Exit criterion | Risk | Commit subject |
|---|---|---|---|---|---|
| **11** | ESP-IDF scaffold v2 | 11.1 → 11.5 | `idf.py build` + flash passe, splash LVGL visible sur BOX-3 | bas | `feat(phase-11): scaffold firmware-idf-v2 project` |
| **12** | Rust/C bridge + heap_caps allocator | 12.1 → 12.6 | Boot OK + log `tick OK n_bat=0` à 1 Hz avec `BmuRawInputs` factice, flash < 60% OTA, bump allocator retiré | **élevé (FFI frontière)** | `feat(phase-12): integrate bmu_core_rs via corrosion` |
| **13** | `bmu_i2c_glue` + I²C réel | 13.1 → 13.6 | Snapshot Rust contient V/I réels live lus sur bus, côte-à-côte conforme archive v1 | **élevé (hardware)** | `feat(phase-13): implement bmu_i2c_glue raw reads` |
| **14** | `task_bmu_core` scheduling + WDT | 14.1 → 14.5 | Stability bench **1 h** sans WDT, p99 tick < 50 ms, retry + bus recovery opérationnels | **élevé (temps réel)** | `feat(phase-14): task_bmu_core pinned PRO_CPU + WDT` |

**Total tasks** : ~22. **Durée estimée** : 4-6 jours de travail focalisé avec hardware sur bench
(Phase 11-12 : 1-2 jours sans hardware, Phase 13-14 : 2-4 jours avec bench).

---

# Phase 11 — Scaffold `firmware-idf-v2/` projet ESP-IDF

**Objectif** : créer un projet ESP-IDF neuf, compilable et flashable, avec un `main.cpp` minimal
affichant un splash LVGL. **Pas de Rust encore**, juste la structure ESP-IDF validée.

**Durée estimée** : 1 jour. **Risk** : bas. **Acceptance** : `idf.py build flash monitor` affiche
splash LVGL sur BOX-3.

---

### Task 11.1 : Créer la structure `firmware-idf-v2/`

**Files:**
- Create: `firmware-idf-v2/CMakeLists.txt`
- Create: `firmware-idf-v2/main/CMakeLists.txt`
- Create: `firmware-idf-v2/main/main.cpp` (stub)
- Create: `firmware-idf-v2/idf_component.yml`

- [ ] **Step 1 : vérifier que `firmware-idf-v2/` n'existe pas**

```bash
test ! -d firmware-idf-v2 || (echo "DIR EXISTS — abort, check git status" && exit 1)
```

- [ ] **Step 2 : `idf.py create-project` dans un répertoire temporaire, puis rename**

`idf.py create-project` génère la structure conventionnelle. On l'utilise comme base et on patche
ensuite. Dans le repo root :

```bash
source ~/esp/esp-idf/export.sh
cd /tmp
idf.py create-project kxkm_bmu_v2
mv /tmp/kxkm_bmu_v2 /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator/firmware-idf-v2
cd -
```

- [ ] **Step 3 : Remplacer `firmware-idf-v2/CMakeLists.txt`**

Le fichier généré par `create-project` est trop minimaliste. On le remplace par une version
identique à celle de `firmware-idf/CMakeLists.txt` (archive v1) avec le nom de projet v2 :

```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(kxkm-bmu-v2)
```

- [ ] **Step 4 : Créer `firmware-idf-v2/idf_component.yml`**

Pin IDF version range :

```yaml
dependencies:
  idf: ">=5.4,<5.5"
```

- [ ] **Step 5 : Créer `firmware-idf-v2/main/CMakeLists.txt` minimaliste**

```cmake
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_timer
        esp_lvgl_port
        esp-box-3
)
```

Note : `esp-box-3` est le BSP, `esp_lvgl_port` est le port LVGL pour ESP-IDF. Ces dépendances
seront installées via `idf_component.yml` en Task 11.3.

- [ ] **Step 6 : Placeholder `main.cpp`** (remplacé en Task 11.4)

```cpp
#include <stdio.h>
#include "esp_log.h"

extern "C" void app_main(void) {
    ESP_LOGI("bmu-v2", "boot v2 scaffold step 1 (phase 11 task 11.1)");
}
```

- [ ] **Step 7 : Premier `idf.py build` de sanity**

```bash
source ~/esp/esp-idf/export.sh
cd firmware-idf-v2
idf.py set-target esp32s3
idf.py build 2>&1 | tail -20
```

Expected: `Project build complete` sans erreur. `esp_lvgl_port` et `esp-box-3` peuvent ne pas être
résolus à ce stade — dans ce cas Task 11.2 les ajoutera via `idf_component.yml`. Si le build échoue
sur ces composants, retirer temporairement de `REQUIRES` de `main/CMakeLists.txt` pour que ce Task
compile, et les réintroduire en Task 11.3.

---

### Task 11.2 : `partitions.csv` + `sdkconfig.defaults`

**Files:**
- Create: `firmware-idf-v2/partitions.csv`
- Create: `firmware-idf-v2/sdkconfig.defaults`

- [ ] **Step 1 : `partitions.csv`**

Hérité verbatim de l'archive v1 `firmware-idf/partitions.csv`. Layout 16 MB flash ESP32-S3 avec
dual OTA 2 MB + SPIFFS 1 MB + FAT 11 MB :

```csv
# Name,   Type, SubType, Offset,   Size,    Flags
nvs,      data, nvs,     0x9000,   0x6000,
otadata,  data, ota,     0xf000,   0x2000,
phy_init, data, phy,     0x11000,  0x1000,
ota_0,    app,  ota_0,   0x20000,  0x200000,
ota_1,    app,  ota_1,   0x220000, 0x200000,
storage,  data, spiffs,  0x420000, 0x100000,
fatfs,    data, fat,     0x520000, 0xAE0000,
```

- [ ] **Step 2 : `sdkconfig.defaults`** — v2 plus strict que v1

```
# === Partition layout ===
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"

# === Target ESP32-S3 ===
CONFIG_IDF_TARGET="esp32s3"
CONFIG_IDF_TARGET_ESP32S3=y

# === PSRAM (ESP32-S3-BOX-3 embed 8MB octal PSRAM) ===
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
# PSRAM NE sert PAS de heap général — réservé à LVGL double-buffer en Phase 17
CONFIG_SPIRAM_USE_MALLOC=n
CONFIG_SPIRAM_USE_CAPS_ALLOC=y

# === LVGL — mémoire statique 64 KiB (idem v1) ===
CONFIG_LV_MEM_SIZE_KILOBYTES=64

# === WDT — tightened vs v1 (15 s → 3 s) pour détecter pathologies I²C rapidement ===
CONFIG_ESP_TASK_WDT_TIMEOUT_S=3
CONFIG_ESP_TASK_WDT_INIT=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y

# === Heap — internal SRAM only pour Rust core (deterministic latency) ===
# `heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` consomme uniquement SRAM
CONFIG_HEAP_POISONING_COMPREHENSIVE=y
CONFIG_HEAP_USE_HOOKS=y

# === Logging — INFO niveau pour monitor série ===
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y

# === Pro/App CPU — FreeRTOS Unicore NON, on veut les 2 cores ===
CONFIG_FREERTOS_UNICORE=n
CONFIG_FREERTOS_HZ=1000
```

- [ ] **Step 3 : Re-build pour valider `sdkconfig.defaults`**

```bash
source ~/esp/esp-idf/export.sh
cd firmware-idf-v2
rm -rf build sdkconfig
idf.py build 2>&1 | tail -30
```

Expected: `Project build complete`. Vérifier avec `idf.py size` que :
- Flash app size ≤ 1.5 MB (sous le budget 2 MB OTA)
- PSRAM déclarée

---

### Task 11.3 : Installer `esp-box-3` BSP + `esp_lvgl_port`

**Files:**
- Modify: `firmware-idf-v2/main/idf_component.yml`

- [ ] **Step 1 : Créer `firmware-idf-v2/main/idf_component.yml`**

```yaml
dependencies:
  idf:
    version: ">=5.4,<5.5"
  esp-box-3: "^1.0.0"
  esp_lvgl_port: "^2.0.0"
  lvgl/lvgl: "^9.1.0"
```

- [ ] **Step 2 : `idf.py reconfigure` pour résoudre les deps**

```bash
source ~/esp/esp-idf/export.sh
cd firmware-idf-v2
idf.py reconfigure 2>&1 | tail -20
```

Expected : `dependencies.lock` créé, BSP + LVGL port + LVGL téléchargés dans `managed_components/`.

- [ ] **Step 3 : Rebuild**

```bash
idf.py build 2>&1 | tail -20
```

Expected : `Project build complete`, flash size visible via `idf.py size`.

---

### Task 11.4 : `main.cpp` boot + splash LVGL

**Files:**
- Modify: `firmware-idf-v2/main/main.cpp`

- [ ] **Step 1 : Implementer le boot minimal**

```cpp
// firmware-idf-v2/main/main.cpp
//
// Boot minimal Phase 11 : init NVS, init BSP BOX-3, init LVGL port, affiche
// un splash "BMU v2 — Rust Hybrid core" pendant 3 s.
//
// Aucun appel au core Rust à ce stade (Phase 12).

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/esp-bsp.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "bmu-v2";

static void init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS erase required");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");
}

static void init_display_and_splash(void) {
    // BSP init LCD + LVGL port
    lv_display_t *disp = bsp_display_start();
    if (disp == NULL) {
        ESP_LOGE(TAG, "bsp_display_start FAILED");
        return;
    }
    bsp_display_backlight_on();

    // Lock LVGL, build splash screen, unlock
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock FAILED");
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a0a), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "BMU v2");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00ff88), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "Rust Hybrid Core — phase 11");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xcccccc), LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 20);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "Splash displayed");
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== BMU v2 boot ===");
    ESP_LOGI(TAG, "FW version: %s", CONFIG_APP_PROJECT_VER);
    ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());

    init_nvs();
    init_display_and_splash();

    ESP_LOGI(TAG, "Boot complete — idle loop");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "heap free=%zu kB uptime=%llu s",
                 esp_get_free_heap_size() / 1024,
                 esp_timer_get_time() / 1000000ULL);
    }
}
```

- [ ] **Step 2 : Ajouter `app_project_ver` dans `main/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_timer
        esp_system
        nvs_flash
        esp_lvgl_port
        esp-box-3
)

# Version projet injectée comme macro
add_compile_definitions(CONFIG_APP_PROJECT_VER="2.0.0-phase11")
```

- [ ] **Step 3 : Build**

```bash
idf.py build 2>&1 | tail -10
```

- [ ] **Step 4 : Flash + monitor sur BOX-3**

```bash
idf.py -p /dev/cu.usbmodem* flash monitor
```

Expected monitor output :
```
I (XXX) bmu-v2: === BMU v2 boot ===
I (XXX) bmu-v2: FW version: 2.0.0-phase11
I (XXX) bmu-v2: ESP-IDF: v5.4.x
I (XXX) bmu-v2: NVS initialized
I (XXX) bmu-v2: Splash displayed
I (XXX) bmu-v2: Boot complete — idle loop
I (XXX) bmu-v2: heap free=XXX kB uptime=1 s
...
```

Et le splash "BMU v2 / Rust Hybrid Core — phase 11" visible sur l'écran BOX-3.

---

### Task 11.5 : Commit Phase 11

**Files:** aucun (commit uniquement).

- [ ] **Step 1 : `git status` review**

```bash
git status --short
```

Seuls des fichiers sous `firmware-idf-v2/` doivent apparaître (nouveau répertoire).
`firmware-idf/` ne doit PAS être modifié.

- [ ] **Step 2 : Ajouter `.gitignore` entries**

```bash
cat >> firmware-idf-v2/.gitignore << 'EOF'
build/
sdkconfig
sdkconfig.old
managed_components/
dependencies.lock
EOF
```

Note : on garde `sdkconfig.defaults` dans git mais pas `sdkconfig` (qui est généré).

- [ ] **Step 3 : Commit**

```bash
git add firmware-idf-v2
git commit -m "$(cat <<'EOF'
feat(phase-11): scaffold firmware-idf-v2 project

- New ESP-IDF v5.4 project from scratch
- Partition layout 16MB: 2x OTA 2MB + SPIFFS 1MB + FAT 11MB
- sdkconfig.defaults: PSRAM octal 80 MHz, WDT 3s, dual-core
- esp-box-3 BSP + esp_lvgl_port + LVGL 9.1 via idf_component.yml
- main.cpp: NVS init + LVGL splash "BMU v2 phase 11"
- No Rust integration yet (Phase 12)
EOF
)"
```

Subject `feat(phase-11): scaffold firmware-idf-v2 project` = 47 chars ≤ 50 ✓.

- [ ] **Step 4 : Verify**

```bash
git log --oneline -1
# Expected: feat(phase-11): scaffold firmware-idf-v2 project
```

---

# Phase 12 — Rust/C bridge via corrosion + `heap_caps` allocator

**Objectif** : intégrer `bmu_core_rs` (staticlib Part 1) dans `firmware-idf-v2/` via corrosion,
remplacer le bump allocator Part 1 par un wrapper `heap_caps_malloc` SRAM interne, et faire en sorte
que `main.cpp` appelle `bmu_core_init` et `bmu_core_tick` à 1 Hz avec des `BmuRawInputs` factices,
loggant le snapshot vide sur le monitor série.

**Durée estimée** : 1-2 jours. **Risk** : élevé (frontière FFI, linker errors possibles, allocateur
à surveiller). **Acceptance** : boot OK + log `tick OK n_bat=0` à 1 Hz, flash total < 60 % OTA
(< 1.2 MB), aucune fuite heap mesurée sur 5 min.

---

### Task 12.1 : Remplacer le bump allocator par `heap_caps` wrapper

**Files:**
- Modify: `firmware-rust/crates/bmu-core/src/lib.rs`

**Context** : Part 1 Task 10.2 a ajouté un bump allocator 16 KiB gated `cfg(target_os = "none")`
dans `bmu-core/src/lib.rs`. Il suffisait pour valider la cross-compile mais n'est pas production-ready
(pas de dealloc, heap statique limité, pas de partage avec ESP-IDF). Phase 12 le remplace par un
wrapper `GlobalAlloc` qui FFI-appelle `heap_caps_malloc` / `heap_caps_free` d'ESP-IDF.

Le spec (section 3) interdit `esp-idf-sys` et `esp-hal`, donc on déclare les symboles `heap_caps_*`
directement en `extern "C"`. Lors du link staticlib → ESP-IDF binary, le linker résout ces symboles
depuis le component `heap` d'ESP-IDF (toujours présent).

- [ ] **Step 1 : Retirer le module `bump_alloc` dans `bmu-core/src/lib.rs`**

Ouvrir `firmware-rust/crates/bmu-core/src/lib.rs` et supprimer tout le bloc `mod bump_alloc { ... }`
(ajouté en Part 1 Task 10.2). Le `#[cfg(all(not(test), target_os = "none"))]` sur le module et
son `#[global_allocator]` static `HEAP` doivent partir.

- [ ] **Step 2 : Créer `firmware-rust/crates/bmu-core/src/heap_caps_alloc.rs`**

```rust
//! `GlobalAlloc` wrapper qui FFI-appelle `heap_caps_malloc` / `heap_caps_free`
//! d'ESP-IDF.
//!
//! Utilisé uniquement à la cross-compile `xtensa-esp32s3-none-elf` (target_os =
//! "none"). Sur host (`cargo test`), `std` fournit l'allocateur natif.
//!
//! **Safety contract** : `heap_caps_malloc` et `heap_caps_free` sont exportés
//! en `extern "C"` par le composant `heap` d'ESP-IDF ; ils sont présents dans
//! tout firmware IDF v5+. Le linker `xtensa-esp-elf-ld` résoud ces symboles
//! au moment du link final côté `firmware-idf-v2/`.
//!
//! **Stratégie heap** : `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` (0x1008) —
//! on n'utilise JAMAIS la PSRAM pour le core Rust :
//! - latence SRAM interne ~2x plus déterministe
//! - évite cache coherency issues avec le tick 200 ms
//! - `BmuCore` fait ~1 KiB, 1 seule allocation au boot, zéro allocation per-tick

#![cfg(all(not(test), target_os = "none"))]

use core::alloc::{GlobalAlloc, Layout};

// Caps : MALLOC_CAP_INTERNAL (bit 11) | MALLOC_CAP_8BIT (bit 3) = 0x808.
// Vérification : esp-idf/components/heap/include/esp_heap_caps.h
//   MALLOC_CAP_8BIT    = (1 << 2)  → 0x0004 (valeur exacte suivant IDF 5.4)
//   MALLOC_CAP_INTERNAL = (1 << 3) → 0x0008
// Soit ensemble 0x000C. On utilise 0x0C — vérifier l'header ESP-IDF local.
//
// Note : la valeur exacte peut varier entre IDF versions. Phase 14 validation
// ajoute un test runtime (main.cpp vérifie `heap_caps_get_free_size(cap) > 0`).
const CAPS_INTERNAL_8BIT: u32 = 0x0C;

extern "C" {
    fn heap_caps_malloc(size: usize, caps: u32) -> *mut u8;
    fn heap_caps_free(ptr: *mut u8);
}

struct EspIdfHeap;

unsafe impl GlobalAlloc for EspIdfHeap {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        // Layout.align() est ignoré : heap_caps_malloc retourne des buffers
        // alignés sur 8 bytes minimum, suffisant pour toutes les structs Rust
        // de bmu-core (alignement max = 8 sur u64 / Option<RintResult>).
        //
        // Si un jour un type avec align > 8 est ajouté, utiliser
        // `heap_caps_aligned_alloc(layout.align(), layout.size(), caps)`.
        debug_assert!(layout.align() <= 8, "alignement > 8 non supporté");
        heap_caps_malloc(layout.size(), CAPS_INTERNAL_8BIT)
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        heap_caps_free(ptr);
    }
}

#[global_allocator]
static GLOBAL: EspIdfHeap = EspIdfHeap;
```

- [ ] **Step 3 : Exposer le module dans `lib.rs`**

Dans `firmware-rust/crates/bmu-core/src/lib.rs`, remplacer les anciens imports bump_alloc par :

```rust
// À la place de l'ancien `mod bump_alloc;` et de son `#[cfg(...)]` :
#[cfg(all(not(test), target_os = "none"))]
mod heap_caps_alloc;
```

- [ ] **Step 4 : Vérifier que le host test compile toujours**

```bash
. ~/export-esp.sh
cd firmware-rust
cargo test -p bmu-core --lib 2>&1 | tail -10
```

Expected : 13 tests passing (aucun impact host, le module est `cfg`-gated).

- [ ] **Step 5 : Vérifier que la cross-compile réussit SANS link final**

```bash
cd firmware-rust
cargo +esp xbuild 2>&1 | tail -15
```

Expected : `Finished release [optimized]` sans erreur. Les symboles `heap_caps_malloc` /
`heap_caps_free` seront des `UNDEFINED` dans le staticlib ; ils seront résolus au link ESP-IDF
en Task 12.4.

- [ ] **Step 6 : `cargo xtask size`**

```bash
cargo xtask size 2>&1 | tail -5
```

Expected : taille < 500 KB (bump allocator = 16 KiB static removed → légère baisse).

- [ ] **Step 7 : Clippy + fmt**

```bash
cargo clippy -p bmu-core --all-targets -- -D warnings 2>&1 | tail -10
cargo fmt -p bmu-core -- --check
```

Expected : clean.

---

### Task 12.2 : Créer `components/bmu_core_rs/` avec corrosion

**Files:**
- Create: `firmware-idf-v2/components/bmu_core_rs/CMakeLists.txt`
- Create: `firmware-idf-v2/components/bmu_core_rs/include/bmu_core.h` (vendoré)

- [ ] **Step 1 : Créer le répertoire composant**

```bash
mkdir -p firmware-idf-v2/components/bmu_core_rs/include
```

- [ ] **Step 2 : `components/bmu_core_rs/CMakeLists.txt`**

```cmake
# Component bmu_core_rs : wrapper ESP-IDF autour du staticlib Rust libbmu_core.a
# produit par le workspace firmware-rust/ via corrosion.
#
# corrosion_import_crate :
#   - lit firmware-rust/Cargo.toml
#   - invoque `cargo build --target xtensa-esp32s3-none-elf --release -p bmu-core`
#     (via `cargo +esp` — voir CARGO_BUILD_RUSTFLAGS plus bas)
#   - expose la cible CMake `bmu-core-static` qui sera linkée au binaire
#
# Le header `bmu_core.h` est vendoré dans include/ via `cargo xtask vendor-header`
# (Task 12.3). Ne PAS le régénérer côté CMake : on veut un artefact explicite en git
# pour audit et review.

idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES esp_common heap
)

include(FetchContent)

# Pin corrosion v0.6.0 — aligné spec §11.3
FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG v0.6.0
)
FetchContent_MakeAvailable(Corrosion)

# Import du staticlib Rust
corrosion_import_crate(
    MANIFEST_PATH "${CMAKE_SOURCE_DIR}/../firmware-rust/Cargo.toml"
    CRATES bmu-core
    PROFILE release
    LOCKED
)

# Fixer target + toolchain
# Le toolchain `+esp` est fourni par espup ; xtensa-esp32s3-none-elf est le target
# attendu par Part 1 et aligné sur ESP-IDF esp32s3.
corrosion_set_env_vars(bmu-core
    CARGO_BUILD_TARGET=xtensa-esp32s3-none-elf
    RUSTUP_TOOLCHAIN=esp
)

# Linker le staticlib au component (visible pour les autres components)
target_link_libraries(${COMPONENT_LIB} INTERFACE bmu-core-static)
```

- [ ] **Step 3 : Vendoriser `bmu_core.h` via xtask**

```bash
cd firmware-rust
cargo xtask vendor-header 2>&1 | tail -5
```

Expected : `Vendored header: .../firmware-idf-v2/components/bmu_core_rs/include/bmu_core.h`.
Fichier copié ~210 lignes.

- [ ] **Step 4 : Vérifier qu'il contient `#define MAX_BATTERIES 16`**

```bash
grep -n "MAX_BATTERIES\|bmu_core_init\|bmu_core_tick" firmware-idf-v2/components/bmu_core_rs/include/bmu_core.h
```

Expected :
```
#define MAX_BATTERIES 16
... int32_t bmu_core_init(...);
... int32_t bmu_core_tick(...);
...
```

- [ ] **Step 5 : Ajouter `bmu_core_rs` aux `REQUIRES` de `main/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_timer
        esp_system
        nvs_flash
        esp_lvgl_port
        esp-box-3
        bmu_core_rs      # ← nouveau
)
```

- [ ] **Step 6 : Premier build avec corrosion**

```bash
source ~/esp/esp-idf/export.sh
. ~/export-esp.sh  # nécessaire pour que le `cargo build` déclenché par corrosion trouve le toolchain +esp
cd firmware-idf-v2
idf.py build 2>&1 | tail -30
```

Expected : CMake télécharge corrosion (première fois), invoque `cargo +esp build --target xtensa-esp32s3-none-elf --release -p bmu-core`, produit `libbmu_core.a`, link avec le firmware. `Project build complete`.

**Troubleshooting possible** :
- Si `corrosion` échoue à fetch : vérifier connectivité réseau ; fallback sur git subtree.
- Si le toolchain `+esp` n'est pas trouvé : ajouter `~/.rustup/toolchains/esp/bin` au PATH dans
  l'environnement où `idf.py` est invoqué.
- Si `libbmu_core.a` produit mais symboles `heap_caps_*` unresolved : normal à cette étape,
  le link final (Task 12.4) les résoudra.

---

### Task 12.3 : Intégration `main.cpp` avec `bmu_core_init`

**Files:**
- Modify: `firmware-idf-v2/main/main.cpp`

- [ ] **Step 1 : Ajouter l'include `bmu_core.h`**

```cpp
extern "C" {
#include "bmu_core.h"
}
```

- [ ] **Step 2 : Init du core Rust dans `app_main`**

Après `init_nvs()` et avant `init_display_and_splash()`, ajouter :

```cpp
static BmuCore *s_core = nullptr;

static void init_bmu_core(void) {
    BmuConfigC cfg = {
        .umin_mv = 24000,
        .umax_mv = 30000,
        .imax_ma = 1000,
        .vdiff_imbalance_mv = 1000,
        .nb_switch_max = 5,
        .reconnect_delay_ms = 10000,
        .tick_period_ms = 200,
    };

    s_core = bmu_core_init(&cfg);
    if (s_core == nullptr) {
        ESP_LOGE(TAG, "bmu_core_init returned NULL — check cfg validation");
        return;
    }
    ESP_LOGI(TAG, "bmu_core_init OK, handle=%p", s_core);
}
```

- [ ] **Step 3 : Remplacer l'idle loop par un tick factice 1 Hz**

```cpp
static void tick_loop_fake(void) {
    BmuRawInputs raw = {
        .n_ina = 0,  // topology vide → fleet fail-safe all OFF
        .n_tca = 0,
        .ina_registers = {{0}},
        .tca_inputs = {0},
        .climate_temp_c10 = 230,   // 23.0 °C
        .climate_rh_pct10 = 450,   // 45.0 %
        .monotonic_us = 0,
    };
    BmuSnapshotC snap = {0};
    BmuActionsC actions = {0};

    while (true) {
        raw.monotonic_us = esp_timer_get_time();
        int32_t rc = bmu_core_tick(s_core, &raw, &snap, &actions);
        if (rc != 0) {
            ESP_LOGW(TAG, "bmu_core_tick rc=%ld", rc);
        } else {
            ESP_LOGI(TAG, "tick OK n_bat=%u topo=%u heap=%zu kB",
                     snap.n_bat,
                     snap.system.topology_ok,
                     esp_get_free_heap_size() / 1024);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 4 : Modifier `app_main` pour appeler `init_bmu_core` puis `tick_loop_fake`**

```cpp
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== BMU v2 boot ===");
    ESP_LOGI(TAG, "FW version: %s", CONFIG_APP_PROJECT_VER);

    init_nvs();
    init_display_and_splash();
    init_bmu_core();

    if (s_core == nullptr) {
        ESP_LOGE(TAG, "Core init failed, halting");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Boot complete — entering fake tick loop 1Hz");
    tick_loop_fake();
}
```

---

### Task 12.4 : Build, flash, verify

- [ ] **Step 1 : Build complet**

```bash
source ~/esp/esp-idf/export.sh
. ~/export-esp.sh
cd firmware-idf-v2
idf.py build 2>&1 | tail -30
```

Expected : `Project build complete`. Les symboles `heap_caps_malloc` / `heap_caps_free` sont
résolus par le component `heap` d'ESP-IDF (via `REQUIRES heap` dans `components/bmu_core_rs/CMakeLists.txt`).

**Si `heap_caps_malloc` unresolved** : ajouter `esp_common heap` aux `REQUIRES` du component
`bmu_core_rs` CMakeLists (déjà fait en Task 12.2 Step 2, vérifier).

- [ ] **Step 2 : Taille flash < 60% OTA (1.2 MB)**

```bash
idf.py size 2>&1 | tail -20
```

Expected output contient quelque chose comme :
```
Total sizes:
 DRAM .data size:   XXX bytes
 DRAM .bss  size:   XXX bytes
Used static DRAM:   XXX bytes
Used static IRAM:   XXX bytes
Flash code:         ~1000000 bytes  (< 1.2 MB)
Flash rodata:       XXX bytes
```

Flash code + rodata doit être ≤ 1.2 MB (60% de 2 MB OTA).

- [ ] **Step 3 : Flash + monitor**

```bash
idf.py -p /dev/cu.usbmodem* flash monitor
```

Expected monitor output après boot :
```
I (XXX) bmu-v2: === BMU v2 boot ===
I (XXX) bmu-v2: NVS initialized
I (XXX) bmu-v2: Splash displayed
I (XXX) bmu-v2: bmu_core_init OK, handle=0x3FCXXXXX
I (XXX) bmu-v2: Boot complete — entering fake tick loop 1Hz
I (XXX) bmu-v2: tick OK n_bat=0 topo=0 heap=YYY kB
I (XXX) bmu-v2: tick OK n_bat=0 topo=0 heap=YYY kB
I (XXX) bmu-v2: tick OK n_bat=0 topo=0 heap=YYY kB
...
```

Le `heap` ne doit **pas décroître** au fil des ticks (zéro allocation per-tick — garantie par
Part 1 design : `BmuCore::step` n'alloue jamais).

- [ ] **Step 4 : Stabilité 5 min**

Laisser tourner 5 min, observer :
- Aucun crash / reboot
- Heap free stable (tolérance ±1 KB)
- Aucun message ERROR / WARN de `bmu-v2`

---

### Task 12.5 : Pre-commit hook — divergence header check

**Files:**
- Create: `firmware-idf-v2/.git-hooks/pre-commit` (optionnel, documenté pour info)

- [ ] **Step 1 : Documenter le risque de divergence header**

`bmu_core.h` vit à deux endroits :
- `firmware-rust/target/include/bmu_core.h` — généré par cbindgen à chaque `cargo build -p bmu-core`
- `firmware-idf-v2/components/bmu_core_rs/include/bmu_core.h` — vendoré via `cargo xtask vendor-header`

Si un développeur modifie `firmware-rust/crates/bmu-core/src/ffi_types.rs` sans re-vendoriser, le
header vendoré devient obsolète. Spec §11.4 exige un pre-commit hook pour catch ça.

- [ ] **Step 2 : Ajouter une check CI simple**

Dans `.github/workflows/firmware-rust-ci.yml` (existant depuis Part 1), ajouter un step :

```yaml
      - name: Verify vendored bmu_core.h is up-to-date
        run: |
          cd firmware-rust
          . ~/export-esp.sh
          cargo build -p bmu-core --release
          cargo xtask vendor-header
          if ! git diff --quiet firmware-idf-v2/components/bmu_core_rs/include/bmu_core.h; then
            echo "ERROR: vendored bmu_core.h is out of date. Run 'cargo xtask vendor-header' and commit."
            git diff firmware-idf-v2/components/bmu_core_rs/include/bmu_core.h
            exit 1
          fi
```

Note : pour ce plan on documente seulement. L'ajout CI peut être fait en Phase 22 (pre-merge).

---

### Task 12.6 : Commit Phase 12

- [ ] **Step 1 : `git status` review**

Expected modifications :
- `firmware-rust/crates/bmu-core/src/lib.rs` (bump_alloc retiré)
- `firmware-rust/crates/bmu-core/src/heap_caps_alloc.rs` (nouveau)
- `firmware-idf-v2/components/bmu_core_rs/CMakeLists.txt` (nouveau)
- `firmware-idf-v2/components/bmu_core_rs/include/bmu_core.h` (vendoré)
- `firmware-idf-v2/main/CMakeLists.txt` (REQUIRES bmu_core_rs)
- `firmware-idf-v2/main/main.cpp` (bmu_core_init + tick loop)

- [ ] **Step 2 : Commit**

```bash
git add firmware-rust/crates/bmu-core/src/ \
        firmware-idf-v2/components/bmu_core_rs/ \
        firmware-idf-v2/main/
git commit -m "$(cat <<'EOF'
feat(phase-12): integrate bmu_core_rs via corrosion

- Replace bump allocator with heap_caps_malloc FFI wrapper
  (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, SRAM only, no PSRAM)
- components/bmu_core_rs/ with FetchContent corrosion v0.6.0
- corrosion_import_crate firmware-rust/Cargo.toml, CRATES bmu-core
- Vendored bmu_core.h via cargo xtask vendor-header
- main.cpp: bmu_core_init + fake BmuRawInputs loop 1 Hz
- Flash code under 60% OTA budget
EOF
)"
```

Subject `feat(phase-12): integrate bmu_core_rs via corrosion` = 50 chars ✓ (borderline).
Alternative plus courte : `feat(phase-12): wire bmu_core_rs via corrosion` = 46 chars.

---

# Phase 13 — `bmu_i2c_glue` + I²C réel

**Objectif** : implémenter le composant C `bmu_i2c_glue` qui lit les INA237 (16×), TCA9535 (4×) et
AHT30 (1×) sur le bus I²C DOCK, remplit `BmuRawInputs` avec les bytes bruts, et les pousse dans
`bmu_core_tick`. Comparaison côte-à-côte avec l'archive v1 sur bench hardware.

**Durée estimée** : 1-2 jours. **Risk** : élevé (hardware requis, timing I²C, parsing registres).
**Acceptance** : snapshot Rust contient V/I réels conformes à l'archive v1 (delta ≤ 1 %).

---

### Task 13.1 : Créer `components/bmu_i2c_glue/` squelette

**Files:**
- Create: `firmware-idf-v2/components/bmu_i2c_glue/CMakeLists.txt`
- Create: `firmware-idf-v2/components/bmu_i2c_glue/include/bmu_i2c_glue.h`
- Create: `firmware-idf-v2/components/bmu_i2c_glue/src/bmu_i2c_glue.c`

- [ ] **Step 1 : `CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "src/bmu_i2c_glue.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_timer
        esp-box-3
)
```

- [ ] **Step 2 : `include/bmu_i2c_glue.h`**

```c
/*
 * bmu_i2c_glue — C layer between ESP-IDF i2c_master driver and the Rust core's
 * BmuRawInputs. No parsing: raw register dumps only. The Rust core's
 * bmu-drivers::ina237::parse_vbus/parse_current/parse_dietemp_c10 do all the
 * conversion.
 *
 * Hardware (KXKM PCB v2, ESP32-S3-BOX-3 DOCK bus):
 *   - I²C port : I2C_NUM_1 (BSP-owned, retrieved via i2c_master_get_bus_handle)
 *   - SDA      : GPIO 41
 *   - SCL      : GPIO 40
 *   - Freq     : 100 kHz
 *   - INA237   : 0x40..0x4F (16 devices max)
 *   - TCA9535  : 0x20..0x23 (4 devices max, 4 channels each = 16 batteries)
 *   - AHT30    : 0x38 (single device)
 *
 * Thread-safety: all reads go through a FreeRTOS semaphore internally.
 * Callers do NOT need to hold a lock themselves.
 *
 * Topology contract: Nb_TCA × 4 == Nb_INA (enforced by Rust core).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "bmu_core.h"   // BmuRawInputs

#ifdef __cplusplus
extern "C" {
#endif

// --- Hardware constants (match firmware-idf v1 archive) ---
#define BMU_I2C_PORT              I2C_NUM_1
#define BMU_I2C_SDA_GPIO          GPIO_NUM_41
#define BMU_I2C_SCL_GPIO          GPIO_NUM_40
#define BMU_I2C_FREQ_HZ           100000

#define BMU_INA237_ADDR_BASE      0x40u
#define BMU_INA237_ADDR_MAX       0x4Fu
#define BMU_INA237_REG_DUMP_LEN   18u

#define BMU_TCA9535_ADDR_BASE     0x20u
#define BMU_TCA9535_ADDR_MAX      0x23u
#define BMU_TCA9535_INPUT_PORT0   0x00u

#define BMU_AHT30_ADDR            0x38u
#define BMU_AHT30_MEAS_WAIT_MS    80u

#define BMU_I2C_DEVICE_TIMEOUT_MS 50

/**
 * Initialize the glue: retrieve BSP bus handle, create device handles for all
 * candidate addresses, create the bus lock semaphore.
 *
 * Must be called once before any other bmu_i2c_glue_* function.
 *
 * @return ESP_OK on success.
 */
esp_err_t bmu_i2c_glue_init(void);

/**
 * Scan all INA237 and TCA9535 candidate addresses via address probe.
 * Populates `out_n_ina` and `out_n_tca` with actually responding devices.
 *
 * @param out_n_ina Number of INA237 detected (0..16)
 * @param out_n_tca Number of TCA9535 detected (0..4)
 * @return ESP_OK on success (even if 0 devices found)
 */
esp_err_t bmu_i2c_glue_scan(uint8_t *out_n_ina, uint8_t *out_n_tca);

/**
 * Read ALL inputs into a BmuRawInputs struct, ready to pass to bmu_core_tick.
 * Caller provides the struct zeroed; this function fills only the fields the
 * glue owns:
 *   - n_ina, n_tca (from last scan, cached)
 *   - ina_registers[i][0..18] for each INA237 at address 0x40+i
 *   - tca_inputs[i] for each TCA9535 at address 0x20+i
 *   - climate_temp_c10, climate_rh_pct10
 *   - monotonic_us (= esp_timer_get_time())
 *
 * On per-device failure, that device's slot is zeroed and the function
 * continues with the next device. Per-device error counters are incremented
 * internally; after 3 consecutive fails, bmu_i2c_glue_recover_bus() is
 * automatically invoked (not in this task — see Phase 14).
 *
 * @param out Target struct (must be non-NULL, can be pre-zeroed or not)
 * @return ESP_OK on success (even if some devices failed — errors are
 *         reported via the per-device counters and the topology diff).
 */
esp_err_t bmu_i2c_glue_read_inputs(BmuRawInputs *out);

/**
 * Bus recovery sequence: bit-bang 9 clock pulses on SCL + STOP condition.
 * Used when a device held SDA low. Called by the scheduler in Phase 14 after
 * 3 consecutive read failures.
 *
 * Note: uses the BSP bus handle + direct GPIO manipulation. Invalidates the
 * bus driver state temporarily.
 *
 * @return ESP_OK on success
 */
esp_err_t bmu_i2c_glue_recover_bus(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3 : `src/bmu_i2c_glue.c` squelette** (implementation en Task 13.2-13.4)

```c
/*
 * See bmu_i2c_glue.h for contract.
 *
 * Current stub: init + scan return ESP_OK with zero devices. Task 13.2 adds
 * real INA237 read; Task 13.3 adds TCA9535; Task 13.4 adds AHT30; Task 13.5
 * wires read_inputs to fill BmuRawInputs fully.
 */

#include "bmu_i2c_glue.h"

#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "i2c-glue";

static i2c_master_bus_handle_t s_bus = NULL;
static SemaphoreHandle_t       s_lock = NULL;

// Per-address device handles (lazily created in scan)
static i2c_master_dev_handle_t s_ina_handles[16] = {NULL};
static i2c_master_dev_handle_t s_tca_handles[4]  = {NULL};
static i2c_master_dev_handle_t s_aht_handle      = NULL;

// Topology state (populated by scan, consumed by read_inputs)
static uint8_t s_n_ina = 0;
static uint8_t s_n_tca = 0;

esp_err_t bmu_i2c_glue_init(void) {
    if (s_bus != NULL) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_OK;
    }

    // Retrieve BSP-owned bus handle (BOX-3 BSP has already called bsp_i2c_init)
    esp_err_t err = i2c_master_get_bus_handle(BMU_I2C_PORT, &s_bus);
    if (err != ESP_OK || s_bus == NULL) {
        ESP_LOGE(TAG, "i2c_master_get_bus_handle failed: %s", esp_err_to_name(err));
        return err;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        ESP_LOGE(TAG, "mutex create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "initialized (bus handle=%p)", s_bus);
    return ESP_OK;
}

static esp_err_t create_dev_handle(uint8_t addr, i2c_master_dev_handle_t *out) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BMU_I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &dev_cfg, out);
}

esp_err_t bmu_i2c_glue_scan(uint8_t *out_n_ina, uint8_t *out_n_tca) {
    if (s_bus == NULL) return ESP_ERR_INVALID_STATE;
    if (out_n_ina == NULL || out_n_tca == NULL) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    s_n_ina = 0;
    s_n_tca = 0;

    // Scan INA237 range
    for (uint8_t a = BMU_INA237_ADDR_BASE; a <= BMU_INA237_ADDR_MAX; a++) {
        esp_err_t probe = i2c_master_probe(s_bus, a, 50);
        if (probe == ESP_OK) {
            uint8_t idx = a - BMU_INA237_ADDR_BASE;
            if (s_ina_handles[idx] == NULL) {
                create_dev_handle(a, &s_ina_handles[idx]);
            }
            s_n_ina++;
        }
    }

    // Scan TCA9535 range
    for (uint8_t a = BMU_TCA9535_ADDR_BASE; a <= BMU_TCA9535_ADDR_MAX; a++) {
        esp_err_t probe = i2c_master_probe(s_bus, a, 50);
        if (probe == ESP_OK) {
            uint8_t idx = a - BMU_TCA9535_ADDR_BASE;
            if (s_tca_handles[idx] == NULL) {
                create_dev_handle(a, &s_tca_handles[idx]);
            }
            s_n_tca++;
        }
    }

    // AHT30 (single address)
    esp_err_t aht_probe = i2c_master_probe(s_bus, BMU_AHT30_ADDR, 50);
    if (aht_probe == ESP_OK && s_aht_handle == NULL) {
        create_dev_handle(BMU_AHT30_ADDR, &s_aht_handle);
    }

    xSemaphoreGive(s_lock);

    *out_n_ina = s_n_ina;
    *out_n_tca = s_n_tca;

    ESP_LOGI(TAG, "scan result: INA=%u TCA=%u AHT=%s",
             s_n_ina, s_n_tca, (aht_probe == ESP_OK) ? "yes" : "no");
    return ESP_OK;
}

// Task 13.2+ : implémentation bmu_i2c_glue_read_inputs
esp_err_t bmu_i2c_glue_read_inputs(BmuRawInputs *out) {
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->monotonic_us = esp_timer_get_time();
    out->n_ina = s_n_ina;
    out->n_tca = s_n_tca;
    // TODO task 13.2: read INA237 raw registers
    // TODO task 13.3: read TCA9535 input ports
    // TODO task 13.4: read AHT30 climate
    return ESP_OK;
}

// Task 14.3 : implémentation bmu_i2c_glue_recover_bus
esp_err_t bmu_i2c_glue_recover_bus(void) {
    ESP_LOGW(TAG, "recover_bus stub — implemented in Phase 14 Task 14.3");
    return ESP_OK;
}
```

- [ ] **Step 4 : Build sanity**

```bash
cd firmware-idf-v2
idf.py build 2>&1 | tail -10
```

Expected : `Project build complete`. Pas encore d'appel depuis main.cpp.

---

### Task 13.2 : Lecture raw INA237 (18 bytes par device)

**Files:**
- Modify: `firmware-idf-v2/components/bmu_i2c_glue/src/bmu_i2c_glue.c`

**Context** : l'INA237 expose ~14 registres (0x00 config, 0x01 adc_config, 0x02 shunt_cal,
0x04 vshunt, 0x05 vbus, 0x06 dietemp, 0x07 current, 0x08 power, 0x0B diag, 0x3E manuf_id,
0x3F device_id — voir `bmu-drivers::ina237::Reg` in Part 1). Pour `BmuRawInputs.ina_registers[16][18]`
on dump les 18 premiers bytes (registres 0x00 à 0x08 inclus, chaque registre = 2 bytes).

Le core Rust consomme `regs[0..2]` comme VBUS et `regs[2..4]` comme CURRENT (convention fixée
dans Part 1 Task 9.3 `bmu_core_tick`). **Attention** : les noms de registres INA237 ne sont
PAS séquentiels (0x00 Config, 0x01 ADCConfig, 0x02 ShuntCal, PUIS saut à 0x04 VShunt). Phase 13
adopte une convention simplifiée : `regs[0..2]` = lecture directe du registre 0x05 VBUS,
`regs[2..4]` = lecture directe du registre 0x07 CURRENT. Les 14 bytes restants sont inutilisés
V1 (réservés Phase 15+ pour `parse_dietemp_c10`, etc.).

Cette convention est **opposée** à un dump séquentiel — elle privilégie la simplicité de mapping
avec le parseur Rust actuel (`bmu_core_tick` lit `regs[0..2]` et `regs[2..4]`).

- [ ] **Step 1 : Helper `read_ina237_raw()` en statique**

```c
/**
 * Read VBUS (reg 0x05) + CURRENT (reg 0x07) into a 18-byte buffer formatted
 * for BmuRawInputs consumption:
 *   bytes[0..2] = VBUS big-endian (direct from INA237)
 *   bytes[2..4] = CURRENT big-endian
 *   bytes[4..18] = zero (reserved for future SHUNT/DIETEMP/POWER reads)
 *
 * Returns ESP_OK on both reads success. On any error, the full buffer is zeroed.
 */
static esp_err_t read_ina237_raw(i2c_master_dev_handle_t dev, uint8_t out[18]) {
    memset(out, 0, 18);

    // Read VBUS (2 bytes)
    uint8_t reg_vbus = 0x05;
    esp_err_t err = i2c_master_transmit_receive(
        dev, &reg_vbus, 1, &out[0], 2,
        BMU_I2C_DEVICE_TIMEOUT_MS
    );
    if (err != ESP_OK) return err;

    // Read CURRENT (2 bytes)
    uint8_t reg_current = 0x07;
    err = i2c_master_transmit_receive(
        dev, &reg_current, 1, &out[2], 2,
        BMU_I2C_DEVICE_TIMEOUT_MS
    );
    if (err != ESP_OK) {
        memset(out, 0, 18);
        return err;
    }

    return ESP_OK;
}
```

- [ ] **Step 2 : Hook dans `bmu_i2c_glue_read_inputs`**

Remplacer le `// TODO task 13.2` par :

```c
    // Read all detected INA237s
    for (uint8_t i = 0; i < s_n_ina && i < 16; i++) {
        if (s_ina_handles[i] == NULL) continue;
        esp_err_t err = read_ina237_raw(s_ina_handles[i], out->ina_registers[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "INA237[0x%02X] read failed: %s",
                     BMU_INA237_ADDR_BASE + i, esp_err_to_name(err));
        }
    }
```

- [ ] **Step 3 : Build + hook test dans `main.cpp`**

Dans `main.cpp`, entre `init_bmu_core()` et `tick_loop_fake()`, ajouter :

```cpp
extern "C" {
#include "bmu_i2c_glue.h"
}

static void init_i2c_glue(void) {
    esp_err_t err = bmu_i2c_glue_init();
    ESP_ERROR_CHECK(err);

    uint8_t n_ina = 0, n_tca = 0;
    err = bmu_i2c_glue_scan(&n_ina, &n_tca);
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "I2C scan: INA=%u TCA=%u", n_ina, n_tca);
}
```

Et appeler `init_i2c_glue()` dans `app_main()` après `init_display_and_splash()`.

- [ ] **Step 4 : Build + flash + monitor sur bench hardware**

```bash
idf.py -p /dev/cu.usbmodem* flash monitor
```

Expected log :
```
I (XXX) i2c-glue: initialized (bus handle=0x...)
I (XXX) i2c-glue: scan result: INA=16 TCA=4 AHT=yes  (si bench 4x4)
I (XXX) bmu-v2: I2C scan: INA=16 TCA=4
```

Sur un bench partiel, les nombres peuvent différer (ex : INA=4 TCA=1 pour un banc de test 4 batteries).

- [ ] **Step 5 : Vérifier que le tick loop voit les INA**

Modifier `tick_loop_fake` pour appeler `bmu_i2c_glue_read_inputs` au lieu de charger `raw` à vide :

```cpp
static void tick_loop_real(void) {
    BmuRawInputs raw;
    BmuSnapshotC snap = {0};
    BmuActionsC actions = {0};

    while (true) {
        esp_err_t err = bmu_i2c_glue_read_inputs(&raw);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "read_inputs failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        int32_t rc = bmu_core_tick(s_core, &raw, &snap, &actions);
        if (rc == 0 && snap.n_bat > 0) {
            ESP_LOGI(TAG, "tick OK n_bat=%u bat0: V=%ld mV I=%ld mA state=%u",
                     snap.n_bat,
                     snap.batteries[0].voltage_mv,
                     snap.batteries[0].current_ma,
                     snap.batteries[0].state);
        } else {
            ESP_LOGW(TAG, "tick rc=%ld n_bat=%u (no valid batteries)", rc, snap.n_bat);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

Remplacer `tick_loop_fake()` par `tick_loop_real()` dans `app_main`.

**Expected bench output** (bench 4 batteries, toutes à ~27 V / -500 mA) :
```
I (XXX) bmu-v2: tick OK n_bat=4 bat0: V=27012 mA I=-498 state=2
```

`state=2` = Online. Si `state=3` (Offline) ou `n_bat=0`, debugger la topologie TCA/INA (Task 13.3).

---

### Task 13.3 : Lecture TCA9535 INPUT_PORT0

- [ ] **Step 1 : Helper `read_tca9535_inputs()`**

```c
/**
 * Read INPUT_PORT0 (reg 0x00) of a TCA9535. Populates out[0] with the byte.
 *
 * The Rust core's `bmu-drivers::tca9535::channel_alert_active` expects port0
 * as a single byte; we read only INPUT_PORT0 (not INPUT_PORT1).
 *
 * Returns ESP_OK on success. On failure, out[0] is set to 0xFF (all inputs
 * high = no alert, fail-safe).
 */
static esp_err_t read_tca9535_inputs(i2c_master_dev_handle_t dev, uint8_t *out) {
    uint8_t reg = BMU_TCA9535_INPUT_PORT0;
    esp_err_t err = i2c_master_transmit_receive(
        dev, &reg, 1, out, 1,
        BMU_I2C_DEVICE_TIMEOUT_MS
    );
    if (err != ESP_OK) {
        *out = 0xFF;  // fail-safe: all alerts inactive
    }
    return err;
}
```

- [ ] **Step 2 : Hook dans `bmu_i2c_glue_read_inputs`**

Après la boucle INA237, ajouter :

```c
    // Read all detected TCA9535s
    for (uint8_t i = 0; i < s_n_tca && i < 4; i++) {
        if (s_tca_handles[i] == NULL) continue;
        esp_err_t err = read_tca9535_inputs(s_tca_handles[i], &out->tca_inputs[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "TCA9535[0x%02X] read failed: %s",
                     BMU_TCA9535_ADDR_BASE + i, esp_err_to_name(err));
        }
    }
```

- [ ] **Step 3 : Build + flash + verify alerts column in log**

Étendre le log tick pour afficher `tca_inputs[0]` en hex :

```cpp
ESP_LOGI(TAG, "tick OK n_bat=%u bat0: V=%ld mV I=%ld mA state=%u tca0=0x%02X",
         snap.n_bat, ..., raw.tca_inputs[0]);
```

Expected : `tca0=0xF0` si tous les canaux sont sans alert (haut bits inputs), ou `0x70` si alert
sur canal 0, etc. (mapping spec §4.3 : bits 4-7 = alerts canal 0-3 reversed).

---

### Task 13.4 : Lecture AHT30 climate

- [ ] **Step 1 : Helper `read_aht30()`**

L'AHT30 exige une séquence trigger + wait 80 ms + read 6 bytes. Pattern identique au driver Rust
`bmu-drivers::aht30` (cf Part 1 Task 5.2).

```c
/**
 * Trigger AHT30 measurement, wait 80 ms, read 6-byte result. Parses into
 * temp_c10 / rh_pct10 using the Q20 fixed-point math from AHT30 datasheet.
 *
 * Returns ESP_OK on success. On failure, out_temp and out_rh are left unchanged.
 */
static esp_err_t read_aht30(i2c_master_dev_handle_t dev, int16_t *out_temp_c10, uint16_t *out_rh_pct10) {
    static const uint8_t CMD_TRIGGER[3] = {0xAC, 0x33, 0x00};
    uint8_t raw[6] = {0};

    // Trigger
    esp_err_t err = i2c_master_transmit(dev, CMD_TRIGGER, 3, BMU_I2C_DEVICE_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    // Wait for conversion
    vTaskDelay(pdMS_TO_TICKS(BMU_AHT30_MEAS_WAIT_MS));

    // Read 6 bytes
    err = i2c_master_receive(dev, raw, 6, BMU_I2C_DEVICE_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    // Check busy flag
    if (raw[0] & 0x80) return ESP_ERR_INVALID_RESPONSE;

    // Parse 20-bit RH: raw[1] << 12 | raw[2] << 4 | raw[3] >> 4
    uint32_t rh_raw = ((uint32_t)raw[1] << 12) | ((uint32_t)raw[2] << 4) | ((uint32_t)raw[3] >> 4);
    // Parse 20-bit temp: (raw[3] & 0x0F) << 16 | raw[4] << 8 | raw[5]
    uint32_t t_raw = (((uint32_t)raw[3] & 0x0F) << 16) | ((uint32_t)raw[4] << 8) | (uint32_t)raw[5];

    // RH in pct10: (raw * 1000) >> 20
    *out_rh_pct10 = (uint16_t)(((uint64_t)rh_raw * 1000u) >> 20);
    // Temp in c10: ((raw * 2000) >> 20) - 500
    int32_t t_scaled = (int32_t)(((uint64_t)t_raw * 2000u) >> 20);
    *out_temp_c10 = (int16_t)(t_scaled - 500);

    return ESP_OK;
}
```

- [ ] **Step 2 : Hook dans `bmu_i2c_glue_read_inputs`**

Après la boucle TCA9535, ajouter :

```c
    // Read AHT30 climate (ignore failures — climate is non-critical)
    if (s_aht_handle != NULL) {
        int16_t t = 0;
        uint16_t rh = 0;
        if (read_aht30(s_aht_handle, &t, &rh) == ESP_OK) {
            out->climate_temp_c10 = t;
            out->climate_rh_pct10 = rh;
        }
        // else: laisser à 0 (climate non-critique, protection ne l'utilise pas V1)
    }
```

- [ ] **Step 3 : Build + flash + verify**

Expected log après ajout de la sortie climate :
```
I (XXX) bmu-v2: tick OK ... climate T=230 RH=450
```

Soit 23.0 °C et 45.0 % sur un bench en conditions normales.

---

### Task 13.5 : Comparaison côte-à-côte avec archive v1

**Objectif** : flasher l'archive v1 (`firmware-idf/`) sur le même bench, noter les V/I/alerts affichés
en série, puis flasher v2 et comparer. Tolérance : delta ≤ 1 % sur V, ≤ 5 % sur I (les INA237 sont
calibrés différemment entre runs).

- [ ] **Step 1 : Flash v1**

```bash
git worktree add /tmp/bmu-v1 archive/firmware-v1
cd /tmp/bmu-v1/firmware-idf
source ~/esp/esp-idf/export.sh
idf.py -p /dev/cu.usbmodem* flash monitor
```

Expected : monitor série affiche les V/I par batterie. Capturer un screenshot/log 30 s.

- [ ] **Step 2 : Flash v2**

```bash
cd /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator/firmware-idf-v2
idf.py -p /dev/cu.usbmodem* flash monitor
```

Capturer 30 s de log également.

- [ ] **Step 3 : Comparer manuellement**

Pour chaque batterie, comparer :
- Tension (v1 float vs v2 i32 mV)
- Courant
- État (Online/Offline)

Si delta V > 1 % : vérifier calibration `SHUNT_CAL` côté C (Task 14.2 l'ajoutera si absente) et
`current_lsb_na` côté Rust (hardcodé 100 A en `bmu_core_tick`).

Si alerts divergents : vérifier mapping `SWITCH_PIN` / `ALERT_PIN` côté driver TCA9535 Rust
(Part 1 Task 5.1) contre l'archive v1.

- [ ] **Step 4 : Documenter les résultats dans `docs/superpowers/validation/runs/`**

```bash
mkdir -p docs/superpowers/validation/runs
cat > docs/superpowers/validation/runs/2026-04-XX-phase13-v1-v2-comparison.md << 'EOF'
# Phase 13 bench — v1 vs v2 comparison

Date: YYYY-MM-DD
Bench: N batteries (list models, S/N if any)
Bus: I2C_NUM_1 DOCK @ 100 kHz

## Results

| Battery | V1 mV | V2 mV | Delta % | V1 mA | V2 mA | Delta % | State match |
|---------|-------|-------|---------|-------|-------|---------|-------------|
| 0       | ...   | ...   | ...     | ...   | ...   | ...     | yes/no      |
...

## Notes
- Calibration: ...
- Observed issues: ...
EOF
```

---

### Task 13.6 : Commit Phase 13

- [ ] **Step 1 : `git status` review**

Expected :
- `firmware-idf-v2/components/bmu_i2c_glue/` (nouveau)
- `firmware-idf-v2/main/CMakeLists.txt` (REQUIRES bmu_i2c_glue)
- `firmware-idf-v2/main/main.cpp` (init_i2c_glue + tick_loop_real)

- [ ] **Step 2 : Commit**

```bash
git add firmware-idf-v2/components/bmu_i2c_glue/ \
        firmware-idf-v2/main/
git commit -m "$(cat <<'EOF'
feat(phase-13): bmu_i2c_glue raw reads INA/TCA/AHT

- bmu_i2c_glue_init: retrieves BSP-owned I2C_NUM_1 bus handle
- bmu_i2c_glue_scan: probes 0x40-0x4F (INA) + 0x20-0x23 (TCA) + 0x38
- read_ina237_raw: VBUS (reg 0x05) + CURRENT (reg 0x07) into
  BmuRawInputs.ina_registers[i][0..4], rest zero
- read_tca9535_inputs: INPUT_PORT0 single byte per device
- read_aht30: trigger + 80 ms wait + 6-byte read + Q20 parse
- main.cpp: tick_loop_real calls read_inputs + bmu_core_tick
- Bench comparison v1 vs v2 validated under 1% delta
EOF
)"
```

Subject `feat(phase-13): bmu_i2c_glue raw reads INA/TCA/AHT` = 48 chars ✓.

---

# Phase 14 — `task_bmu_core` pinné PRO_CPU + WDT + bus recovery

**Objectif** : sortir le tick loop de `app_main` et le pousser dans une FreeRTOS task pinned sur
PRO_CPU (core 0), priorité 5, période 200 ms, WDT 3 s. Implémenter la politique retry I²C
(3 fails → bus recovery) et la séquence recovery bit-bang. Mesurer latence p50/p99 via esp_timer.
Stabilité 1h sur bench sans reboot WDT.

**Durée estimée** : 1 jour. **Risk** : élevé (temps réel). **Acceptance** : 1h bench stable, p99 ≤ 50 ms.

---

### Task 14.1 : `task_bmu_core.cpp` squelette + pinning

**Files:**
- Create: `firmware-idf-v2/main/task_bmu_core.cpp`
- Create: `firmware-idf-v2/main/task_bmu_core.h`
- Modify: `firmware-idf-v2/main/main.cpp`
- Modify: `firmware-idf-v2/main/CMakeLists.txt`

- [ ] **Step 1 : `task_bmu_core.h`**

```c
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Spawn the BMU core tick task.
 *
 * Task properties:
 *   - Name:     task_bmu_core
 *   - Core:     PRO_CPU (0)
 *   - Priority: 5 (protection)
 *   - Stack:    8 KiB (large for BmuSnapshotC on-stack copies)
 *   - Period:   tick_period_ms from BmuConfigC (default 200 ms)
 *   - WDT:      registered with 3 s timeout
 *
 * @param core BmuCore handle returned by bmu_core_init
 * @return ESP_OK on xTaskCreatePinnedToCore success
 */
esp_err_t task_bmu_core_start(void *core);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2 : `task_bmu_core.cpp` squelette**

```cpp
#include "task_bmu_core.h"

#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "bmu_core.h"
#include "bmu_i2c_glue.h"
}

static const char *TAG = "task_core";

static constexpr uint32_t TICK_PERIOD_MS = 200;
static constexpr int PROTECTION_PRIORITY = 5;
static constexpr int PIN_PRO_CPU = 0;
static constexpr uint32_t STACK_SIZE = 8 * 1024;

static BmuCore *s_core = nullptr;

// Latency histogram (simplistic p50/p99 via sorted recent samples)
static constexpr size_t LATENCY_WINDOW = 300;  // 1 min @ 5 Hz
static uint32_t s_lat_us[LATENCY_WINDOW] = {0};
static size_t   s_lat_pos = 0;
static uint32_t s_tick_count = 0;

static void record_latency(uint32_t us) {
    s_lat_us[s_lat_pos] = us;
    s_lat_pos = (s_lat_pos + 1) % LATENCY_WINDOW;
}

static void log_latency_every(uint32_t ticks_per_log) {
    if (s_tick_count % ticks_per_log != 0) return;

    // Copy + sort for percentile compute
    uint32_t sorted[LATENCY_WINDOW];
    size_t n = (s_tick_count >= LATENCY_WINDOW) ? LATENCY_WINDOW : s_tick_count;
    memcpy(sorted, s_lat_us, n * sizeof(uint32_t));
    // Simple insertion sort (n ≤ 300)
    for (size_t i = 1; i < n; i++) {
        uint32_t x = sorted[i];
        ssize_t j = i - 1;
        while (j >= 0 && sorted[j] > x) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = x;
    }
    uint32_t p50 = sorted[n / 2];
    uint32_t p99 = sorted[(n * 99) / 100];
    ESP_LOGI(TAG, "latency tick=%lu n=%zu p50=%lu us p99=%lu us",
             (unsigned long)s_tick_count, n, (unsigned long)p50, (unsigned long)p99);
}

static void task_body(void *arg) {
    s_core = (BmuCore *)arg;
    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    if (wdt_err != ESP_OK) {
        ESP_LOGW(TAG, "esp_task_wdt_add failed: %s", esp_err_to_name(wdt_err));
    }

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(TICK_PERIOD_MS);

    BmuRawInputs raw;
    BmuSnapshotC snap = {0};
    BmuActionsC actions = {0};

    ESP_LOGI(TAG, "started: core=PRO_CPU prio=%d period=%lu ms",
             PROTECTION_PRIORITY, (unsigned long)TICK_PERIOD_MS);

    while (true) {
        esp_task_wdt_reset();

        int64_t t_start = esp_timer_get_time();

        // Read I²C inputs
        esp_err_t read_err = bmu_i2c_glue_read_inputs(&raw);
        if (read_err != ESP_OK) {
            ESP_LOGW(TAG, "read_inputs failed: %s", esp_err_to_name(read_err));
            // Task 14.3 adds retry + bus_recover here
        }

        // Tick the Rust core
        int32_t rc = bmu_core_tick(s_core, &raw, &snap, &actions);
        if (rc != 0) {
            ESP_LOGW(TAG, "bmu_core_tick rc=%ld", rc);
        }

        // TODO Phase 15+ : consumer actions.tca_set_mask / clr_mask
        // via bmu_i2c_glue_apply_actions (future task)

        int64_t t_end = esp_timer_get_time();
        record_latency((uint32_t)(t_end - t_start));
        s_tick_count++;

        // Log every 50 ticks (10 s @ 5 Hz)
        log_latency_every(50);

        vTaskDelayUntil(&last_wake, period_ticks);
    }
}

esp_err_t task_bmu_core_start(void *core) {
    if (core == nullptr) return ESP_ERR_INVALID_ARG;

    BaseType_t res = xTaskCreatePinnedToCore(
        task_body,
        "task_bmu_core",
        STACK_SIZE,
        core,
        PROTECTION_PRIORITY,
        nullptr,
        PIN_PRO_CPU
    );
    if (res != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
```

- [ ] **Step 3 : `main/CMakeLists.txt` ajouter `task_bmu_core.cpp`**

```cmake
idf_component_register(
    SRCS "main.cpp" "task_bmu_core.cpp"
    ...
)
```

- [ ] **Step 4 : `main.cpp` — remplacer `tick_loop_real()` par `task_bmu_core_start()`**

```cpp
extern "C" {
#include "task_bmu_core.h"
}

// ... dans app_main, à la place de tick_loop_real(): ...
ESP_ERROR_CHECK(task_bmu_core_start(s_core));

// Idle loop main thread — laisse tourner la task
while (true) {
    vTaskDelay(pdMS_TO_TICKS(10000));
    ESP_LOGI(TAG, "main idle: heap free=%zu kB uptime=%llu s",
             esp_get_free_heap_size() / 1024,
             esp_timer_get_time() / 1000000ULL);
}
```

- [ ] **Step 5 : Build + flash + 10 min observation**

```bash
idf.py -p /dev/cu.usbmodem* flash monitor
```

Expected :
```
I (XXX) task_core: started: core=PRO_CPU prio=5 period=200 ms
I (XXX) task_core: latency tick=50 n=50 p50=XX us p99=YY us
I (XXX) task_core: latency tick=100 n=100 p50=XX us p99=YY us
...
I (XXX) bmu-v2: main idle: heap free=XXX kB uptime=10 s
```

Expected latences : p50 ≈ 1-5 ms (lecture I²C ~300 us/device × 16+ devices + tick Rust pure ~500 us),
p99 ≤ 50 ms.

Si p99 > 50 ms : debugger le bus I²C (freq, devices bloquants).
Si WDT reboot : augmenter temporairement `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5` et debugger.

---

### Task 14.2 : Init SHUNT_CAL côté C (pour cohérence avec Rust lsb hardcodé)

**Context** : Part 1 Task 9.3 hardcode `current_lsb_na(100_000)` dans `bmu_core_tick`.
Pour que les lectures de courant soient correctes, le registre `SHUNT_CAL` (0x02) de chaque INA237
doit être programmé avec une valeur cohérente. La formule est datée du plan Part 1 Task 4.1 :
`SHUNT_CAL = (lsb_na × shunt_μΩ × 8192) / 1e10` avec `max_current = 100 A` et le shunt KXKM
(500 μΩ typique). Valeur attendue ≈ 1249.

- [ ] **Step 1 : Ajouter `bmu_i2c_glue_program_shunt_cal()` dans `bmu_i2c_glue.c`**

```c
/**
 * Program SHUNT_CAL register (0x02) of all detected INA237s.
 *
 * Must match the `current_lsb_na` hardcoded in bmu-core's bmu_core_tick
 * (currently `current_lsb_na(100_000)` = 100 A max). For the KXKM 500 µΩ shunt,
 * SHUNT_CAL = 1249 (see firmware-rust Part 1 Task 4.1 test
 * `encode_shunt_cal_kxkm_100a_500u_ohm`).
 *
 * Called once after bmu_i2c_glue_scan, before the task loop starts.
 */
esp_err_t bmu_i2c_glue_program_shunt_cal(void) {
    // SHUNT_CAL value = 1249 (big-endian u16)
    const uint8_t payload[3] = {
        0x02,       // register 0x02 SHUNT_CAL
        0x04, 0xE1  // 0x04E1 = 1249 (100 A max, 500 µΩ shunt)
    };

    xSemaphoreTake(s_lock, portMAX_DELAY);
    int ok = 0;
    for (uint8_t i = 0; i < s_n_ina; i++) {
        if (s_ina_handles[i] == NULL) continue;
        esp_err_t err = i2c_master_transmit(s_ina_handles[i], payload, 3, BMU_I2C_DEVICE_TIMEOUT_MS);
        if (err == ESP_OK) ok++;
        else ESP_LOGW(TAG, "SHUNT_CAL write INA[0x%02X] failed: %s",
                      BMU_INA237_ADDR_BASE + i, esp_err_to_name(err));
    }
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "SHUNT_CAL programmed on %d/%u INA237", ok, s_n_ina);
    return (ok == s_n_ina) ? ESP_OK : ESP_ERR_INVALID_STATE;
}
```

- [ ] **Step 2 : Déclarer dans `bmu_i2c_glue.h`**

```c
esp_err_t bmu_i2c_glue_program_shunt_cal(void);
```

- [ ] **Step 3 : Appeler depuis `init_i2c_glue()` dans `main.cpp`**

```cpp
ESP_ERROR_CHECK(bmu_i2c_glue_program_shunt_cal());
```

- [ ] **Step 4 : Flash + verify courants live**

Sur bench avec charge connue (ex : 10 A constant), vérifier que `snap.batteries[i].current_ma` ≈ -10000
(convention négative = décharge).

---

### Task 14.3 : Retry + bus recovery policy

**Files:**
- Modify: `firmware-idf-v2/main/task_bmu_core.cpp`
- Modify: `firmware-idf-v2/components/bmu_i2c_glue/src/bmu_i2c_glue.c`

- [ ] **Step 1 : Implémenter `bmu_i2c_glue_recover_bus()`** (stub remplacé)

```c
esp_err_t bmu_i2c_glue_recover_bus(void) {
    ESP_LOGW(TAG, "bus recovery: bit-banging 9 clock pulses + STOP");

    // Remove all device handles (the driver state is invalidated)
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (uint8_t i = 0; i < 16; i++) {
        if (s_ina_handles[i] != NULL) {
            i2c_master_bus_rm_device(s_ina_handles[i]);
            s_ina_handles[i] = NULL;
        }
    }
    for (uint8_t i = 0; i < 4; i++) {
        if (s_tca_handles[i] != NULL) {
            i2c_master_bus_rm_device(s_tca_handles[i]);
            s_tca_handles[i] = NULL;
        }
    }
    if (s_aht_handle != NULL) {
        i2c_master_bus_rm_device(s_aht_handle);
        s_aht_handle = NULL;
    }
    xSemaphoreGive(s_lock);

    // Bit-bang 9 clock pulses on SCL while SDA is released (input with pull-up)
    gpio_set_direction(BMU_I2C_SDA_GPIO, GPIO_MODE_INPUT);
    gpio_set_direction(BMU_I2C_SCL_GPIO, GPIO_MODE_OUTPUT);
    for (int i = 0; i < 9; i++) {
        gpio_set_level(BMU_I2C_SCL_GPIO, 0);
        esp_rom_delay_us(5);
        gpio_set_level(BMU_I2C_SCL_GPIO, 1);
        esp_rom_delay_us(5);
    }
    // STOP condition: SDA low → high while SCL high
    gpio_set_direction(BMU_I2C_SDA_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BMU_I2C_SDA_GPIO, 0);
    esp_rom_delay_us(5);
    gpio_set_level(BMU_I2C_SDA_GPIO, 1);
    esp_rom_delay_us(5);

    // Re-init GPIOs to I²C mode (the BSP bus re-takes ownership)
    // Note: simple rescan re-creates device handles
    uint8_t n_ina = 0, n_tca = 0;
    bmu_i2c_glue_scan(&n_ina, &n_tca);
    bmu_i2c_glue_program_shunt_cal();

    ESP_LOGI(TAG, "bus recovery complete: INA=%u TCA=%u", n_ina, n_tca);
    return ESP_OK;
}
```

- [ ] **Step 2 : Fail counter dans `task_bmu_core.cpp`**

```cpp
static uint32_t s_consecutive_fails = 0;
static constexpr uint32_t FAIL_THRESHOLD = 3;

// ... dans task_body, après bmu_i2c_glue_read_inputs:
if (read_err != ESP_OK) {
    s_consecutive_fails++;
    ESP_LOGW(TAG, "read_inputs fail #%lu", (unsigned long)s_consecutive_fails);
    if (s_consecutive_fails >= FAIL_THRESHOLD) {
        ESP_LOGW(TAG, "triggering bus recovery");
        bmu_i2c_glue_recover_bus();
        s_consecutive_fails = 0;
    }
} else {
    s_consecutive_fails = 0;
}
```

- [ ] **Step 3 : Test de recovery**

Simuler une panne bus (court-circuiter SDA à GND pendant 2 s), observer le log :
```
W (XXX) task_core: read_inputs fail #1
W (XXX) task_core: read_inputs fail #2
W (XXX) task_core: read_inputs fail #3
W (XXX) task_core: triggering bus recovery
W (XXX) i2c-glue: bus recovery: bit-banging 9 clock pulses + STOP
I (XXX) i2c-glue: scan result: INA=16 TCA=4 AHT=yes
I (XXX) i2c-glue: bus recovery complete: INA=16 TCA=4
```

Puis reprise normale.

---

### Task 14.4 : Mesure latence p50/p99 + test 1h

**Files:** aucun (exécution / mesure uniquement)

- [ ] **Step 1 : Flash + let tourner 1h sur bench**

```bash
idf.py -p /dev/cu.usbmodem* flash monitor 2>&1 | tee /tmp/phase14-1h-bench.log
```

Laisser 1 h, observer :
- Aucun reboot (WDT ou panic)
- `heap free` stable
- `p50` ≤ 10 ms, `p99` ≤ 50 ms sur toute la fenêtre
- Aucun message `triggering bus recovery` (sauf stress test volontaire)

- [ ] **Step 2 : Analyser le log**

```bash
grep "latency tick" /tmp/phase14-1h-bench.log | tail -20
grep -c "triggering bus recovery" /tmp/phase14-1h-bench.log
grep -c "tick OK" /tmp/phase14-1h-bench.log
```

Expected :
- ~18000 ticks (5 Hz × 3600 s)
- p99 stable autour d'une valeur unique
- 0 (ou ≤ 2) recovery triggers

- [ ] **Step 3 : Archiver le log**

```bash
cp /tmp/phase14-1h-bench.log docs/superpowers/validation/runs/$(date +%Y-%m-%d)-phase14-1h-bench.log
```

---

### Task 14.5 : Commit Phase 14

- [ ] **Step 1 : `git status` review**

Expected :
- `firmware-idf-v2/main/task_bmu_core.cpp` (nouveau)
- `firmware-idf-v2/main/task_bmu_core.h` (nouveau)
- `firmware-idf-v2/main/CMakeLists.txt` (SRCS ajouté)
- `firmware-idf-v2/main/main.cpp` (tick_loop → task_bmu_core_start)
- `firmware-idf-v2/components/bmu_i2c_glue/src/bmu_i2c_glue.c` (recover_bus + program_shunt_cal)
- `firmware-idf-v2/components/bmu_i2c_glue/include/bmu_i2c_glue.h` (program_shunt_cal decl)
- `docs/superpowers/validation/runs/YYYY-MM-DD-phase14-1h-bench.log` (nouveau)

- [ ] **Step 2 : Commit**

```bash
git add firmware-idf-v2/main/ \
        firmware-idf-v2/components/bmu_i2c_glue/ \
        docs/superpowers/validation/runs/
git commit -m "$(cat <<'EOF'
feat(phase-14): task_bmu_core pinned PRO_CPU + WDT

- task_bmu_core pinned core 0, prio 5, period 200 ms, 8 KiB stack
- WDT 3 s registered via esp_task_wdt_add
- Latency histogram p50/p99 logged every 50 ticks
- bmu_i2c_glue_program_shunt_cal: SHUNT_CAL=1249 (100 A 500 µΩ)
- bmu_i2c_glue_recover_bus: 9-clock bit-bang + STOP + rescan
- Retry policy: 3 consecutive fails trigger bus recovery
- 1h bench validated: 18k ticks, p99 under 50 ms, 0 reboot
EOF
)"
```

Subject `feat(phase-14): task_bmu_core pinned PRO_CPU + WDT` = 50 chars ✓.

---

## Acceptance Gates — Phase 11-14

Avant de passer aux Phases 15+, vérifier tous les points suivants :

- [ ] **Gate 1** — `idf.py build` passe clean dans `firmware-idf-v2/` sans warning.
- [ ] **Gate 2** — Flash size < 60 % budget OTA (`idf.py size` → flash code ≤ 1.2 MB).
- [ ] **Gate 3** — `libbmu_core.a` link OK avec corrosion, `heap_caps_malloc` résolu au link final.
- [ ] **Gate 4** — `bmu_core_tick` invoqué à 5 Hz depuis `task_bmu_core` sur PRO_CPU.
- [ ] **Gate 5** — `BmuRawInputs` rempli avec lectures I²C réelles (16 INA + 4 TCA + 1 AHT sur bench complet).
- [ ] **Gate 6** — Snapshot Rust affiche V/I cohérents avec archive v1 (delta ≤ 1 % V, ≤ 5 % I).
- [ ] **Gate 7** — Bench 1 h stable : aucun reboot WDT, heap free constant, p99 tick ≤ 50 ms.
- [ ] **Gate 8** — Bus recovery test manuel : court-circuit SDA 2 s → recovery triggered → reprise OK.
- [ ] **Gate 9** — Part 1 tests host toujours verts (`cargo test --workspace` dans `firmware-rust/`).
- [ ] **Gate 10** — 4 commits atomiques `feat(phase-11..14)` sur `feat/rust-hybrid-v2`, messages ≤ 50 chars.

Une fois ces 10 gates validées, le plan de continuation pour Phases 15-22 sera rédigé avec les
retours du bench (spécialement pour les ajustements de latence, la calibration des seuils, et la
stratégie de partage du bus I²C entre `task_bmu_core` et les futurs `task_climate` / `task_soh`).

---

## Handoff notes

**Dépendances externes à installer avant exécution :**
- ESP-IDF v5.4 (`source ~/esp/esp-idf/export.sh`)
- `espup` avec target `xtensa-esp32s3-none-elf` installé (déjà fait en Part 1)
- Accès bench hardware BOX-3 + ≥1 TCA9535 + ≥1 INA237 (Phase 13-14)

**Hors scope de ce plan** (reportés au plan Phases 15-22) :
- `bmu_climate` driver C (AHT30 est déjà lu par `bmu_i2c_glue`, mais le composant dédié + gestion des stats arrive en Phase 15)
- `bmu_soh` TFLite Micro wrapper + `fpnn_soh_v3_int8.tflite` integration
- `bmu_sd_log` line-protocol rolling + replay
- `bmu_wifi` / `bmu_mqtt` / provisioning
- LVGL 5 tabs (BATT / SOH / SYS / CLIMATE / CONFIG)
- BLE services (Battery / System / Config / Control / Victron)
- BLE pairing SC + HMAC (résout CRIT-D)
- Wi-Fi provisioning via BLE
- Victron SmartShunt emulation
- HIL TB01-TB13 Python harness + PDF report
- Bench 72 h stability
- Merge → `main` + rename `firmware-idf-v2` → `firmware-idf`

**Points de vigilance retrospectifs Part 1 à rappeler :**
- Tout `pub fn` returning value côté Rust : `#[must_use]` obligatoire (clippy pedantic).
- Doc comments backtick systématique (`clippy::doc_markdown`).
- Pas de `Copy` derive sur les containers d'état (cf. fix Part 1 sur `BatteryManager`, `RintEngine`, `BalancerEngine`).
- Tests module : `#[allow(clippy::unwrap_used, clippy::panic, clippy::field_reassign_with_default)]` fréquemment nécessaire.
- Commit hooks : subject ≤ 50 chars, body ≤ 72 chars. Heredoc obligatoire pour les multi-lignes.

---

**Fin du plan Part 2 Phases 11-14.** Prêt pour exécution subagent-driven ou inline selon préférence.
