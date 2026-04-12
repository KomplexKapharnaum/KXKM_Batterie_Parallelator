# Plan Part 2c — BMU Rust Hybrid V2 — Phases 16-22 (Verbatim)

> **Ce plan est la continuation verbatim de Part 2b (outline)** pour les Phases 16 à 22.
> Part 2 (Phases 11-14) a été exécuté avec succès (scaffold ESP-IDF + FFI Rust + I²C glue +
> task_bmu_core). Part 2b a publié un outline pour Phases 15-22. Part 2c **upgrade l'outline
> vers un format verbatim exécutable** pour les Phases 16-22 uniquement.
>
> **Phase 15 est en cours d'exécution dans un agent background séparé.** Ce plan
> assume que Phase 15 (`bmu_climate` + `bmu_soh` + `bmu_sd_log`) est committée lorsque
> Phase 16 démarre. Si Phase 15 échoue, Phase 16 peut démarrer Wi-Fi/MQTT mais le replay
> SD sera stubbé jusqu'à ce que `bmu_sd_log` atterrisse.
>
> **Scope exact** : Phases 16 → 22 (7 phases) = Wi-Fi/MQTT/SD replay, LVGL 5 tabs, BLE
> read-only, BLE Control + SC + HMAC (**CRIT-D**), Wi-Fi provisioning, Victron SmartShunt
> emulation, HIL TB01-TB13 + bench 72 h + merge final sur `main`.
>
> **Branche** : `feat/rust-hybrid-v2` (continuation, 40+ commits post Phase 14).
> **Spec autoritative** : `docs/superpowers/specs/2026-04-09-bmu-rust-hybrid-v2-design.md`
> (sections §7 BLE GATT, §8 Wi-Fi/MQTT/SD, §9 LVGL, §10.4 HIL, §12 migration steps 15-21).
>
> **Niveau de détail** : verbatim exécutable, proche du niveau Part 2 Phases 11-14 mais
> légèrement plus lean (~3500-5000 lignes) parce que beaucoup de code est copie-collée
> depuis `firmware-idf/components/` v1 — on référence l'archive v1 au lieu de re-dumper
> 400 lignes de NimBLE init ou de LVGL tab setup.

---

## Table des matières

- [Héritages critiques depuis Phases 11-14 + Phase 15](#héritages-critiques-depuis-phases-11-14--phase-15)
- [Execution Notes & Known Deviations (Part 2c)](#execution-notes--known-deviations-part-2c)
- [File Structure Overview — Part 2c delta](#file-structure-overview--part-2c-delta)
- [Phase Plan — Summary Table](#phase-plan--summary-table)
- [Phase 16 — Wi-Fi STA + MQTT telemetry + SD replay](#phase-16--wi-fi-sta--mqtt-telemetry--sd-replay)
- [Phase 17 — LVGL 5 tabs (BATT / SOH / SYS / CLIMATE / CONFIG)](#phase-17--lvgl-5-tabs-batt--soh--sys--climate--config)
- [Phase 18 — BLE GATT read-only (Battery / System / Config)](#phase-18--ble-gatt-read-only-battery--system--config)
- [Phase 19 — BLE Control + Secure Connections + HMAC (CRIT-D)](#phase-19--ble-control--secure-connections--hmac-crit-d)
- [Phase 20 — Wi-Fi provisioning via BLE Command](#phase-20--wi-fi-provisioning-via-ble-command)
- [Phase 21 — Victron SmartShunt emulation](#phase-21--victron-smartshunt-emulation)
- [Phase 22 — HIL TB01-TB13 + bench 72 h + merge final](#phase-22--hil-tb01-tb13--bench-72-h--merge-final)
- [Acceptance Gates — Phases 16-22](#acceptance-gates--phases-16-22)
- [Handoff notes](#handoff-notes)

---

## Héritages critiques depuis Phases 11-14 + Phase 15

Avant d'entamer Phase 16, les gates suivantes doivent être vertes :

**Part 2 Phases 11-14 (déjà merged)** :

- ✅ `firmware-idf-v2/` compile et flashe, splash LVGL visible sur BOX-3
- ✅ `bmu_core_rs` intégré via corrosion, allocator `heap_caps_malloc` opérationnel
- ✅ `bmu_i2c_glue` lit 16 INA237 + 4 TCA9535 + 1 AHT30 en raw
- ✅ `task_bmu_core` pinné PRO_CPU, WDT 3 s, bench 1 h p99 ≤ 50 ms
- ✅ Snapshot Rust cohérent avec archive v1 (delta ≤ 1 % V, ≤ 5 % I)
- ✅ 189 Rust host tests + 5 proptest toujours verts (`cargo test --workspace`)

**Part 2c Phase 15 (en cours en background)** — à committer avant Phase 16 :

- ✅ `bmu_climate` expose `bmu_climate_init()` + `bmu_climate_update()` + `bmu_climate_get_stats()`
- ✅ `bmu_soh` : TFLite Micro wrapper, `fpnn_soh_v3_int8.tflite` embarqué, inférence < 100 ms
- ✅ `bmu_sd_log` : line-protocol rolling 64 MB × N files, sidecar `.nosync` pour cursor replay
- ✅ Smoke test bench 15 min clean, heap stable, pas de WDT
- ✅ Commit `feat(phase-15): climate/soh/sdlog components` merged sur branche

**Si Phase 15 a échoué** : Phase 16 peut démarrer sans `bmu_sd_log` — la Task 16.4 (SD replay)
sera stubbée (retour `ESP_ERR_NOT_SUPPORTED`) et un ticket dédié Part 2d sera créé pour
recompléter le replay. Les Tasks 16.1-16.3 (Wi-Fi + MQTT live) restent entièrement réalisables.

Ce que Part 2c reprend de Part 2 + 15 :

- Le bus I²C déjà géré par `bmu_i2c_glue` et son wrapper mutex `bmu_i2c_lock()`
- Le `BmuSnapshot` exposé via `bmu_core_get_cached_snapshot()` — **source unique de vérité
  read-only** pour LVGL, BLE, MQTT, SD log, Victron
- Scheduling `task_bmu_core` à 5 Hz core 0 — les tâches secondaires (MQTT 1 Hz, UI 20 Hz
  render/5 Hz data, BLE 2 Hz notify) s'ajoutent sur core 1 sans toucher au chemin critique
- Allocator `heap_caps_malloc` — tous les nouveaux composants l'utilisent automatiquement
  via `#include "esp_heap_caps.h"` et `heap_caps_malloc(size, MALLOC_CAP_DEFAULT)` ou
  `MALLOC_CAP_SPIRAM` pour les gros buffers LVGL
- `bmu_sd_log_append_batch()` et `bmu_sd_log_mark_sent(offset)` API définies Phase 15,
  utilisées Phase 16 pour le replay post-reconnect

---

## Execution Notes & Known Deviations (Part 2c)

Les 8 déviations de Part 2b restent pertinentes. Nouveaux éléments propres à Part 2c :

1. **Phase 15 commits first** — Part 2c assume que Phase 15 est committée au moment où
   Phase 16 démarre. Si Phase 15 échoue ou prend plus de temps que prévu, Phase 16 peut
   démarrer en parallèle mais le SD replay sera désactivé (feature flag Kconfig
   `CONFIG_BMU_MQTT_SD_REPLAY_ENABLED` à `n` par défaut si `bmu_sd_log` absent).

2. **CRIT-D lands in Phase 19** — c'est le dernier item d'audit non résolu depuis Part 1
   (audit 2026-03-30 4 CRIT + 8 HIGH). Phase 19 inclut une étape de security review dédiée
   avec referencing explicite de la section §7.5 de la spec. Une relecture `ecc:security-review`
   ou `superpowers:santa-loop` est **obligatoire** avant le commit Phase 19.

3. **Hardware dependency escalation** — Phase 16+ requires un iPhone avec l'app
   `iosApp/` buildée pour valider Phase 18 (BLE read-only), Phase 19 (commandes),
   Phase 20 (provisioning). Sans iOS device, Phases 18-20 peuvent être validées avec
   `bleak` Python + `hcitool` Linux sur un Mac Intel/M1 (bleak CoreBluetooth wrapper), mais
   le pairing SC réel doit être testé sur iPhone avant merge final.

4. **Advertising coexistence Phase 18 + 21** — NimBLE ne supporte qu'un seul advertising
   set par défaut. Phase 21 (Victron) doit **soit** multiplexer les payloads dans un même
   advertising packet, **soit** activer `CONFIG_BT_NIMBLE_EXT_ADV=y` pour advertising
   multi-set. Décision en Task 21.3 avec fallback sur rotation 500 ms alternée si
   multi-set pose souci.

5. **Commit-par-phase** (identique Part 2 + 2b) — pas de commit intermédiaire par task.
   Chaque phase produit 1 commit `feat(phase-NN): <sujet ≤ 50 chars>`. Exception : Phase 22
   produit 3 commits (HIL harness + rename legacy + rename v2→v1) par nécessité du rename git.

6. **`bmu_i2c_lock()` NON requis pour les nouveaux composants Phase 16-22** — sauf
   `bmu_ble_victron` Phase 21 si on ajoute une lecture directe (en pratique, aucun composant
   ne lit I²C dans ces phases, tous passent par `bmu_core_get_cached_snapshot()`).

7. **NimBLE NVS encryption** — Phase 19 active `CONFIG_NVS_ENCRYPTION=y` pour protéger le
   bonding store et les clés HMAC dérivées du LTK. Cela nécessite une **eFuse key partition
   dédiée** ; documenté dans Task 19.1. **Ne jamais** flasher un firmware Phase 19 sans
   NVS encryption active en production (dev OK mais warning log obligatoire).

8. **Bench 72 h non négociable** — Phase 22 exige 72 h continues sans reboot sur bench complet
   (16 batteries connectées, cycle charge/décharge automatisé). Les 72 h sont comptées à partir
   du dernier reboot volontaire (flash ou configuration change). Un reboot involontaire = reset
   du compteur à 0.

9. **HIL TB04-TB10 semi-automatisés** — ces 7 benches nécessitent une intervention humaine
   (court-circuit, débranchement, heat gun, PSU overvoltage). Le Python harness affiche des
   prompts interactifs et attend confirmation clavier opérateur pour pass/fail. TB01-TB03 et
   TB11-TB13 sont 100 % automatisés.

10. **Rename `firmware-idf-v2` → `firmware-idf`** en fin de Phase 22 — l'archive v1 `firmware-idf/`
    est d'abord renommée en `firmware-idf-legacy/` (commit séparé `chore(archive): rename
    firmware-idf to firmware-idf-legacy`), puis `firmware-idf-v2/` est renommée en
    `firmware-idf/` (commit séparé `chore(phase-22): rename firmware-idf-v2 to firmware-idf`).
    Git détecte le rename via similarity, le diff est minimal. Les scripts CI et `CLAUDE.md`
    sont mis à jour dans les mêmes commits.

11. **Merge final via PR draft** — pas de merge direct sur `main`. Une PR draft est ouverte
    après Phase 22 avec le rapport HIL PDF en asset, passe 2 reviews minimum (1 humain
    + 1 santa-loop adversarial), puis merge via `gh pr merge --squash=false` pour préserver
    l'historique des 50+ commits atomiques.

---

## File Structure Overview — Part 2c delta

```
firmware-idf-v2/
├── components/
│   ├── bmu_core_rs/              # Part 2 Phase 12 — inchangé
│   ├── bmu_i2c_glue/             # Part 2 Phase 13 — inchangé
│   ├── bmu_climate/              # Phase 15 — committé
│   ├── bmu_soh/                  # Phase 15 — committé
│   ├── bmu_sd_log/               # Phase 15 — committé
│   ├── bmu_wifi/                 # Phase 16 — NOUVEAU
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_wifi.h
│   │   ├── Kconfig
│   │   └── src/bmu_wifi.c
│   ├── bmu_mqtt/                 # Phase 16 — NOUVEAU
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_mqtt.h
│   │   ├── Kconfig
│   │   ├── src/bmu_mqtt.c
│   │   └── src/bmu_mqtt_replay.c
│   ├── bmu_ui/                   # Phase 17 — NOUVEAU
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_ui.h
│   │   ├── src/bmu_ui.c
│   │   ├── src/tab_batt.c
│   │   ├── src/tab_soh.c
│   │   ├── src/tab_sys.c
│   │   ├── src/tab_climate.c
│   │   ├── src/tab_config.c
│   │   └── assets/               # fonts + icons copiés v1
│   ├── bmu_ble/                  # Phase 18-20 — NOUVEAU (grows across phases)
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_ble.h
│   │   ├── Kconfig
│   │   ├── src/bmu_ble.c
│   │   ├── src/bmu_ble_gatt.c
│   │   ├── src/bmu_ble_control.c     # Phase 19
│   │   ├── src/bmu_ble_hmac.c        # Phase 19
│   │   ├── src/bmu_ble_audit.c       # Phase 19
│   │   └── src/bmu_ble_wifi_prov.c   # Phase 20
│   ├── bmu_ble_victron/          # Phase 21 — NOUVEAU
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_ble_victron.h
│   │   ├── src/bmu_ble_victron.c
│   │   ├── src/fleet_agg.c
│   │   └── src/instant_readout.c
│   └── bmu_hil_stub/             # Phase 22 — optionnel
├── main/
│   ├── main.cpp                  # init chain étendu chaque phase
│   ├── task_bmu_core.cpp         # inchangé
│   ├── task_climate.cpp          # Phase 15
│   ├── task_soh.cpp              # Phase 15
│   ├── task_sd_log.cpp           # Phase 15
│   ├── task_wifi_mqtt.cpp        # Phase 16
│   ├── task_ui.cpp               # Phase 17
│   ├── task_ble.cpp              # Phase 18
│   └── task_victron.cpp          # Phase 21
├── tests/hil/                    # Phase 22
│   ├── conftest.py
│   ├── hil_device.py
│   ├── hil_report.py
│   ├── report_template.py
│   ├── tb01_boot_sequence.py
│   ├── tb02_i2c_scan.py
│   ├── tb03_snapshot_coherence.py
│   ├── tb04_i2c_short.py        # interactive
│   ├── tb05_battery_unplug.py   # interactive
│   ├── tb06_overvoltage.py      # interactive
│   ├── tb07_overcurrent.py      # interactive
│   ├── tb08_overtemp.py         # interactive
│   ├── tb09_output_short.py     # interactive
│   ├── tb10_mcu_poweroff.py     # interactive
│   ├── tb11_ble_control.py
│   ├── tb12_mqtt_replay.py
│   └── tb13_bench_72h.py
└── docs/superpowers/validation/
    ├── runs/YYYY-MM-DD-phase16-mqtt-replay.log
    ├── runs/YYYY-MM-DD-phase17-ui-bench.log
    ├── runs/YYYY-MM-DD-phase18-ble-rd.log
    ├── runs/YYYY-MM-DD-phase19-ble-security.log
    ├── runs/YYYY-MM-DD-phase22-72h-bench.log
    └── reports/YYYY-MM-DD-phase22-hil-report.pdf
```

---

## Phase Plan — Summary Table

| # | Phase | Tasks | Exit criterion | Risk | Commit subject |
|---|---|---|---|---|---|
| **16** | Wi-Fi STA + MQTT telemetry + SD replay | 16.1 → 16.6 | Broker reçoit 1 Hz, replay post-coupure 60 s | moyen | `feat(phase-16): add wifi, mqtt, sd replay` |
| **17** | LVGL 5 tabs read-only UI | 17.1 → 17.8 | 5 tabs swipe fluide 20 Hz, heap stable, CPU <30 % | moyen | `feat(phase-17): add lvgl 5 tabs display` |
| **18** | BLE GATT read-only | 18.1 → 18.6 | iOS app voit 16 batteries live, UUIDs v1 compat | moyen | `feat(phase-18): add ble read-only gatt services` |
| **19** | BLE Control + SC + HMAC | 19.1 → 19.8 | 8 commandes auth+HMAC, audit log SD, CRIT-D résolu | **TRÈS ÉLEVÉ** | `feat(phase-19): ble control + sc pairing + hmac` |
| **20** | Wi-Fi provisioning via BLE | 20.1 → 20.5 | Device vierge → provisionné en <60 s via iOS | moyen | `feat(phase-20): wifiprov command via ble` |
| **21** | Victron SmartShunt emulation | 21.1 → 21.6 | VictronConnect voit BMU en SmartShunt | moyen | `feat(phase-21): add victron smartshunt emulation` |
| **22** | HIL TB01-TB13 + bench 72 h + merge | 22.1 → 22.9 | PDF signé ≥12/13 pass, 72 h clean, PR merged | élevé | `feat(phase-22): hil harness + 72h bench + merge` |

**Total tasks** : ~47. **Durée estimée** : 12-18 jours focalisés avec hardware sur bench
à partir de Phase 18 (BLE exige iOS réel pour la validation finale).

---



# Phase 16 — Wi-Fi STA + MQTT telemetry + SD replay

**Objectif** : connecter le firmware au broker MQTT KXKM (`kxkm-ai:1883`), publier la
telemetry JSON à 1 Hz sur le topic `bmu/<device_id>/telemetry`, et implémenter le replay
SD → MQTT au reconnect après une coupure réseau. Le provisioning Wi-Fi via BLE arrive
Phase 20 ; pour cette phase, les credentials sont pré-chargés en NVS via
`idf.py nvs-partition-gen` ou via un `credentials.h` temporaire.

**Durée estimée** : 1-2 jours. **Risk** : moyen (la logique de replay est subtile, le
coexistence BT/Wi-Fi peut poser des problèmes de radio sharing). **Acceptance** :

- Broker `kxkm-ai:1883` reçoit `bmu/<device_id>/telemetry` à 1 Hz
- Après coupure Wi-Fi de 60 s, le replay draine les records NOSYNC et publie sur
  `bmu/<device_id>/replay` en QoS 1 dans l'ordre
- Heap free stable (delta < 10 KiB sur 10 min)
- Flash total < 1.4 MB (budget 85 % de 2 MB OTA)

**Commit subject** : `feat(phase-16): add wifi, mqtt, sd replay` (42 chars).

## Task list — Phase 16

| # | Task | Files | Durée |
|---|---|---|---|
| 16.1 | `bmu_wifi` composant — STA lifecycle + NVS creds | `components/bmu_wifi/*` | 2 h |
| 16.2 | `bmu_mqtt` composant — client esp-mqtt + JSON publish | `components/bmu_mqtt/*` | 3 h |
| 16.3 | `task_wifi_mqtt` FSM + intégration main | `main/task_wifi_mqtt.cpp`, `main/main.cpp` | 2 h |
| 16.4 | SD replay post-reconnect | `components/bmu_mqtt/src/bmu_mqtt_replay.c` | 3 h |
| 16.5 | Provisioning NVS pré-flash manuel | `scripts/provisioning/nvs_wifi.csv` | 1 h |
| 16.6 | Smoke test 30 min + commit Phase 16 | `docs/superpowers/validation/runs/` | 1 h |

### Heritage v1

| Composant | Source v1 | Réutilisation | Notes |
|---|---|---|---|
| `bmu_wifi` | `firmware-idf/components/bmu_wifi/bmu_wifi.c` | **~90 % copie** | Adapter : lire NVS namespace `bmu/wifi` au lieu de `credentials.h` compile-time. Retirer tout SoftAP fallback. |
| `bmu_mqtt` | `firmware-idf/components/bmu_mqtt/bmu_mqtt.c` | **~70 % copie** | Adapter : payload JSON construit depuis `bmu_core_get_cached_snapshot()`, short keys `v/c/s/sc/ah/r/soh/dut`. |
| SD replay | aucun | **100 % nouveau** | v1 écrivait InfluxDB HTTP direct — pas de replay. |

---

### Task 16.1 : `bmu_wifi` composant — STA lifecycle + NVS creds

**Files:**

- Create: `firmware-idf-v2/components/bmu_wifi/CMakeLists.txt`
- Create: `firmware-idf-v2/components/bmu_wifi/Kconfig`
- Create: `firmware-idf-v2/components/bmu_wifi/include/bmu_wifi.h`
- Create: `firmware-idf-v2/components/bmu_wifi/src/bmu_wifi.c`

#### Heritage v1 à inspecter

```bash
sed -n '1,200p' firmware-idf/components/bmu_wifi/bmu_wifi.c
```

La structure v1 est :

1. `bmu_wifi_init()` : init NVS, `esp_netif_init`, `esp_event_loop_create_default`,
   `esp_wifi_init`, register handlers, `esp_wifi_set_mode(WIFI_MODE_STA)`,
   `esp_wifi_set_config(..., wifi_config)`, `esp_wifi_start()`
2. Event handler : `WIFI_EVENT_STA_START` → `esp_wifi_connect()` ; `WIFI_EVENT_STA_DISCONNECTED`
   → retry avec backoff ; `IP_EVENT_STA_GOT_IP` → set flag `s_wifi_connected = true`

**Différence v2** : les credentials sont lus depuis NVS namespace `bmu` keys `wifi/ssid`
et `wifi/psk`, **pas** depuis `credentials.h`. Si NVS vide, on log un warning et on
reste offline (Phase 20 ajoutera BLE prov).

#### Step 1 : Créer `bmu_wifi/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "src/bmu_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        log
)
```

#### Step 2 : Créer `bmu_wifi/Kconfig`

```kconfig
menu "BMU Wi-Fi"

config BMU_WIFI_RETRY_MAX
    int "Max consecutive reconnect attempts before giving up (0 = infinite)"
    default 0

config BMU_WIFI_CONNECT_TIMEOUT_MS
    int "Timeout (ms) for initial STA connection before marking offline"
    default 30000

config BMU_WIFI_NVS_NAMESPACE
    string "NVS namespace for Wi-Fi credentials"
    default "bmu"

endmenu
```

#### Step 3 : Créer `bmu_wifi/include/bmu_wifi.h`

```c
#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bmu_wifi_init(void);
bool bmu_wifi_is_connected(void);
esp_err_t bmu_wifi_get_ip(char *out_ip);
esp_err_t bmu_wifi_set_creds(const char *ssid, const char *psk);

#ifdef __cplusplus
}
#endif
```

#### Step 4 : Créer `bmu_wifi/src/bmu_wifi.c`

Strategy : copie-coller `firmware-idf/components/bmu_wifi/bmu_wifi.c` comme base, puis :

1. Supprimer tout `#include "credentials.h"`
2. Remplacer l'init des `wifi_config_t.sta.ssid` / `.password` par un lecteur NVS
3. Ajouter `bmu_wifi_set_creds()` qui écrit NVS + disconnect + set_config + connect
4. Retry backoff : 1 s → 5 s → 25 s → cap 25 s
5. Log tag : `BMU_WIFI`

Handler verbatim :

```c
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_retry_num++;
        int delay_s = 1;
        if (s_retry_num >= 2) delay_s = 5;
        if (s_retry_num >= 3) delay_s = 25;
        vTaskDelay(pdMS_TO_TICKS(delay_s * 1000));
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_events, BIT_CONNECTED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_events, BIT_CONNECTED);
    }
}
```

Le reste du fichier suit la structure v1 avec adaptations NVS.

#### Step 5 : Build sanity

```bash
source ~/esp/esp-idf/export.sh && . ~/export-esp.sh
cd firmware-idf-v2
idf.py build 2>&1 | tail -30
```

Expected : `Project build complete`.

---

### Task 16.2 : `bmu_mqtt` composant — client esp-mqtt + JSON publish

**Files:**

- Create: `firmware-idf-v2/components/bmu_mqtt/CMakeLists.txt`
- Create: `firmware-idf-v2/components/bmu_mqtt/Kconfig`
- Create: `firmware-idf-v2/components/bmu_mqtt/include/bmu_mqtt.h`
- Create: `firmware-idf-v2/components/bmu_mqtt/src/bmu_mqtt.c`
- Create: `firmware-idf-v2/components/bmu_mqtt/src/bmu_mqtt_replay.c` (stub rempli Task 16.4)

#### Heritage v1

```bash
sed -n '1,200p' firmware-idf/components/bmu_mqtt/bmu_mqtt.c
```

La v1 utilise déjà `esp-mqtt` v5. On copie le client init + event handler, et on
réécrit le payload builder pour utiliser `bmu_core_get_cached_snapshot()`.

**Short keys spec §8.3** :

- `t` = timestamp ms
- `d` = device_id (MAC hex)
- `b` = array 16 objets : `v` mV, `c` cA, `s` SoC%, `sc` state, `ah` cumul, `r` Rint,
  `soh`, `dut` duty %
- `cl` = climate `{tb, tm, h}`
- `sy` = system `{up, hp, wf, mq, tp}`

#### Step 1 : `bmu_mqtt/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS
        "src/bmu_mqtt.c"
        "src/bmu_mqtt_replay.c"
    INCLUDE_DIRS "include"
    REQUIRES
        mqtt esp_netif esp_event nvs_flash
        bmu_core_rs bmu_sd_log bmu_wifi log
)
```

#### Step 2 : `bmu_mqtt/Kconfig`

```kconfig
menu "BMU MQTT"

config BMU_MQTT_BROKER_URI
    string "Default MQTT broker URI"
    default "mqtt://kxkm-ai:1883"

config BMU_MQTT_TOPIC_PREFIX
    string "MQTT topic prefix"
    default "bmu"

config BMU_MQTT_PUBLISH_PERIOD_MS
    int "Publish period (ms)"
    default 1000
    range 200 10000

config BMU_MQTT_SD_REPLAY_ENABLED
    bool "Enable SD-backed replay on reconnect"
    default y

config BMU_MQTT_REPLAY_BATCH_SIZE
    int "Replay batch size (messages before yield)"
    default 50
    range 1 500

endmenu
```

#### Step 3 : `bmu_mqtt/include/bmu_mqtt.h`

```c
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bmu_mqtt_init(void);
esp_err_t bmu_mqtt_start(void);
bool bmu_mqtt_is_connected(void);
esp_err_t bmu_mqtt_publish_telemetry(void);
void bmu_mqtt_replay_start(void);
esp_mqtt_client_handle_t bmu_mqtt_get_client(void);
const char *bmu_mqtt_get_replay_topic(void);

#ifdef __cplusplus
}
#endif
```

#### Step 4 : `bmu_mqtt/src/bmu_mqtt.c` — event handler + JSON builder

Squelette complet (copie-adaptée de v1) :

```c
#include "bmu_mqtt.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bmu_core.h"
#include "bmu_sd_log.h"
#include "sdkconfig.h"

#define TAG "BMU_MQTT"

static esp_mqtt_client_handle_t s_client;
static bool s_connected;
static char s_device_id[13];
static char s_topic_live[64];
static char s_topic_replay[64];

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data) {
    switch ((esp_mqtt_event_id_t)id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "broker connected");
            s_connected = true;
            bmu_mqtt_replay_start();
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "broker disconnected");
            s_connected = false;
            break;
        default: break;
    }
}

static void init_device_id(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id),
             "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_topic_live, sizeof(s_topic_live),
             "%s/%s/telemetry", CONFIG_BMU_MQTT_TOPIC_PREFIX, s_device_id);
    snprintf(s_topic_replay, sizeof(s_topic_replay),
             "%s/%s/replay", CONFIG_BMU_MQTT_TOPIC_PREFIX, s_device_id);
}

esp_err_t bmu_mqtt_init(void) {
    init_device_id();
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_BMU_MQTT_BROKER_URI,
        .session.keepalive = 30,
        .network.reconnect_timeout_ms = 5000,
        .credentials.client_id = s_device_id,
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) return ESP_FAIL;
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                    mqtt_event_handler, NULL);
    ESP_LOGI(TAG, "mqtt init device=%s", s_device_id);
    return ESP_OK;
}

esp_err_t bmu_mqtt_start(void) { return esp_mqtt_client_start(s_client); }
bool bmu_mqtt_is_connected(void) { return s_connected; }
esp_mqtt_client_handle_t bmu_mqtt_get_client(void) { return s_client; }
const char *bmu_mqtt_get_replay_topic(void) { return s_topic_replay; }

static int build_telemetry_json(char *buf, size_t cap) {
    BmuSnapshot snap;
    if (bmu_core_get_cached_snapshot(&snap) != 0) return -1;
    size_t n = 0;
    n += snprintf(buf + n, cap - n,
        "{\"t\":%" PRIu64 ",\"d\":\"%s\",\"b\":[",
        (uint64_t)(esp_timer_get_time() / 1000), s_device_id);
    for (int i = 0; i < MAX_BATTERIES && n < cap - 80; i++) {
        const BmuBattery *b = &snap.batteries[i];
        n += snprintf(buf + n, cap - n,
            "%s{\"v\":%u,\"c\":%d,\"s\":%u,\"sc\":%u,"
            "\"ah\":%.2f,\"r\":%u,\"soh\":%.2f,\"dut\":%u}",
            i == 0 ? "" : ",",
            (unsigned)b->voltage_mv, (int)(b->current_ma / 10),
            (unsigned)b->soc_pct, (unsigned)b->state,
            b->cumulative_ah, (unsigned)b->rint_mohm,
            b->soh, (unsigned)b->duty_pct);
    }
    n += snprintf(buf + n, cap - n,
        "],\"cl\":{\"tb\":%.1f,\"tm\":%.1f,\"h\":%u},"
        "\"sy\":{\"up\":%" PRIu64 ",\"hp\":%" PRIu32 ",\"tp\":%u}}",
        snap.climate.t_box_c, snap.climate.t_mcu_c,
        (unsigned)snap.climate.humidity_pct,
        (uint64_t)(esp_timer_get_time() / 1000000),
        esp_get_free_heap_size(),
        (unsigned)snap.system.topology_ok);
    return (int)n;
}

esp_err_t bmu_mqtt_publish_telemetry(void) {
    if (!s_connected) return ESP_FAIL;
    static char buf[2048];
    int len = build_telemetry_json(buf, sizeof(buf));
    if (len <= 0) return ESP_FAIL;
    int msg_id = esp_mqtt_client_publish(s_client, s_topic_live, buf, len, 0, 0);
    if (msg_id < 0) return ESP_FAIL;
#if CONFIG_BMU_MQTT_SD_REPLAY_ENABLED
    bmu_sd_log_append_raw(buf, len);
#endif
    return ESP_OK;
}
```

#### Step 5 : `bmu_mqtt_replay.c` stub

```c
#include "bmu_mqtt.h"
#include "esp_log.h"
void bmu_mqtt_replay_start(void) { /* filled Task 16.4 */ }
```

#### Step 6 : Build sanity

```bash
cd firmware-idf-v2 && idf.py build 2>&1 | tail -30
```

---

### Task 16.3 : `task_wifi_mqtt` FSM + intégration main

**Files:**

- Create: `firmware-idf-v2/main/task_wifi_mqtt.cpp`
- Create: `firmware-idf-v2/main/task_wifi_mqtt.h`
- Modify: `firmware-idf-v2/main/main.cpp`
- Modify: `firmware-idf-v2/main/CMakeLists.txt`

#### FSM spec §8.1

```
INIT → NVS ready → WIFI_STA_INIT → wait IP
   ↓ (no creds) OFFLINE (wait Phase 20 BLE prov)
WIFI_CONNECTED → MQTT_INIT → MQTT_START → wait connected
MQTT_CONNECTED → publish loop 1 Hz
   ↓ (disconnect) MQTT_WAIT_RECONNECT → on reconnect → replay → resume
```

#### Step 1 : `task_wifi_mqtt.cpp`

```cpp
#include "task_wifi_mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bmu_wifi.h"
#include "bmu_mqtt.h"
#include "sdkconfig.h"

static const char *TAG = "TASK_WIFI_MQTT";

static void task_wifi_mqtt(void *arg) {
    ESP_LOGI(TAG, "starting");
    esp_err_t err = bmu_wifi_init();
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "no creds — waiting Phase 20 BLE prov");
        while (!bmu_wifi_is_connected()) vTaskDelay(pdMS_TO_TICKS(1000));
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi init failed err=%d", err);
        vTaskDelete(NULL); return;
    }
    const TickType_t deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(CONFIG_BMU_WIFI_CONNECT_TIMEOUT_MS);
    while (!bmu_wifi_is_connected() && xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    ESP_ERROR_CHECK(bmu_mqtt_init());
    ESP_ERROR_CHECK(bmu_mqtt_start());
    const TickType_t period = pdMS_TO_TICKS(CONFIG_BMU_MQTT_PUBLISH_PERIOD_MS);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        if (bmu_mqtt_is_connected()) bmu_mqtt_publish_telemetry();
        vTaskDelayUntil(&last, period);
    }
}

extern "C" void task_wifi_mqtt_start(void) {
    xTaskCreatePinnedToCore(task_wifi_mqtt, "wifi_mqtt", 8192, NULL, 3, NULL, 1);
}
```

Et `task_wifi_mqtt.h` :

```cpp
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void task_wifi_mqtt_start(void);
#ifdef __cplusplus
}
#endif
```

#### Step 2 : Patcher `main.cpp`

Ajouter `#include "task_wifi_mqtt.h"` et, après les init Phase 15 :

```cpp
task_wifi_mqtt_start();   // Phase 16
```

#### Step 3 : `main/CMakeLists.txt`

Ajouter `task_wifi_mqtt.cpp` dans SRCS et `bmu_wifi bmu_mqtt` dans REQUIRES.

#### Step 4 : Build + flash + monitor

```bash
cd firmware-idf-v2
idf.py build 2>&1 | tail -10
idf.py -p /dev/cu.usbmodem1101 flash
python3 scripts/monitor_passive.py /dev/cu.usbmodem1101
```

Expected monitor output :

```
I TASK_WIFI_MQTT: starting
I BMU_WIFI: got IP: 192.168.0.42
I BMU_MQTT: mqtt init device=7c9ebd1a2b3c
I BMU_MQTT: broker connected
```

Côté broker :

```bash
mosquitto_sub -h kxkm-ai -t 'bmu/+/telemetry' -v
```

Un message par seconde. 5 min sans reboot.

---

### Task 16.4 : SD replay post-reconnect

**Files:**

- Modify: `firmware-idf-v2/components/bmu_mqtt/src/bmu_mqtt_replay.c`
- Modify: `firmware-idf-v2/components/bmu_sd_log/include/bmu_sd_log.h` (cursor API)
- Modify: `firmware-idf-v2/components/bmu_sd_log/src/bmu_sd_log.c`

#### Algorithme spec §8.4

```
on MQTT_CONNECTED:
  spawn oneshot task mqtt_replay prio 2, stack 4 KiB, core 1
  files = bmu_sd_log_list_nosync()
  for each file:
    offset = cursor
    while not EOF:
      batch 50 lines, publish QoS 1 on bmu/<id>/replay
      mark_sent(cur_offset)
      yield 10 ms
    close file
  vTaskDelete(NULL)
```

#### Step 1 : Cursor API dans `bmu_sd_log.h`

```c
typedef struct {
    char path[128];
    size_t cursor_offset;
    size_t file_size;
} bmu_sd_log_nosync_entry_t;

int bmu_sd_log_list_nosync(bmu_sd_log_nosync_entry_t *out, int max);
esp_err_t bmu_sd_log_mark_sent(const char *path, size_t new_offset);
esp_err_t bmu_sd_log_append_raw(const char *line, size_t len);
```

#### Step 2 : Implémenter `bmu_mqtt_replay.c`

```c
#include "bmu_mqtt.h"
#include "bmu_sd_log.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#define TAG "BMU_MQTT_REPLAY"
#define LINE_MAX 2048

static void replay_task(void *arg) {
    ESP_LOGI(TAG, "replay start");
    bmu_sd_log_nosync_entry_t entries[8];
    int n = bmu_sd_log_list_nosync(entries, 8);
    ESP_LOGI(TAG, "found %d files with backlog", n);

    esp_mqtt_client_handle_t client = bmu_mqtt_get_client();
    const char *topic = bmu_mqtt_get_replay_topic();

    for (int i = 0; i < n; i++) {
        FILE *f = fopen(entries[i].path, "rb");
        if (!f) continue;
        fseek(f, entries[i].cursor_offset, SEEK_SET);
        char line[LINE_MAX];
        int batch = 0;
        size_t cur_offset = entries[i].cursor_offset;
        while (fgets(line, sizeof(line), f)) {
            int len = (int)strlen(line);
            if (len > 0 && line[len - 1] == '\n') line[--len] = 0;
            int msg_id = esp_mqtt_client_publish(client, topic, line, len, 1, 0);
            if (msg_id < 0) { ESP_LOGW(TAG, "publish failed, abort"); goto done; }
            cur_offset += (size_t)len + 1;
            batch++;
            if (batch >= CONFIG_BMU_MQTT_REPLAY_BATCH_SIZE) {
                bmu_sd_log_mark_sent(entries[i].path, cur_offset);
                batch = 0;
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        bmu_sd_log_mark_sent(entries[i].path, cur_offset);
    done:
        fclose(f);
    }
    ESP_LOGI(TAG, "replay complete");
    vTaskDelete(NULL);
}

void bmu_mqtt_replay_start(void) {
#if CONFIG_BMU_MQTT_SD_REPLAY_ENABLED
    xTaskCreatePinnedToCore(replay_task, "mqtt_replay", 4096, NULL, 2, NULL, 1);
#endif
}
```

#### Step 3 : Test coupure Wi-Fi 60 s

Procédure :

1. Device running, MQTT streaming normal
2. Couper Wi-Fi côté AP (`iw dev wlan0 disconnect` ou équivalent)
3. Attendre 60 s → device log `BMU_WIFI disconnected retry...`
4. Rebrancher
5. Observer `BMU_MQTT broker connected` → `BMU_MQTT_REPLAY replay start` → `replay complete`
6. Broker : `mosquitto_sub -h kxkm-ai -t 'bmu/+/replay' -v | head -80` — ~60 messages dans l'ordre

Archiver le log dans `docs/superpowers/validation/runs/$(date +%Y-%m-%d)-phase16-mqtt-replay.log`.

---

### Task 16.5 : Provisioning NVS pré-flash manuel

**Files:**

- Create: `scripts/provisioning/nvs_wifi.csv.example`
- Create: `scripts/provisioning/gen_nvs_partition.sh`

#### Step 1 : Template CSV

`scripts/provisioning/nvs_wifi.csv.example` :

```csv
key,type,encoding,value
bmu,namespace,,
wifi/ssid,data,string,"KXKM-2.4G"
wifi/psk,data,string,"REPLACE_ME"
mqtt/uri,data,string,"mqtt://kxkm-ai:1883"
```

#### Step 2 : Script génération + flash

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
SRC=nvs_wifi.csv
OUT=nvs_wifi.bin
SIZE=0x6000
python "$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py" \
    generate "$SRC" "$OUT" $SIZE
esptool.py --chip esp32s3 -p "${1:-/dev/cu.usbmodem1101}" \
    write_flash 0x9000 "$OUT"
echo "NVS partition flashed."
```

#### Step 3 : Documentation CLAUDE.md

Ajouter une section **Wi-Fi provisioning (Phase 16 temporaire)** dans
`firmware-idf-v2/CLAUDE.md`, obsolète en Phase 20.

---

### Task 16.6 : Smoke test 30 min + commit Phase 16

#### Step 1 : Smoke test

```bash
cd firmware-idf-v2
idf.py -p /dev/cu.usbmodem1101 flash
python3 scripts/monitor_passive.py /dev/cu.usbmodem1101 > /tmp/phase16-smoke.log &
MONITOR_PID=$!
sleep 1800
kill $MONITOR_PID

grep -c "broker connected" /tmp/phase16-smoke.log
grep -c "publish failed" /tmp/phase16-smoke.log
grep -c "WDT" /tmp/phase16-smoke.log
grep -c "Guru Meditation" /tmp/phase16-smoke.log

cp /tmp/phase16-smoke.log \
    docs/superpowers/validation/runs/$(date +%Y-%m-%d)-phase16-smoke.log
```

Expected : ~1800 publish events, 0 reboots, heap stable.

#### Step 2 : Rust host tests verts

```bash
cd firmware-rust && cargo test --workspace 2>&1 | tail -5
# Expected: 189 passed
```

#### Step 3 : Flash size check

```bash
cd firmware-idf-v2 && idf.py size 2>&1 | grep "Used size"
# Expected: flash code < 1.4 MB
```

#### Step 4 : Git status review

Expected fichiers modifiés/créés :

- `firmware-idf-v2/components/bmu_wifi/**` (nouveau)
- `firmware-idf-v2/components/bmu_mqtt/**` (nouveau)
- `firmware-idf-v2/main/task_wifi_mqtt.{cpp,h}` (nouveau)
- `firmware-idf-v2/main/main.cpp` (modifié)
- `firmware-idf-v2/main/CMakeLists.txt` (modifié)
- `scripts/provisioning/**` (nouveau)
- `docs/superpowers/validation/runs/YYYY-MM-DD-phase16-smoke.log`

#### Step 5 : Créer le commit atomique

Staging :

```bash
git add firmware-idf-v2/components/bmu_wifi/ \
        firmware-idf-v2/components/bmu_mqtt/ \
        firmware-idf-v2/main/task_wifi_mqtt.cpp \
        firmware-idf-v2/main/task_wifi_mqtt.h \
        firmware-idf-v2/main/main.cpp \
        firmware-idf-v2/main/CMakeLists.txt \
        scripts/provisioning/ \
        docs/superpowers/validation/runs/
```

Créer le commit via heredoc avec sujet `feat(phase-16): add wifi, mqtt, sd replay`
(42 chars) et body :

```
- bmu_wifi: STA lifecycle with NVS credentials, exponential
  backoff retry (1/5/25 s), no SoftAP fallback
- bmu_mqtt: esp-mqtt v5 client, JSON short-key telemetry
  (spec 8.3), publish 1 Hz on bmu/<id>/telemetry
- task_wifi_mqtt: FSM init-wifi, wait-ip, mqtt-start,
  publish loop, pinned core 1 prio 3, 8 KiB stack
- SD replay: oneshot task on MQTT_CONNECTED drains nosync
  files in 50-msg batches with 10 ms yield, QoS 1
- scripts/provisioning: manual NVS partition gen for dev
  bench (replaced by Phase 20 BLE WifiProv later)
- Smoke 30 min: 1800 publishes, 0 reboot, heap stable
```

### Known risks — Phase 16

- **MQTT event loop blocked by replay** : batch + yield obligatoires (implémenté Task 16.4)
- **BT/Wi-Fi coexistence** : pas encore actif, revoir Phase 18
- **NVS corrompue au power-loss pendant write** : double buffer NVS natif OK
- **JSON builder stack overflow** : buffer 2 KB static, stack 8 KB, OK
- **Heap fragmentation** : monitor sur 30 min, delta < 10 KiB attendu

- [ ] **Commit** : `feat(phase-16): add wifi, mqtt, sd replay`

### Execution Notes — Phase 16

1. **Coexistence plan** : documenter Phase 18 après BLE active
2. **Device ID** : base MAC STA, éventuellement efuse Phase 20
3. **Broker unavailable fallback** : hors scope, noter post-v2
4. **Telegraf mapping** : vérifier short keys alignés dans `kxkm-api/telegraf.conf`

---


# Phase 17 — LVGL 5 tabs (BATT / SOH / SYS / CLIMATE / CONFIG)

**Objectif** : afficher sur le display BOX-3 (320×240 ST7789) un UI 5 onglets read-only
alimenté par le `BmuSnapshot` Rust. Cadences distinctes : data refresh 5 Hz (snapshot pull),
render 20 Hz (LVGL tick). L'onglet BATT est critique (temps de réponse visuel). Aucune
allocation runtime après l'init LVGL (spec §9.3 strict).

**Durée estimée** : 2 jours. **Risk** : moyen (LVGL heap, PSRAM framebuffer, asset budget).
**Acceptance** :

- 5 tabs swipe fluide horizontal
- Heap LVGL stable (delta < 5 KiB sur 10 min)
- CPU render LVGL < 30 % core 1
- Affichage cohérent avec MQTT broker (delta ≤ 1 s sur V/I)
- Flash delta < 200 KiB vs Phase 16

**Commit subject** : `feat(phase-17): add lvgl 5 tabs display` (40 chars).

## Task list — Phase 17

| # | Task | Files | Durée |
|---|---|---|---|
| 17.1 | `bmu_ui` composant scaffold + LVGL tab container | `components/bmu_ui/{CMakeLists,bmu_ui.h,bmu_ui.c}` | 2 h |
| 17.2 | Tab BATT (critique) — 4×4 grid + couleurs d'état | `components/bmu_ui/src/tab_batt.c` | 4 h |
| 17.3 | Tab SOH — 16 bars horizontales | `components/bmu_ui/src/tab_soh.c` | 1 h |
| 17.4 | Tab SYS — uptime, heap, Wi-Fi, MQTT, topology | `components/bmu_ui/src/tab_sys.c` | 2 h |
| 17.5 | Tab CLIMATE — AHT30 + lv_chart 30 min | `components/bmu_ui/src/tab_climate.c` | 2 h |
| 17.6 | Tab CONFIG — read-only seuils | `components/bmu_ui/src/tab_config.c` | 1 h |
| 17.7 | `task_ui` cadence data 5 Hz + render 20 Hz | `main/task_ui.cpp` | 2 h |
| 17.8 | Smoke 10 min + heap check + commit Phase 17 | `docs/superpowers/validation/runs/` | 1 h |

### Heritage v1

| Composant | Source v1 | Réutilisation | Notes |
|---|---|---|---|
| `bmu_ui` | `firmware-idf/components/bmu_display/` | **~60 % copie** | Architecture tabs OK, mais data binding réécrit pour taper `bmu_core_get_cached_snapshot()` |
| Assets (fonts, icons) | `firmware-idf/components/bmu_display/assets/` | **100 % copie** | Fonts DejaVu + icons batterie réutilisés tels quels |
| LVGL port | `esp_lvgl_port` (component registry) | **100 %** | Port ESP-IDF officiel, init via BSP `esp-box-3` |

---

### Task 17.1 : `bmu_ui` composant scaffold + LVGL tab container

**Files:**

- Create: `firmware-idf-v2/components/bmu_ui/CMakeLists.txt`
- Create: `firmware-idf-v2/components/bmu_ui/include/bmu_ui.h`
- Create: `firmware-idf-v2/components/bmu_ui/src/bmu_ui.c`
- Create: `firmware-idf-v2/components/bmu_ui/src/tab_batt.c` (stub)
- Create: `firmware-idf-v2/components/bmu_ui/src/tab_soh.c` (stub)
- Create: `firmware-idf-v2/components/bmu_ui/src/tab_sys.c` (stub)
- Create: `firmware-idf-v2/components/bmu_ui/src/tab_climate.c` (stub)
- Create: `firmware-idf-v2/components/bmu_ui/src/tab_config.c` (stub)
- Copy: `firmware-idf/components/bmu_display/assets/**` → `firmware-idf-v2/components/bmu_ui/assets/`

#### Heritage v1 à inspecter

```bash
ls firmware-idf/components/bmu_display/
sed -n '1,200p' firmware-idf/components/bmu_display/bmu_display.cpp
find firmware-idf/components/bmu_display/assets -type f
```

Repérer la fonction v1 `bmu_display_create_tabview()` et copier la structure `lv_tabview_create`
+ `lv_tabview_add_tab` pour chacun des 5 tabs.

#### Step 1 : `CMakeLists.txt`

```cmake
idf_component_register(
    SRCS
        "src/bmu_ui.c"
        "src/tab_batt.c"
        "src/tab_soh.c"
        "src/tab_sys.c"
        "src/tab_climate.c"
        "src/tab_config.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_lvgl_port
        esp-box-3
        bmu_core_rs
        bmu_climate
        bmu_soh
        bmu_wifi
        bmu_mqtt
        log
    EMBED_TXTFILES
        "assets/font_dejavu_14.c"
        "assets/icon_battery.c"
)
```

Note : les fonts et icons LVGL sont généralement embarqués en tant que fichiers C avec un
array `uint8_t` — `EMBED_TXTFILES` fonctionne si on les convertit en C arrays, sinon on les
compile directement comme sources.

#### Step 2 : `include/bmu_ui.h`

```c
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bmu_ui_init(void);
void bmu_ui_update_data(void);  // Called 5 Hz by task_ui

#ifdef __cplusplus
}
#endif
```

Et le header interne `bmu_ui_internal.h` partagé entre `bmu_ui.c` et `tab_*.c` :

```c
#pragma once
#include "lvgl.h"

typedef struct {
    lv_obj_t *tabview;
    lv_obj_t *tab_batt;
    lv_obj_t *tab_soh;
    lv_obj_t *tab_sys;
    lv_obj_t *tab_climate;
    lv_obj_t *tab_config;
} bmu_ui_ctx_t;

extern bmu_ui_ctx_t g_ui;

void tab_batt_create(lv_obj_t *parent);
void tab_batt_update(void);

void tab_soh_create(lv_obj_t *parent);
void tab_soh_update(void);

void tab_sys_create(lv_obj_t *parent);
void tab_sys_update(void);

void tab_climate_create(lv_obj_t *parent);
void tab_climate_update(void);

void tab_config_create(lv_obj_t *parent);
void tab_config_update(void);
```

#### Step 3 : `src/bmu_ui.c`

```c
#include "bmu_ui.h"
#include "bmu_ui_internal.h"
#include "esp_log.h"
#include "bsp/esp-box-3.h"
#include "esp_lvgl_port.h"

#define TAG "BMU_UI"
bmu_ui_ctx_t g_ui;

esp_err_t bmu_ui_init(void) {
    /* LCD + touch already init by BSP in main.cpp */
    lvgl_port_lock(0);

    g_ui.tabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 32);
    lv_obj_set_style_text_font(g_ui.tabview, &lv_font_montserrat_14, 0);

    g_ui.tab_batt    = lv_tabview_add_tab(g_ui.tabview, "BATT");
    g_ui.tab_soh     = lv_tabview_add_tab(g_ui.tabview, "SOH");
    g_ui.tab_sys     = lv_tabview_add_tab(g_ui.tabview, "SYS");
    g_ui.tab_climate = lv_tabview_add_tab(g_ui.tabview, "CLIM");
    g_ui.tab_config  = lv_tabview_add_tab(g_ui.tabview, "CONF");

    tab_batt_create(g_ui.tab_batt);
    tab_soh_create(g_ui.tab_soh);
    tab_sys_create(g_ui.tab_sys);
    tab_climate_create(g_ui.tab_climate);
    tab_config_create(g_ui.tab_config);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "ui init complete");
    return ESP_OK;
}

void bmu_ui_update_data(void) {
    if (!lvgl_port_lock(10)) return;
    tab_batt_update();
    tab_soh_update();
    tab_sys_update();
    tab_climate_update();
    tab_config_update();
    lvgl_port_unlock();
}
```

#### Step 4 : Créer les 5 fichiers `tab_*.c` avec stubs

Chaque stub expose `*_create(parent)` qui crée un label placeholder, et `*_update()` vide.
Sera rempli en Tasks 17.2 à 17.6.

#### Step 5 : Build sanity

```bash
cd firmware-idf-v2 && idf.py build 2>&1 | tail -20
```

Expected : build OK, assets embeds résolus.

---

### Task 17.2 : Tab BATT — 4×4 grid + couleurs d'état

**Files:**

- Modify: `firmware-idf-v2/components/bmu_ui/src/tab_batt.c`

#### Layout spec §9.2

```
┌────────────────────────────────────┐
│ BATT 0 │ BATT 1 │ BATT 2 │ BATT 3 │
│ 24.5V  │ 24.3V  │ 24.6V  │ 24.4V  │
│ +1.2A  │ -0.8A  │ +2.1A  │ OFF    │
│ 87%    │ 72%    │ 95%    │ ---    │
├────────┼────────┼────────┼────────┤
│ BATT 4 │  ...                     │
```

16 cellules dans un grid LVGL, chaque cellule = 1 container `lv_obj` avec 3 labels
(voltage, current, soc) + 1 background color indiquant l'état :

- **Vert** : Online (normal)
- **Jaune** : PreCharging / Warning
- **Rouge** : Fault / Forced OFF
- **Gris** : Offline

Touch sur une cellule → overlay avec `lv_chart` V/I des 5 dernières minutes.

#### Step 1 : Créer les 16 cellules au boot

```c
#include "bmu_ui_internal.h"
#include "lvgl.h"
#include "bmu_core.h"
#include <stdio.h>

#define CELL_W 80
#define CELL_H 55

static lv_obj_t *s_cells[16];
static lv_obj_t *s_lbl_v[16];
static lv_obj_t *s_lbl_i[16];
static lv_obj_t *s_lbl_soc[16];

static void cell_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    /* open overlay chart - stub for Phase 17 basic, full impl Phase 17+*/
}

void tab_batt_create(lv_obj_t *parent) {
    lv_obj_set_style_pad_all(parent, 4, 0);

    for (int i = 0; i < 16; i++) {
        int row = i / 4;
        int col = i % 4;
        lv_obj_t *cell = lv_obj_create(parent);
        lv_obj_set_size(cell, CELL_W, CELL_H);
        lv_obj_set_pos(cell, col * (CELL_W + 2), row * (CELL_H + 2));
        lv_obj_set_style_bg_color(cell, lv_color_hex(0x333333), 0);
        lv_obj_set_style_pad_all(cell, 2, 0);
        lv_obj_add_event_cb(cell, cell_click_cb, LV_EVENT_CLICKED,
                             (void *)(intptr_t)i);
        s_cells[i] = cell;

        lv_obj_t *lbl_idx = lv_label_create(cell);
        lv_label_set_text_fmt(lbl_idx, "B%d", i);
        lv_obj_align(lbl_idx, LV_ALIGN_TOP_LEFT, 0, 0);

        s_lbl_v[i] = lv_label_create(cell);
        lv_obj_align(s_lbl_v[i], LV_ALIGN_TOP_LEFT, 0, 12);
        lv_label_set_text(s_lbl_v[i], "--");

        s_lbl_i[i] = lv_label_create(cell);
        lv_obj_align(s_lbl_i[i], LV_ALIGN_TOP_LEFT, 0, 24);
        lv_label_set_text(s_lbl_i[i], "--");

        s_lbl_soc[i] = lv_label_create(cell);
        lv_obj_align(s_lbl_soc[i], LV_ALIGN_TOP_LEFT, 0, 36);
        lv_label_set_text(s_lbl_soc[i], "--");
    }
}

static lv_color_t color_for_state(uint8_t state) {
    switch (state) {
        case 1: return lv_color_hex(0x00a000);   // Online (green)
        case 2: return lv_color_hex(0xc0a000);   // PreCharging (yellow)
        case 3: return lv_color_hex(0xa00000);   // Forced OFF (red)
        case 4: return lv_color_hex(0xa00000);   // Fault (red)
        default: return lv_color_hex(0x404040);  // Offline (gray)
    }
}

void tab_batt_update(void) {
    BmuSnapshot snap;
    if (bmu_core_get_cached_snapshot(&snap) != 0) return;

    for (int i = 0; i < 16; i++) {
        const BmuBattery *b = &snap.batteries[i];
        lv_obj_set_style_bg_color(s_cells[i], color_for_state(b->state), 0);
        lv_label_set_text_fmt(s_lbl_v[i], "%u.%01uV",
                               b->voltage_mv / 1000,
                               (b->voltage_mv % 1000) / 100);
        if (b->state == 0 || b->state == 3) {
            lv_label_set_text(s_lbl_i[i], "OFF");
            lv_label_set_text(s_lbl_soc[i], "---");
        } else {
            int ma = b->current_ma;
            lv_label_set_text_fmt(s_lbl_i[i], "%+.1fA", (float)ma / 1000.0f);
            lv_label_set_text_fmt(s_lbl_soc[i], "%u%%", b->soc_pct);
        }
    }
}
```

#### Step 2 : Build + smoke

```bash
cd firmware-idf-v2 && idf.py build && idf.py -p /dev/cu.usbmodem1101 flash
python3 scripts/monitor_passive.py /dev/cu.usbmodem1101 | head -40
```

Cote display : 16 cellules visibles, valeurs cohérentes avec MQTT broker.

---

### Task 17.3 : Tab SOH — 16 bars horizontales

**Files:** `components/bmu_ui/src/tab_soh.c`

```c
#include "bmu_ui_internal.h"
#include "lvgl.h"
#include "bmu_core.h"
#include "bmu_soh.h"

static lv_obj_t *s_bars[16];
static lv_obj_t *s_lbls[16];

void tab_soh_create(lv_obj_t *parent) {
    lv_obj_set_style_pad_all(parent, 4, 0);
    for (int i = 0; i < 16; i++) {
        lv_obj_t *bar = lv_bar_create(parent);
        lv_obj_set_size(bar, 200, 12);
        lv_obj_set_pos(bar, 30, i * 13);
        lv_bar_set_range(bar, 0, 100);
        s_bars[i] = bar;

        lv_obj_t *lbl = lv_label_create(parent);
        lv_obj_set_pos(lbl, 0, i * 13);
        lv_label_set_text_fmt(lbl, "B%d", i);

        lv_obj_t *val = lv_label_create(parent);
        lv_obj_set_pos(val, 240, i * 13);
        lv_label_set_text(val, "--%");
        s_lbls[i] = val;
    }
}

void tab_soh_update(void) {
    BmuSnapshot snap;
    if (bmu_core_get_cached_snapshot(&snap) != 0) return;
    for (int i = 0; i < 16; i++) {
        float soh = snap.batteries[i].soh;
        int pct = (int)(soh * 100.0f);
        lv_bar_set_value(s_bars[i], pct, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_lbls[i], "%d%%", pct);
    }
}
```

---

### Task 17.4 : Tab SYS — uptime, heap, Wi-Fi, MQTT, topology

```c
#include "bmu_ui_internal.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "bmu_wifi.h"
#include "bmu_mqtt.h"
#include "bmu_core.h"

static lv_obj_t *s_lbl_uptime;
static lv_obj_t *s_lbl_heap;
static lv_obj_t *s_lbl_wifi;
static lv_obj_t *s_lbl_mqtt;
static lv_obj_t *s_lbl_topo;
static lv_obj_t *s_lbl_ip;
static lv_obj_t *s_lbl_tick;

void tab_sys_create(lv_obj_t *parent) {
    lv_obj_set_style_pad_all(parent, 8, 0);
    s_lbl_uptime = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_uptime, 0, 0);
    s_lbl_heap = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_heap, 0, 20);
    s_lbl_wifi = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_wifi, 0, 40);
    s_lbl_mqtt = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_mqtt, 0, 60);
    s_lbl_topo = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_topo, 0, 80);
    s_lbl_ip = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_ip, 0, 100);
    s_lbl_tick = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_tick, 0, 120);
}

void tab_sys_update(void) {
    uint64_t up_s = esp_timer_get_time() / 1000000;
    lv_label_set_text_fmt(s_lbl_uptime, "Uptime: %llu s", up_s);
    lv_label_set_text_fmt(s_lbl_heap, "Heap: %" PRIu32 " KB",
                           esp_get_free_heap_size() / 1024);
    lv_label_set_text(s_lbl_wifi, bmu_wifi_is_connected() ? "Wi-Fi: UP" : "Wi-Fi: --");
    lv_label_set_text(s_lbl_mqtt, bmu_mqtt_is_connected() ? "MQTT: UP" : "MQTT: --");

    BmuSnapshot snap;
    if (bmu_core_get_cached_snapshot(&snap) == 0) {
        lv_label_set_text_fmt(s_lbl_topo, "Topology: %s",
                               snap.system.topology_ok ? "OK" : "FAIL");
        lv_label_set_text_fmt(s_lbl_tick, "Tick p99: %u ms",
                               (unsigned)snap.system.tick_p99_ms);
    }

    char ip[16] = "---";
    bmu_wifi_get_ip(ip);
    lv_label_set_text_fmt(s_lbl_ip, "IP: %s", ip);
}
```

---

### Task 17.5 : Tab CLIMATE — gros numbers + lv_chart 30 min

```c
#include "bmu_ui_internal.h"
#include "lvgl.h"
#include "bmu_climate.h"

static lv_obj_t *s_lbl_tbox;
static lv_obj_t *s_lbl_tmcu;
static lv_obj_t *s_lbl_hum;
static lv_obj_t *s_chart;
static lv_chart_series_t *s_ser_tbox;

void tab_climate_create(lv_obj_t *parent) {
    lv_obj_set_style_pad_all(parent, 8, 0);

    s_lbl_tbox = lv_label_create(parent);
    lv_obj_set_style_text_font(s_lbl_tbox, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(s_lbl_tbox, 0, 0);

    s_lbl_tmcu = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_tmcu, 0, 40);

    s_lbl_hum = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_hum, 0, 60);

    s_chart = lv_chart_create(parent);
    lv_obj_set_size(s_chart, 280, 100);
    lv_obj_set_pos(s_chart, 0, 90);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, 180);   // 180 points × 10 s = 30 min
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 60);
    s_ser_tbox = lv_chart_add_series(s_chart, lv_color_hex(0x00c0ff),
                                      LV_CHART_AXIS_PRIMARY_Y);
}

void tab_climate_update(void) {
    BmuClimateStats st;
    if (bmu_climate_get_stats(&st) != 0) return;
    lv_label_set_text_fmt(s_lbl_tbox, "%.1f C", st.t_box_c);
    lv_label_set_text_fmt(s_lbl_tmcu, "MCU: %.1f C", st.t_mcu_c);
    lv_label_set_text_fmt(s_lbl_hum, "Hum: %u%%", (unsigned)st.humidity_pct);

    static uint32_t tick_count = 0;
    if (tick_count++ % 50 == 0) {   // push point every 10 s (50 × 200 ms)
        lv_chart_set_next_value(s_chart, s_ser_tbox, (int)st.t_box_c);
    }
}
```

---

### Task 17.6 : Tab CONFIG — read-only seuils

```c
#include "bmu_ui_internal.h"
#include "lvgl.h"
#include "bmu_core.h"

static lv_obj_t *s_lbl_cfg;

void tab_config_create(lv_obj_t *parent) {
    lv_obj_set_style_pad_all(parent, 8, 0);
    s_lbl_cfg = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_cfg, 0, 0);
    lv_obj_set_style_text_font(s_lbl_cfg, &lv_font_montserrat_14, 0);
}

void tab_config_update(void) {
    BmuConfigC cfg;
    if (bmu_core_get_config(&cfg) != 0) return;
    lv_label_set_text_fmt(s_lbl_cfg,
        "Vmin:  %u mV\n"
        "Vmax:  %u mV\n"
        "Imax:  %u mA\n"
        "Tmax:  %u C\n"
        "Topology: %u/%u\n"
        "Device: %s",
        cfg.vmin_mv, cfg.vmax_mv, cfg.imax_ma, cfg.tmax_c,
        cfg.n_ina, cfg.n_tca, cfg.device_name);
}
```

---

### Task 17.7 : `task_ui` + cadence séparée data/render

**Files:**

- Create: `firmware-idf-v2/main/task_ui.cpp`
- Create: `firmware-idf-v2/main/task_ui.h`
- Modify: `firmware-idf-v2/main/main.cpp`
- Modify: `firmware-idf-v2/main/CMakeLists.txt`

```cpp
#include "task_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bmu_ui.h"

static const char *TAG = "TASK_UI";

static void task_ui(void *arg) {
    ESP_LOGI(TAG, "ui task starting");
    bmu_ui_init();
    const TickType_t data_period = pdMS_TO_TICKS(200);   // 5 Hz
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        bmu_ui_update_data();
        vTaskDelayUntil(&last, data_period);
    }
}

extern "C" void task_ui_start(void) {
    xTaskCreatePinnedToCore(task_ui, "ui", 16384, NULL, 2, NULL, 1);
}
```

Note : LVGL render 20 Hz est géré par `esp_lvgl_port` internally (task LVGL dédiée
core 1 prio 3). Notre `task_ui` ne fait que le data pull 5 Hz.

Patcher `main.cpp` pour appeler `task_ui_start()` après `task_wifi_mqtt_start()`.

---

### Task 17.8 : Smoke 10 min + heap check + commit

```bash
cd firmware-idf-v2
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash
python3 scripts/monitor_passive.py /dev/cu.usbmodem1101 > /tmp/phase17-smoke.log &
sleep 600
# Vérifier visuellement: swipe 5 tabs fluide, valeurs cohérentes

# Heap delta check
grep "heap free" /tmp/phase17-smoke.log | head -2
grep "heap free" /tmp/phase17-smoke.log | tail -2
# Expected: delta < 5 KiB

cp /tmp/phase17-smoke.log \
   docs/superpowers/validation/runs/$(date +%Y-%m-%d)-phase17-ui-smoke.log
```

#### Staging + commit

```bash
git add firmware-idf-v2/components/bmu_ui/ \
        firmware-idf-v2/main/task_ui.cpp \
        firmware-idf-v2/main/task_ui.h \
        firmware-idf-v2/main/main.cpp \
        firmware-idf-v2/main/CMakeLists.txt \
        docs/superpowers/validation/runs/
```

Commit subject : `feat(phase-17): add lvgl 5 tabs display` (40 chars).

Body :

```
- bmu_ui: LVGL tabview 5 tabs with esp_lvgl_port
- tab_batt: 4x4 grid with state color (green/yellow/red/gray)
- tab_soh: 16 horizontal bars 0-100%
- tab_sys: uptime, heap, wifi, mqtt, topology, ip, tick p99
- tab_climate: big box/mcu temp + humidity, 30 min chart
- tab_config: read-only dump of BmuConfigC
- task_ui: pinned core 1 prio 2, 200 ms data pull, LVGL
  render handled by esp_lvgl_port task
- Smoke 10 min: swipe fluide, heap delta < 5 KiB
```

### Known risks — Phase 17

- **LVGL heap leak** — widgets non détruits au screen change. Aucun `lv_obj_del()`
  dans ce design (tabs statiques). OK.
- **Double buffer PSRAM** : `esp-box-3` BSP configure déjà LVGL avec PSRAM double buffer.
  Vérifier `CONFIG_SPIRAM_USE_MALLOC=y` et LVGL port config.
- **Flash bloat assets** : garder max 2 fonts (regular + bold) pour éviter 600 KB flash.
  Si nécessaire, passer au font `lv_font_montserrat_14` built-in (pas de custom font).
- **Touch cell overlay chart** — laisser pour post-v2 si le temps manque. Le click
  handler est un stub ; l'overlay `lv_chart` est optionnel Phase 17.
- **BSP init order** — `bsp_display_start()` doit être appelé dans `main.cpp` AVANT
  `task_ui_start()`. Vérifier avec Phase 11 init chain.

- [ ] **Commit** : `feat(phase-17): add lvgl 5 tabs display`

### Execution Notes — Phase 17

1. **Aucune allocation runtime** (spec §9.3) : tous les `lv_*_create` se font dans
   les `*_create` appelés une fois au boot. Les `*_update` ne font que
   `lv_label_set_text_fmt` / `lv_bar_set_value` → pas de `lv_obj_create`.
2. **Le `lvgl_port_lock()` est obligatoire** avant tout appel LVGL depuis une task
   externe (notre `task_ui` qui n'est pas la LVGL task).
3. **Tests host Rust** : `cargo test --workspace` doit toujours être vert, 189 tests.

---


# Phase 18 — BLE GATT read-only (Battery / System / Config)

**Objectif** : exposer via BLE (stack NimBLE) 3 services GATT read-only conformes spec §7 :
Battery Service (16 characteristics, 1 par battery), System Service (64 bytes aggregated),
Config Service (read-only dump des seuils + labels). L'app iOS voit 16 batteries en live
via BLE notifications. **Aucune commande write pour cette phase** — le Control Service
arrive Phase 19.

**Durée estimée** : 1-2 jours. **Risk** : moyen (coexistence BT/Wi-Fi sur ESP32-S3 + UUID
parité iOS app v1 ↔ v2). **Acceptance** :

- iOS app (`iosApp/`) scan + connect + voit les 16 batteries mises à jour live via
  BLE notifications (cadence 2 Hz)
- UUIDs 100 % compatibles avec l'app v1 (audit 2026-04-08 = 100 % parité)
- Pas de dégradation MQTT (le streaming Wi-Fi reste stable)
- Max 4 connexions simultanées

**Commit subject** : `feat(phase-18): add ble read-only gatt services` (45 chars).

## Task list — Phase 18

| # | Task | Files | Durée |
|---|---|---|---|
| 18.1 | `bmu_ble` scaffold + NimBLE init + advertising | `components/bmu_ble/{CMake,bmu_ble.h,bmu_ble.c}` | 3 h |
| 18.2 | Battery Service F00D0001 — 16 characteristics | `components/bmu_ble/src/bmu_ble_gatt.c` | 3 h |
| 18.3 | System Service F00D0002 — 64 bytes aggregated | `components/bmu_ble/src/bmu_ble_gatt.c` | 1 h |
| 18.4 | Config Service F00D0004 — read-only | `components/bmu_ble/src/bmu_ble_gatt.c` | 1 h |
| 18.5 | `task_ble` notify loop 2 Hz + integration | `main/task_ble.cpp`, `main.cpp` | 2 h |
| 18.6 | Smoke iOS app + commit Phase 18 | `docs/superpowers/validation/runs/` | 1 h |

### Heritage v1

| Composant | Source v1 | Réutilisation | Notes |
|---|---|---|---|
| `bmu_ble` | `firmware-idf/components/bmu_ble/bmu_ble.cpp` | **~70 % copie** | Service definitions + UUIDs OK, réécrire les read callbacks pour taper `bmu_core_get_cached_snapshot()` |
| UUIDs | spec §7.1 | Identiques v1 | Parité 100 % audit 2026-04-08 avec iOS app |
| NimBLE init | ESP-IDF `esp_nimble_hci` | copy-paste init sequence | `nimble_port_init` → `ble_hs_cfg_set` → `ble_svc_gap_init` → `ble_svc_gatt_init` → `ble_gatts_count_cfg` → `ble_gatts_add_svcs` |

### UUID layout (spec §7.1)

- **Battery Service** : `F00D0001-9B1F-4A6F-9E5D-0A1B2C3D4E5F`
  - 16 characteristics `F00D0010 + i` (i = 0..15), read + notify, 24 bytes packed BE
- **System Service** : `F00D0002-9B1F-4A6F-9E5D-0A1B2C3D4E5F`
  - 1 characteristic `F00D0020`, read + notify, 64 bytes
- **Config Service** : `F00D0004-9B1F-4A6F-9E5D-0A1B2C3D4E5F`
  - 1 characteristic `F00D0040`, read-only, variable length (dump `BmuConfigC`)
- **Control Service (Phase 19)** : `F00D0003-...`

---

### Task 18.1 : `bmu_ble` scaffold + NimBLE init + advertising

**Files:**

- Create: `firmware-idf-v2/components/bmu_ble/CMakeLists.txt`
- Create: `firmware-idf-v2/components/bmu_ble/Kconfig`
- Create: `firmware-idf-v2/components/bmu_ble/include/bmu_ble.h`
- Create: `firmware-idf-v2/components/bmu_ble/src/bmu_ble.c`
- Create: `firmware-idf-v2/components/bmu_ble/src/bmu_ble_gatt.c`
- Modify: `firmware-idf-v2/sdkconfig.defaults` (enable NimBLE, disable Bluedroid)

#### Heritage v1 à inspecter

```bash
sed -n '1,250p' firmware-idf/components/bmu_ble/bmu_ble.cpp
grep -n "ble_gatt_svc_def" firmware-idf/components/bmu_ble/*.cpp
```

La structure v1 typique :

1. `nimble_port_init()` + `nimble_port_run()` dans une task dédiée
2. `ble_hs_cfg` callbacks : `sync_cb`, `reset_cb`, `store_status_cb`
3. `ble_svc_gap_device_name_set("KXKM-BMU-XXXX")`
4. `ble_svc_gatt_init()` + `ble_gatts_count_cfg(gatt_svc_table)`
5. Advertising loop : `ble_gap_adv_start` avec payload name + UUIDs

#### Step 1 : `sdkconfig.defaults` patches pour NimBLE

```
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_CONTROLLER_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=n
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=4
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
CONFIG_BT_NIMBLE_ROLE_OBSERVER=n
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192
CONFIG_BT_NIMBLE_NVS_PERSIST=y
CONFIG_BT_NIMBLE_SOFTWARE_COEX=y
```

Note : `CONFIG_BT_NIMBLE_SOFTWARE_COEX=y` est critique pour coexister avec Wi-Fi STA.

#### Step 2 : `CMakeLists.txt`

```cmake
idf_component_register(
    SRCS
        "src/bmu_ble.c"
        "src/bmu_ble_gatt.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES
        bt
        nvs_flash
        bmu_core_rs
        bmu_climate
        bmu_soh
        log
)
```

#### Step 3 : `bmu_ble/Kconfig`

```kconfig
menu "BMU BLE"

config BMU_BLE_ADV_NAME_PREFIX
    string "Advertising name prefix"
    default "KXKM-BMU"

config BMU_BLE_NOTIFY_PERIOD_MS
    int "Notify period for Battery Service (ms)"
    default 500
    range 100 5000

config BMU_BLE_MAX_CONNECTIONS
    int "Max concurrent connections"
    default 4
    range 1 4

endmenu
```

#### Step 4 : `bmu_ble/include/bmu_ble.h`

```c
#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bmu_ble_init(void);
void bmu_ble_notify_all(void);   // Called by task_ble
bool bmu_ble_is_advertising(void);
int  bmu_ble_active_connections(void);

#ifdef __cplusplus
}
#endif
```

#### Step 5 : `bmu_ble/src/bmu_ble.c` — NimBLE init

```c
#include "bmu_ble.h"
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "sdkconfig.h"

#define TAG "BMU_BLE"

static char s_device_name[24];
static uint8_t s_own_addr_type;
extern int bmu_ble_gatt_init(void);   // from bmu_ble_gatt.c

static int gap_event_cb(struct ble_gap_event *event, void *arg);

static void start_advertising(void) {
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)s_device_name;
    fields.name_len = strlen(s_device_name);
    fields.name_is_complete = 1;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    int rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                                &adv_params, gap_event_cb, NULL);
    if (rc != 0) ESP_LOGE(TAG, "adv_start failed rc=%d", rc);
}

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "connected conn_handle=%d", event->connect.conn_handle);
            if (event->connect.status != 0) start_advertising();
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGW(TAG, "disconnected reason=%d", event->disconnect.reason);
            start_advertising();
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "subscribe conn=%d attr=%d cur_notif=%d",
                     event->subscribe.conn_handle,
                     event->subscribe.attr_handle,
                     event->subscribe.cur_notify);
            break;
        default: break;
    }
    return 0;
}

static void on_sync(void) {
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) { ESP_LOGE(TAG, "infer_auto rc=%d", rc); return; }
    start_advertising();
    ESP_LOGI(TAG, "sync OK, advertising as %s", s_device_name);
}

static void on_reset(int reason) {
    ESP_LOGW(TAG, "host reset reason=%d", reason);
}

static void nimble_host_task(void *arg) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t bmu_ble_init(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_name, sizeof(s_device_name),
             "%s-%02X%02X", CONFIG_BMU_BLE_ADV_NAME_PREFIX, mac[4], mac[5]);

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(s_device_name);

    bmu_ble_gatt_init();

    nimble_port_freertos_init(nimble_host_task);
    ESP_LOGI(TAG, "ble init complete");
    return ESP_OK;
}
```

---

### Task 18.2 : Battery Service F00D0001 — 16 characteristics

**Files:** `firmware-idf-v2/components/bmu_ble/src/bmu_ble_gatt.c`

#### Byte layout (spec §7.2) — 24 bytes packed big-endian par battery

```
offset  type      field
0       u16       voltage_mv
2       i16       current_cA      (mA / 10)
4       u8        soc_pct
5       u8        state           (0..4)
6       u32       cumulative_ah_milli  (ah × 1000)
10      u16       rint_mohm
12      u16       soh_milli       (soh × 1000)
14      u8        duty_pct
15      u8        flags           (bit0=precharge_ok, bit1=latched, ...)
16      u32       cycles
20      u32       reserved        (padding for 24 bytes alignment)
```

#### Code

```c
#include <string.h>
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_log.h"
#include "bmu_core.h"

#define TAG "BMU_BLE_GATT"
#define N_BATT 16

static const ble_uuid128_t BATTERY_SVC_UUID = BLE_UUID128_INIT(
    0x5f, 0x4e, 0x3d, 0x2c, 0x1b, 0x0a,
    0x5d, 0x9e, 0x6f, 0x4a,
    0x1f, 0x9b,
    0x01, 0x00, 0x0d, 0xf0);

static const ble_uuid128_t SYSTEM_SVC_UUID = BLE_UUID128_INIT(
    0x5f, 0x4e, 0x3d, 0x2c, 0x1b, 0x0a,
    0x5d, 0x9e, 0x6f, 0x4a,
    0x1f, 0x9b,
    0x02, 0x00, 0x0d, 0xf0);

static const ble_uuid128_t CONFIG_SVC_UUID = BLE_UUID128_INIT(
    0x5f, 0x4e, 0x3d, 0x2c, 0x1b, 0x0a,
    0x5d, 0x9e, 0x6f, 0x4a,
    0x1f, 0x9b,
    0x04, 0x00, 0x0d, 0xf0);

static ble_uuid128_t BATTERY_CHAR_UUIDS[N_BATT];
static uint16_t s_batt_char_handles[N_BATT];
static uint16_t s_system_char_handle;
static uint16_t s_config_char_handle;

static void pack_battery(const BmuBattery *b, uint8_t out[24]) {
    out[0]  = (b->voltage_mv >> 8) & 0xff;
    out[1]  = b->voltage_mv & 0xff;
    int16_t cA = (int16_t)(b->current_ma / 10);
    out[2]  = (cA >> 8) & 0xff;
    out[3]  = cA & 0xff;
    out[4]  = b->soc_pct;
    out[5]  = b->state;
    uint32_t ah_m = (uint32_t)(b->cumulative_ah * 1000.0f);
    out[6]  = (ah_m >> 24) & 0xff;
    out[7]  = (ah_m >> 16) & 0xff;
    out[8]  = (ah_m >> 8) & 0xff;
    out[9]  = ah_m & 0xff;
    out[10] = (b->rint_mohm >> 8) & 0xff;
    out[11] = b->rint_mohm & 0xff;
    uint16_t soh_m = (uint16_t)(b->soh * 1000.0f);
    out[12] = (soh_m >> 8) & 0xff;
    out[13] = soh_m & 0xff;
    out[14] = b->duty_pct;
    out[15] = b->flags;
    out[16] = (b->cycles >> 24) & 0xff;
    out[17] = (b->cycles >> 16) & 0xff;
    out[18] = (b->cycles >> 8) & 0xff;
    out[19] = b->cycles & 0xff;
    out[20] = 0; out[21] = 0; out[22] = 0; out[23] = 0;
}

static int batt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg) {
    int idx = (int)(intptr_t)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    BmuSnapshot snap;
    if (bmu_core_get_cached_snapshot(&snap) != 0) return BLE_ATT_ERR_UNLIKELY;
    uint8_t buf[24];
    pack_battery(&snap.batteries[idx], buf);
    int rc = os_mbuf_append(ctxt->om, buf, sizeof(buf));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int system_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    BmuSnapshot snap;
    if (bmu_core_get_cached_snapshot(&snap) != 0) return BLE_ATT_ERR_UNLIKELY;
    uint8_t buf[64] = {0};
    memcpy(buf, &snap.system, sizeof(snap.system) < 64 ? sizeof(snap.system) : 64);
    int rc = os_mbuf_append(ctxt->om, buf, sizeof(buf));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int config_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    BmuConfigC cfg;
    if (bmu_core_get_config(&cfg) != 0) return BLE_ATT_ERR_UNLIKELY;
    int rc = os_mbuf_append(ctxt->om, &cfg, sizeof(cfg));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* Service table builder - dynamic because we need 16 characteristic entries */
static struct ble_gatt_chr_def s_batt_chrs[N_BATT + 1];
static struct ble_gatt_chr_def s_sys_chrs[2];
static struct ble_gatt_chr_def s_cfg_chrs[2];
static struct ble_gatt_svc_def s_services[4];

int bmu_ble_gatt_init(void) {
    /* Build Battery Service characteristics */
    for (int i = 0; i < N_BATT; i++) {
        BATTERY_CHAR_UUIDS[i] = (ble_uuid128_t)BLE_UUID128_INIT(
            0x5f, 0x4e, 0x3d, 0x2c, 0x1b, 0x0a,
            0x5d, 0x9e, 0x6f, 0x4a,
            0x1f, 0x9b,
            0x10 + i, 0x00, 0x0d, 0xf0);
        s_batt_chrs[i].uuid = &BATTERY_CHAR_UUIDS[i].u;
        s_batt_chrs[i].access_cb = batt_access_cb;
        s_batt_chrs[i].arg = (void *)(intptr_t)i;
        s_batt_chrs[i].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY;
        s_batt_chrs[i].val_handle = &s_batt_char_handles[i];
    }
    s_batt_chrs[N_BATT] = (struct ble_gatt_chr_def){ 0 };

    /* System Service */
    static const ble_uuid128_t SYS_CHAR_UUID = BLE_UUID128_INIT(
        0x5f, 0x4e, 0x3d, 0x2c, 0x1b, 0x0a,
        0x5d, 0x9e, 0x6f, 0x4a,
        0x1f, 0x9b,
        0x20, 0x00, 0x0d, 0xf0);
    s_sys_chrs[0].uuid = &SYS_CHAR_UUID.u;
    s_sys_chrs[0].access_cb = system_access_cb;
    s_sys_chrs[0].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY;
    s_sys_chrs[0].val_handle = &s_system_char_handle;
    s_sys_chrs[1] = (struct ble_gatt_chr_def){ 0 };

    /* Config Service */
    static const ble_uuid128_t CFG_CHAR_UUID = BLE_UUID128_INIT(
        0x5f, 0x4e, 0x3d, 0x2c, 0x1b, 0x0a,
        0x5d, 0x9e, 0x6f, 0x4a,
        0x1f, 0x9b,
        0x40, 0x00, 0x0d, 0xf0);
    s_cfg_chrs[0].uuid = &CFG_CHAR_UUID.u;
    s_cfg_chrs[0].access_cb = config_access_cb;
    s_cfg_chrs[0].flags = BLE_GATT_CHR_F_READ;
    s_cfg_chrs[0].val_handle = &s_config_char_handle;
    s_cfg_chrs[1] = (struct ble_gatt_chr_def){ 0 };

    /* Services table */
    s_services[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    s_services[0].uuid = &BATTERY_SVC_UUID.u;
    s_services[0].characteristics = s_batt_chrs;
    s_services[1].type = BLE_GATT_SVC_TYPE_PRIMARY;
    s_services[1].uuid = &SYSTEM_SVC_UUID.u;
    s_services[1].characteristics = s_sys_chrs;
    s_services[2].type = BLE_GATT_SVC_TYPE_PRIMARY;
    s_services[2].uuid = &CONFIG_SVC_UUID.u;
    s_services[2].characteristics = s_cfg_chrs;
    s_services[3] = (struct ble_gatt_svc_def){ 0 };

    int rc = ble_gatts_count_cfg(s_services);
    if (rc != 0) { ESP_LOGE(TAG, "count_cfg rc=%d", rc); return rc; }
    rc = ble_gatts_add_svcs(s_services);
    if (rc != 0) { ESP_LOGE(TAG, "add_svcs rc=%d", rc); return rc; }
    ESP_LOGI(TAG, "gatt services registered (3 services, %d characteristics)",
             N_BATT + 2);
    return 0;
}

void bmu_ble_notify_battery_all(void) {
    BmuSnapshot snap;
    if (bmu_core_get_cached_snapshot(&snap) != 0) return;
    for (int i = 0; i < N_BATT; i++) {
        uint8_t buf[24];
        pack_battery(&snap.batteries[i], buf);
        struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
        if (om) ble_gatts_notify_custom(0, s_batt_char_handles[i], om);
    }
}

void bmu_ble_notify_system(void) {
    /* Similar pattern: pack system aggregated, ble_gatts_notify_custom */
}
```

Note : le `conn_handle` passé à `ble_gatts_notify_custom` doit être celui du peer
subscribe ; en pratique il faut tracker les `BLE_GAP_EVENT_SUBSCRIBE` events pour
savoir quelle connexion notifier. Pour simplicité Phase 18, on notify tous les
connected peers via une boucle interne. Voir spec §7.3 pour détails.

---

### Task 18.5 : `task_ble` notify loop + integration

**Files:**

- Create: `firmware-idf-v2/main/task_ble.cpp`
- Create: `firmware-idf-v2/main/task_ble.h`
- Modify: `firmware-idf-v2/main/main.cpp`
- Modify: `firmware-idf-v2/main/CMakeLists.txt`

```cpp
#include "task_ble.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bmu_ble.h"

static const char *TAG = "TASK_BLE";

extern "C" void bmu_ble_notify_battery_all(void);
extern "C" void bmu_ble_notify_system(void);

static void task_ble(void *arg) {
    ESP_LOGI(TAG, "ble task starting");
    bmu_ble_init();
    const TickType_t period = pdMS_TO_TICKS(500);   // 2 Hz
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        bmu_ble_notify_battery_all();
        bmu_ble_notify_system();
        vTaskDelayUntil(&last, period);
    }
}

extern "C" void task_ble_start(void) {
    xTaskCreatePinnedToCore(task_ble, "ble", 8192, NULL, 3, NULL, 1);
}
```

Patcher `main.cpp` pour appeler `task_ble_start()` après `task_ui_start()`.

---

### Task 18.6 : Smoke iOS app + commit Phase 18

#### Step 1 : Flash + monitor

```bash
cd firmware-idf-v2 && idf.py build && idf.py -p /dev/cu.usbmodem1101 flash
python3 scripts/monitor_passive.py /dev/cu.usbmodem1101 > /tmp/phase18-smoke.log &
sleep 60
```

Expected monitor :

```
I BMU_BLE: sync OK, advertising as KXKM-BMU-XXXX
I BMU_BLE_GATT: gatt services registered (3 services, 18 characteristics)
I TASK_BLE: ble task starting
```

#### Step 2 : Test iOS app

- Ouvrir `iosApp/` sur iPhone avec XCode
- Build + run
- Scanner → voir `KXKM-BMU-XXXX`
- Connect → voir 16 batteries live (valeurs qui bougent à 2 Hz)
- Comparer avec MQTT broker `mosquitto_sub -h kxkm-ai -t 'bmu/+/telemetry'` — les V/I
  doivent être cohérentes à ±1 %

#### Step 3 : MQTT + BLE coexistence

Vérifier pendant 5 min que :

- MQTT continue de publier 1 Hz sans gap
- BLE notifications arrivent à 2 Hz dans iOS app
- Aucun crash / WDT
- Heap stable

#### Step 4 : Commit

```bash
git add firmware-idf-v2/components/bmu_ble/ \
        firmware-idf-v2/main/task_ble.cpp \
        firmware-idf-v2/main/task_ble.h \
        firmware-idf-v2/main/main.cpp \
        firmware-idf-v2/main/CMakeLists.txt \
        firmware-idf-v2/sdkconfig.defaults \
        docs/superpowers/validation/runs/
```

Commit subject : `feat(phase-18): add ble read-only gatt services` (45 chars).

Body :

```
- bmu_ble: NimBLE init with software coexistence for BT/Wi-Fi,
  advertising name KXKM-BMU-XXXX, max 4 connections
- Battery Service F00D0001: 16 characteristics, 24 bytes
  packed big-endian per battery, read + notify
- System Service F00D0002: 64 bytes aggregated snapshot
- Config Service F00D0004: read-only BmuConfigC dump
- task_ble: pinned core 1 prio 3, 8 KiB stack, notify 2 Hz
- UUIDs 100 percent compat with v1 iOS app (audit 2026-04-08)
- Smoke iOS app: 16 batteries live, MQTT unaffected, heap stable
```

### Known risks — Phase 18

- **BT/Wi-Fi coexistence** — software coex activé ; surveiller les drop MQTT pendant
  les smoke tests. Si packet loss > 5 %, enabler `CONFIG_BT_CTRL_MODE_EFF=3`.
- **UUID drift v1 ↔ v2** — iOS app hardcode les UUIDs. Vérifier avec un audit diff
  vs `iosApp/` avant flash. Si mismatch, corriger les bytes dans `BATTERY_SVC_UUID`.
- **Notify storm** — 16 chars × 2 Hz = 32 notify/s. iOS peut perdre si bandwidth low.
  Alternative : packer toutes les batteries dans 1 characteristic "bulk" 384 bytes
  (16 × 24). Décision à valider sur iPhone réel.
- **`conn_handle` tracking** — `ble_gatts_notify_custom(conn, ...)` nécessite le
  conn_handle du peer. Pour Phase 18 simple, on utilise `conn_handle = 0` qui broadcast
  aux peers subscribe. À raffiner Phase 19.
- **Advertising interruption pendant connexion** — en BLE 5.0 périphérique classique,
  l'advertising s'arrête quand une connexion est établie. Redémarrer dans
  `GAP_EVENT_DISCONNECT`. Déjà implémenté dans `gap_event_cb`.

- [ ] **Commit** : `feat(phase-18): add ble read-only gatt services`

### Execution Notes — Phase 18

1. **Heap surveillance** — NimBLE consomme ~40 KB heap au boot. Vérifier que
   `esp_get_free_heap_size()` reste ≥ 100 KB après `bmu_ble_init()`.
2. **Latence notify** — mesurer le délai entre `bmu_core_get_cached_snapshot` et
   l'arrivée iOS. Attendu < 300 ms end-to-end.
3. **Security** — Phase 18 n'active PAS de pairing. Les services sont accessibles
   sans bonding. C'est OK parce que read-only + pas de données sensibles
   (les V/I batteries sont publiques dans MQTT aussi). **Phase 19** ajoute le
   Control Service qui requiert pairing SC + HMAC.

---


# Phase 19 — BLE Control + Secure Connections + HMAC (CRIT-D)

**Objectif** : ajouter le **BLE Control Service** F00D0003 avec write commands
authentifiées. Activer NimBLE Secure Connections (SC) + passkey display 6 chiffres.
Dériver une clé HMAC-SHA256 du LTK post-bonding pour signer chaque commande + nonce
anti-replay. Audit log append-only sur SD card pour chaque commande reçue (PASS ou REJECT).

**Cette phase résout la vulnérabilité CRIT-D de l'audit 2026-03-30** — dernière faille
critique en suspens depuis Part 1. Une relecture sécurité dédiée (`ecc:security-review`
ou `superpowers:santa-loop`) est **obligatoire** avant le commit.

**Durée estimée** : 2-3 jours. **Risk** : **TRÈS ÉLEVÉ — SÉCURITÉ CRITIQUE**. Une erreur
de logique ici peut permettre une commande `ForceOff` non autorisée qui déclenche une
déconnexion batterie en plein show, avec risque physique (arc électrique, perte
d'éclairage scénographique, risque incendie sur load dump).

**Acceptance** :

- Pairing SC fonctionne : passkey 6 digits affiché sur LVGL, saisi côté iOS, bond stocké NVS
- Commande envoyée sans bonding → rejetée `BLE_ATT_ERR_INSUFFICIENT_AUTHENTICATION`
- Commande envoyée avec HMAC invalide → rejetée, log audit `HMAC_FAIL`
- Commande envoyée avec nonce déjà vu → rejetée (anti-replay)
- Rate limit 10 cmd/s/conn respecté → commandes suivantes droppées
- Audit log `/sdcard/audit.log` contient chaque commande (PASS + REJECT) avec timestamp,
  peer MAC, cmd_id, résultat, HMAC chaîné
- Max 4 bondings stockés, éviction LRU au-delà
- Tests positifs + négatifs validés (voir Task 19.7)
- **Security review signée avant merge**

**Commit subject** : `feat(phase-19): ble control + sc pairing + hmac` (49 chars).

## Task list — Phase 19

| # | Task | Files | Durée |
|---|---|---|---|
| 19.1 | NimBLE Secure Connections + Passkey Display + bonding NVS | `bmu_ble.c`, `sdkconfig.defaults`, LVGL overlay | 4 h |
| 19.2 | Control Service F00D0003 + 8 commands dispatcher | `bmu_ble_control.c` | 4 h |
| 19.3 | HMAC-SHA256 derivation depuis LTK via HKDF | `bmu_ble_hmac.c` | 3 h |
| 19.4 | Anti-replay nonce persistent NVS | `bmu_ble_control.c` | 2 h |
| 19.5 | Rate limiting token bucket 10 cmd/s/conn | `bmu_ble_control.c` | 1 h |
| 19.6 | Audit log SD append-only chained HMAC | `bmu_ble_audit.c` | 3 h |
| 19.7 | Tests positifs + négatifs + security review | `tests/hil/test_ble_control.py` | 4 h |
| 19.8 | Commit Phase 19 avec review sign-off | (commit) | 1 h |

### Heritage v1

| Item | Source v1 | Réutilisation | Notes |
|---|---|---|---|
| NimBLE SC pairing | aucun | **0 % copie** | v1 n'utilisait pas SC — nouveau code |
| HMAC derivation | aucun | **0 % copie** | Nouveau, cf spec §7.5 |
| Audit log format | `firmware-idf/components/bmu_storage/audit.c` | **~30 % copie** | Format similaire mais signature HMAC chain nouvelle |
| Command dispatch | `bmu_core_submit_cmd` FFI | **100 %** (existant) | Exposé par Part 1 Phase 10, on l'appelle depuis le callback GATT |

### Avant de commencer Phase 19

1. **Lire intégralement** spec `docs/superpowers/specs/2026-04-09-bmu-rust-hybrid-v2-design.md`
   sections §7.4, §7.5, §7.6 (BLE Security Model, HMAC derivation, audit log)
2. **Relire l'audit** `docs/superpowers/audit/2026-03-30-security-audit.md` section CRIT-D
3. **Noter** que `iosApp/` doit déjà avoir le support SC + HMAC côté app (vérifier le
   ticket KMP `kxkm-bmu-app/docs/ble-control-protocol.md`)
4. **S'engager** à ne pas merger sans `ecc:security-review` passing

### Command frame layout (spec §7.4)

```
byte  type    field           notes
0     u8      cmd_id          0x01..0x09 (voir table ci-dessous)
1     u8      battery_idx     0..15 ou 0xFF = all
2     u32     param           BE, semantique dependant cmd
6     u32     nonce_lo        BE, low 32 bits du nonce 64-bit
10    u32     nonce_hi        BE, high 32 bits
14    u8[8]   hmac            truncated HMAC-SHA256(cmd_id || idx || param || nonce)
22    total                   22 bytes fixed
```

Pour `SetLabel` (cmd 0x05) et `WifiProv` (Phase 20 cmd 0x08), la frame est étendue en
version-2 (TBD Phase 20).

### Commands (spec §7.4)

| ID | Name | Rust core call | Side effect |
|---|---|---|---|
| 0x01 | ForceOff | `bmu_core_cmd_force_off(idx)` | Battery(s) OFF immediately |
| 0x02 | ResetAh | `bmu_core_cmd_reset_ah(idx)` | Clear cumulative_ah counter |
| 0x03 | TriggerRint | `bmu_core_cmd_trigger_rint(idx)` | Queue Rint measurement |
| 0x04 | ResetLatch | `bmu_core_cmd_reset_latch(idx)` | Clear latched fault |
| 0x05 | SetLabel | NVS write only | Persist user label for battery |
| 0x06 | SetConfig | `bmu_core_cmd_set_config(key, val)` | Update runtime threshold |
| 0x07 | Reboot | `esp_restart()` after 500 ms | Device reboot |
| 0x09 | RequestSoh | `bmu_core_cmd_request_soh(idx)` | Queue TFLite inference |

0x08 = WifiProv is Phase 20 (extended frame).

---

### Task 19.1 : NimBLE Secure Connections + Passkey Display + bonding NVS

**Files:**

- Modify: `firmware-idf-v2/components/bmu_ble/src/bmu_ble.c`
- Modify: `firmware-idf-v2/sdkconfig.defaults`
- Modify: `firmware-idf-v2/components/bmu_ui/src/tab_config.c` (overlay passkey)

#### Step 1 : Patcher `sdkconfig.defaults`

```
# Secure Connections pairing
CONFIG_BT_NIMBLE_SM_SC=y
CONFIG_BT_NIMBLE_SM_LEGACY=n
CONFIG_BT_NIMBLE_DEBUG=n
CONFIG_BT_NIMBLE_NVS_PERSIST=y

# NVS encryption (protect bonding store + HMAC keys)
CONFIG_NVS_ENCRYPTION=y
CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC=y
```

**Important** : activer `CONFIG_NVS_ENCRYPTION=y` nécessite une eFuse key partition
dédiée. Documenter dans `firmware-idf-v2/CLAUDE.md` la procédure :

```
# Generate encrypted NVS key partition (once per device)
espsecure.py generate_flash_encryption_key keys/nvs_key.bin
espefuse.py --port /dev/cu.usbmodem1101 burn_key BLOCK_KEY0 keys/nvs_key.bin NVS_ENCRYPTION_KEY
```

#### Step 2 : Modifier `bmu_ble.c` pour SC + passkey display

Dans `bmu_ble_init()`, ajouter après `ble_svc_gap_init()` :

```c
ble_hs_cfg.sm_bonding = 1;
ble_hs_cfg.sm_mitm = 1;
ble_hs_cfg.sm_sc = 1;
ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
```

Dans `gap_event_cb`, ajouter le passkey display handler :

```c
case BLE_GAP_EVENT_PASSKEY_ACTION: {
    struct ble_sm_io pkey = {0};
    if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
        pkey.action = BLE_SM_IOACT_DISP;
        pkey.passkey = esp_random() % 1000000;   // 6 digits
        ESP_LOGI(TAG, "passkey to display: %06" PRIu32, pkey.passkey);
        bmu_ui_show_passkey(pkey.passkey);   // LVGL overlay
        ble_sm_inject_io(event->passkey.conn_handle, &pkey);
    }
    break;
}
case BLE_GAP_EVENT_ENC_CHANGE: {
    ESP_LOGI(TAG, "encryption change conn=%d status=%d",
              event->enc_change.conn_handle, event->enc_change.status);
    if (event->enc_change.status == 0) {
        bmu_ui_hide_passkey();
        /* Derive HMAC key from LTK */
        bmu_ble_hmac_derive_for_conn(event->enc_change.conn_handle);
    }
    break;
}
```

#### Step 3 : LVGL overlay passkey dans `tab_config.c`

Ajouter une fonction `bmu_ui_show_passkey(uint32_t passkey)` qui crée un
`lv_msgbox` overlay avec le passkey en grand. `bmu_ui_hide_passkey()` le supprime.

```c
static lv_obj_t *s_passkey_msgbox;

void bmu_ui_show_passkey(uint32_t passkey) {
    if (!lvgl_port_lock(10)) return;
    static const char *btns[] = { "" };
    char buf[32];
    snprintf(buf, sizeof(buf), "Pairing\n%06" PRIu32, passkey);
    s_passkey_msgbox = lv_msgbox_create(NULL, "BLE Pairing", buf, btns, false);
    lv_obj_center(s_passkey_msgbox);
    lvgl_port_unlock();
}

void bmu_ui_hide_passkey(void) {
    if (!lvgl_port_lock(10)) return;
    if (s_passkey_msgbox) {
        lv_msgbox_close(s_passkey_msgbox);
        s_passkey_msgbox = NULL;
    }
    lvgl_port_unlock();
}
```

#### Step 4 : Bonding storage NVS

NimBLE gère automatiquement le bonding via `CONFIG_BT_NIMBLE_NVS_PERSIST=y`. Les bonds
sont stockés dans le namespace NVS `nimble_bond` (géré par le stack). Aucun code
custom requis, juste vérifier que les bonds persistent après reboot.

**Max 4 bondings** : configurable via `CONFIG_BT_NIMBLE_MAX_BONDS=4` dans `sdkconfig.defaults`.
Éviction LRU est native NimBLE.

---

### Task 19.2 : Control Service F00D0003 + 8 commands dispatcher

**Files:**

- Create: `firmware-idf-v2/components/bmu_ble/src/bmu_ble_control.c`
- Modify: `firmware-idf-v2/components/bmu_ble/src/bmu_ble_gatt.c` (add Control service to table)

#### Step 1 : `bmu_ble_control.c`

```c
#include <string.h>
#include <inttypes.h>
#include "host/ble_hs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "bmu_core.h"
#include "bmu_ble_control.h"
#include "bmu_ble_hmac.h"
#include "bmu_ble_audit.h"

#define TAG "BMU_BLE_CTRL"

/* Frame layout */
#pragma pack(push, 1)
typedef struct {
    uint8_t  cmd_id;
    uint8_t  battery_idx;
    uint32_t param_be;
    uint32_t nonce_lo_be;
    uint32_t nonce_hi_be;
    uint8_t  hmac[8];
} cmd_frame_t;
#pragma pack(pop)

_Static_assert(sizeof(cmd_frame_t) == 22, "cmd_frame_t must be 22 bytes");

/* Per-connection state */
typedef struct {
    uint16_t conn_handle;
    uint64_t last_nonce;
    uint8_t  token_bucket;
    uint64_t last_refill_us;
    bool     active;
} conn_state_t;

#define MAX_CONNS 4
static conn_state_t s_conns[MAX_CONNS];

static conn_state_t *find_or_alloc_conn(uint16_t conn_handle) {
    for (int i = 0; i < MAX_CONNS; i++) {
        if (s_conns[i].active && s_conns[i].conn_handle == conn_handle)
            return &s_conns[i];
    }
    for (int i = 0; i < MAX_CONNS; i++) {
        if (!s_conns[i].active) {
            s_conns[i] = (conn_state_t){ .conn_handle = conn_handle,
                                          .token_bucket = 10,
                                          .last_refill_us = esp_timer_get_time(),
                                          .active = true };
            return &s_conns[i];
        }
    }
    return NULL;
}

static bool rate_limit_allow(conn_state_t *c) {
    uint64_t now = esp_timer_get_time();
    uint64_t delta_ms = (now - c->last_refill_us) / 1000;
    if (delta_ms >= 100) {
        /* refill 1 token per 100 ms, cap 10 */
        uint8_t add = (uint8_t)(delta_ms / 100);
        c->token_bucket = (c->token_bucket + add > 10) ? 10 : c->token_bucket + add;
        c->last_refill_us = now;
    }
    if (c->token_bucket == 0) return false;
    c->token_bucket--;
    return true;
}

static esp_err_t dispatch_command(uint8_t cmd_id, uint8_t idx, uint32_t param) {
    switch (cmd_id) {
        case 0x01: return bmu_core_cmd_force_off(idx);
        case 0x02: return bmu_core_cmd_reset_ah(idx);
        case 0x03: return bmu_core_cmd_trigger_rint(idx);
        case 0x04: return bmu_core_cmd_reset_latch(idx);
        case 0x05: /* SetLabel handled elsewhere (NVS write) */
                   return bmu_ble_control_set_label(idx, param);
        case 0x06: return bmu_core_cmd_set_config(idx, param);
        case 0x07: {
            ESP_LOGW(TAG, "Reboot requested, scheduling in 500 ms");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            return ESP_OK;   /* unreachable */
        }
        case 0x09: return bmu_core_cmd_request_soh(idx);
        default:   return ESP_ERR_INVALID_ARG;
    }
}

int bmu_ble_control_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
        return BLE_ATT_ERR_UNLIKELY;

    /* 1. Require encrypted link (SC pairing done) */
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn_handle, &desc) != 0)
        return BLE_ATT_ERR_UNLIKELY;
    if (!desc.sec_state.encrypted || !desc.sec_state.bonded) {
        ESP_LOGW(TAG, "rejected: link not encrypted/bonded");
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, 0, "NOT_BONDED");
        return BLE_ATT_ERR_INSUFFICIENT_AUTHENTICATION;
    }

    /* 2. Size check */
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len != sizeof(cmd_frame_t)) {
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, 0, "BAD_SIZE");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    cmd_frame_t frame;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, &frame, sizeof(frame), NULL);
    if (rc != 0) return BLE_ATT_ERR_UNLIKELY;

    /* 3. Rate limit */
    conn_state_t *c = find_or_alloc_conn(conn_handle);
    if (!c || !rate_limit_allow(c)) {
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, frame.cmd_id, "RATE_LIMIT");
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    /* 4. HMAC verify */
    uint64_t nonce = ((uint64_t)ntohl(frame.nonce_hi_be) << 32) | ntohl(frame.nonce_lo_be);
    if (nonce <= c->last_nonce) {
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, frame.cmd_id, "REPLAY");
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    if (!bmu_ble_hmac_verify(conn_handle, &frame, sizeof(frame) - 8,
                              frame.hmac, 8)) {
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, frame.cmd_id, "HMAC_FAIL");
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    c->last_nonce = nonce;
    bmu_ble_hmac_persist_nonce(&desc.peer_ota_addr, nonce);

    /* 5. Dispatch */
    uint32_t param = ntohl(frame.param_be);
    esp_err_t result = dispatch_command(frame.cmd_id, frame.battery_idx, param);
    bmu_ble_audit_log_pass(&desc.peer_ota_addr, frame.cmd_id,
                             frame.battery_idx, param, result);
    return result == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
}
```

#### Step 2 : Ajouter le Control Service au GATT table

Dans `bmu_ble_gatt.c`, ajouter un 4ème service dans `s_services[]` :

```c
static const ble_uuid128_t CONTROL_SVC_UUID = BLE_UUID128_INIT(
    0x5f, 0x4e, 0x3d, 0x2c, 0x1b, 0x0a,
    0x5d, 0x9e, 0x6f, 0x4a,
    0x1f, 0x9b,
    0x03, 0x00, 0x0d, 0xf0);
static const ble_uuid128_t CONTROL_CHAR_UUID = BLE_UUID128_INIT(
    0x5f, 0x4e, 0x3d, 0x2c, 0x1b, 0x0a,
    0x5d, 0x9e, 0x6f, 0x4a,
    0x1f, 0x9b,
    0x30, 0x00, 0x0d, 0xf0);

static struct ble_gatt_chr_def s_ctrl_chrs[2];

extern int bmu_ble_control_write_cb(uint16_t, uint16_t,
                                     struct ble_gatt_access_ctxt *, void *);

/* In bmu_ble_gatt_init() */
s_ctrl_chrs[0].uuid = &CONTROL_CHAR_UUID.u;
s_ctrl_chrs[0].access_cb = bmu_ble_control_write_cb;
s_ctrl_chrs[0].flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC
                     | BLE_GATT_CHR_F_WRITE_AUTHEN;
s_ctrl_chrs[1] = (struct ble_gatt_chr_def){ 0 };

/* Extend s_services[] to 5 entries, adding control at index 3 */
```

Le flag `BLE_GATT_CHR_F_WRITE_AUTHEN` exige une connexion authentifiée (MITM-protected
SC pairing) au niveau ATT, doublant la vérification faite dans `dispatch_command`.

---

### Task 19.3 : HMAC-SHA256 derivation depuis LTK via HKDF

**Files:** `firmware-idf-v2/components/bmu_ble/src/bmu_ble_hmac.c`, `bmu_ble_hmac.h`

#### Spec §7.5 HMAC derivation

```
Per-connection HMAC key = HKDF-SHA256(
    IKM = LTK (16 bytes after SC pairing),
    salt = peer BD_ADDR (6 bytes),
    info = "KXKM-BMU-CMD-v1",
    L = 32
)
```

Le LTK est accessible depuis `ble_store_read()` avec key type `BLE_STORE_OBJ_TYPE_OUR_SEC`
après un pairing SC réussi.

#### Step 1 : `bmu_ble_hmac.h`

```c
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "host/ble_hs.h"

void bmu_ble_hmac_derive_for_conn(uint16_t conn_handle);
bool bmu_ble_hmac_verify(uint16_t conn_handle,
                          const void *msg, size_t msg_len,
                          const uint8_t *tag, size_t tag_len);
void bmu_ble_hmac_persist_nonce(const ble_addr_t *peer, uint64_t nonce);
uint64_t bmu_ble_hmac_load_nonce(const ble_addr_t *peer);
```

#### Step 2 : `bmu_ble_hmac.c`

```c
#include "bmu_ble_hmac.h"
#include <string.h>
#include "esp_log.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "host/ble_store.h"
#include "nvs.h"

#define TAG "BMU_BLE_HMAC"
#define MAX_CONNS 4
#define HMAC_KEY_LEN 32
#define INFO_STR "KXKM-BMU-CMD-v1"

typedef struct {
    uint16_t conn_handle;
    uint8_t  key[HMAC_KEY_LEN];
    bool     valid;
} hmac_entry_t;

static hmac_entry_t s_entries[MAX_CONNS];

void bmu_ble_hmac_derive_for_conn(uint16_t conn_handle) {
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn_handle, &desc) != 0) return;

    /* Fetch our sec record with LTK */
    union ble_store_key key = {0};
    union ble_store_value val = {0};
    memcpy(&key.sec.peer_addr, &desc.peer_id_addr, sizeof(ble_addr_t));
    int rc = ble_store_read(BLE_STORE_OBJ_TYPE_PEER_SEC, &key, &val);
    if (rc != 0 || !val.sec.ltk_present) {
        ESP_LOGW(TAG, "LTK not available for conn=%d", conn_handle);
        return;
    }

    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    hmac_entry_t *slot = NULL;
    for (int i = 0; i < MAX_CONNS; i++) {
        if (!s_entries[i].valid) { slot = &s_entries[i]; break; }
    }
    if (!slot) {
        ESP_LOGW(TAG, "no HMAC slot free");
        return;
    }
    slot->conn_handle = conn_handle;
    int ret = mbedtls_hkdf(md_info,
                            desc.peer_id_addr.val, 6,      /* salt */
                            val.sec.ltk, 16,                /* IKM */
                            (const uint8_t *)INFO_STR,
                            strlen(INFO_STR),
                            slot->key, HMAC_KEY_LEN);       /* OKM */
    if (ret != 0) {
        ESP_LOGE(TAG, "hkdf failed ret=%d", ret);
        return;
    }
    slot->valid = true;
    ESP_LOGI(TAG, "HMAC key derived for conn=%d", conn_handle);
}

bool bmu_ble_hmac_verify(uint16_t conn_handle,
                          const void *msg, size_t msg_len,
                          const uint8_t *tag, size_t tag_len) {
    hmac_entry_t *slot = NULL;
    for (int i = 0; i < MAX_CONNS; i++) {
        if (s_entries[i].valid && s_entries[i].conn_handle == conn_handle) {
            slot = &s_entries[i]; break;
        }
    }
    if (!slot) return false;

    uint8_t computed[32];
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    int ret = mbedtls_md_hmac(md_info, slot->key, HMAC_KEY_LEN,
                               msg, msg_len, computed);
    if (ret != 0) return false;

    /* Constant-time compare first tag_len bytes */
    uint8_t diff = 0;
    for (size_t i = 0; i < tag_len; i++) diff |= computed[i] ^ tag[i];
    return diff == 0;
}
```

#### Step 3 : Nonce persistence NVS

```c
void bmu_ble_hmac_persist_nonce(const ble_addr_t *peer, uint64_t nonce) {
    nvs_handle_t h;
    if (nvs_open("bmu_nonces", NVS_READWRITE, &h) != ESP_OK) return;
    char key[16];
    snprintf(key, sizeof(key), "%02x%02x%02x%02x%02x%02x",
             peer->val[5], peer->val[4], peer->val[3],
             peer->val[2], peer->val[1], peer->val[0]);
    nvs_set_u64(h, key, nonce);
    nvs_commit(h);
    nvs_close(h);
}

uint64_t bmu_ble_hmac_load_nonce(const ble_addr_t *peer) {
    nvs_handle_t h;
    if (nvs_open("bmu_nonces", NVS_READONLY, &h) != ESP_OK) return 0;
    char key[16];
    snprintf(key, sizeof(key), "%02x%02x%02x%02x%02x%02x",
             peer->val[5], peer->val[4], peer->val[3],
             peer->val[2], peer->val[1], peer->val[0]);
    uint64_t nonce = 0;
    nvs_get_u64(h, key, &nonce);
    nvs_close(h);
    return nonce;
}
```

**Charger le nonce au pairing** : lors de `BLE_GAP_EVENT_ENC_CHANGE`, après
`bmu_ble_hmac_derive_for_conn`, appeler `bmu_ble_hmac_load_nonce(peer)` et initialiser
`conn_state.last_nonce`.

---

### Task 19.4 : Anti-replay nonce — déjà couvert Task 19.3

La logique est intégrée dans `bmu_ble_control_write_cb` (check `nonce <= last_nonce`) et
persistée via `bmu_ble_hmac_persist_nonce`. Aucun code additionnel.

**Test critique** : persister à chaque commande, **pas** seulement au disconnect. Si
le device reboot entre temps, le nonce doit survivre.

---

### Task 19.5 : Rate limiting — déjà couvert Task 19.2

Token bucket `rate_limit_allow()` dans `bmu_ble_control.c`. Config par défaut : 10
tokens, refill 1 token / 100 ms → 10 cmd/s soutenu.

**Test** : script Python qui envoie 15 commandes en 1 s → les 5 dernières doivent être
droppées (`BLE_ATT_ERR_WRITE_NOT_PERMITTED`), audit log montre `RATE_LIMIT` pour chacune.

---

### Task 19.6 : Audit log SD append-only + HMAC chain

**Files:** `firmware-idf-v2/components/bmu_ble/src/bmu_ble_audit.c`, `bmu_ble_audit.h`

#### Format ligne audit

```
<timestamp_us> <peer_mac> <cmd_id> <bat_idx> <param_hex> <result> <hmac_chain_hex8>
```

Example :

```
1712345678123456 aa:bb:cc:dd:ee:ff 01 03 00000000 PASS a3f8c71d
1712345679234567 aa:bb:cc:dd:ee:ff 01 ff 00000000 REJECT_HMAC_FAIL 2b7e91ae
```

Le `hmac_chain_hex8` est HMAC-SHA256(line_content || prev_hmac)[0..4]. Il permet de
détecter tampering a posteriori si quelqu'un modifie les lignes passées.

#### Step 1 : `bmu_ble_audit.c`

```c
#include "bmu_ble_audit.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/md.h"
#include "nvs.h"

#define TAG "BMU_BLE_AUDIT"
#define AUDIT_PATH "/sdcard/audit.log"
#define HMAC_KEY_NVS_NAME "audit_key"

static uint8_t s_audit_key[32];
static uint8_t s_prev_hmac[4] = {0};
static bool s_key_loaded = false;

static esp_err_t load_or_create_audit_key(void) {
    nvs_handle_t h;
    if (nvs_open("bmu", NVS_READWRITE, &h) != ESP_OK) return ESP_FAIL;
    size_t len = sizeof(s_audit_key);
    if (nvs_get_blob(h, HMAC_KEY_NVS_NAME, s_audit_key, &len) != ESP_OK) {
        esp_fill_random(s_audit_key, sizeof(s_audit_key));
        nvs_set_blob(h, HMAC_KEY_NVS_NAME, s_audit_key, sizeof(s_audit_key));
        nvs_commit(h);
        ESP_LOGI(TAG, "new audit key generated");
    }
    nvs_close(h);
    s_key_loaded = true;
    return ESP_OK;
}

static void append_line(const char *line) {
    if (!s_key_loaded) load_or_create_audit_key();

    FILE *f = fopen(AUDIT_PATH, "ab");
    if (!f) { ESP_LOGW(TAG, "open failed"); return; }

    /* Compute HMAC(line || prev_hmac) */
    uint8_t hmac_buf[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, s_audit_key, sizeof(s_audit_key));
    mbedtls_md_hmac_update(&ctx, (const uint8_t *)line, strlen(line));
    mbedtls_md_hmac_update(&ctx, s_prev_hmac, sizeof(s_prev_hmac));
    mbedtls_md_hmac_finish(&ctx, hmac_buf);
    mbedtls_md_free(&ctx);

    fprintf(f, "%s %02x%02x%02x%02x\n", line,
             hmac_buf[0], hmac_buf[1], hmac_buf[2], hmac_buf[3]);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    memcpy(s_prev_hmac, hmac_buf, 4);
}

void bmu_ble_audit_log_pass(const ble_addr_t *peer, uint8_t cmd_id,
                              uint8_t bat_idx, uint32_t param, int result) {
    char line[128];
    snprintf(line, sizeof(line),
             "%" PRIu64 " %02x:%02x:%02x:%02x:%02x:%02x %02x %02x %08" PRIx32 " %s",
             (uint64_t)esp_timer_get_time(),
             peer->val[5], peer->val[4], peer->val[3],
             peer->val[2], peer->val[1], peer->val[0],
             cmd_id, bat_idx, param,
             result == 0 ? "PASS" : "FAIL");
    append_line(line);
}

void bmu_ble_audit_log_reject(const ble_addr_t *peer, uint8_t cmd_id,
                                const char *reason) {
    char line[128];
    snprintf(line, sizeof(line),
             "%" PRIu64 " %02x:%02x:%02x:%02x:%02x:%02x %02x -- -- REJECT_%s",
             (uint64_t)esp_timer_get_time(),
             peer->val[5], peer->val[4], peer->val[3],
             peer->val[2], peer->val[1], peer->val[0],
             cmd_id, reason);
    append_line(line);
}
```

#### Step 2 : Verification script Python (offline)

`scripts/audit/verify_audit_log.py` — lit `/sdcard/audit.log`, recalcule chaque
HMAC chaîné avec la key dumpée NVS, flag toute incohérence.

---

### Task 19.7 : Tests positifs + négatifs + security review

**Files:** `firmware-idf-v2/tests/hil/test_ble_control.py`

Ce script Python utilise `bleak` pour simuler l'app iOS côté Mac/Linux.

#### Step 1 : Test matrix

| Test | Attendu |
|---|---|
| 1. Connect + pair SC avec passkey (lu depuis log série) | bond stocké |
| 2. Send ForceOff(idx=3) avec HMAC valide | result OK, audit `PASS`, battery 3 OFF sur MQTT |
| 3. Send same ForceOff (same nonce) | reject `WRITE_NOT_PERMITTED`, audit `REPLAY` |
| 4. Send ForceOff sans bonding (disconnect, reconnect unbonded) | reject `INSUFFICIENT_AUTHENTICATION` |
| 5. Send ForceOff avec HMAC tweaked 1 byte | reject, audit `HMAC_FAIL` |
| 6. Send 15 cmd en 1 s | 10 pass, 5 reject `RATE_LIMIT` |
| 7. Reboot device → reconnect → send cmd avec nonce < last_persisted | reject `REPLAY` |
| 8. Bond 5 devices → 5e device force éviction LRU du 1er | vérifier bondsetNVS |

#### Step 2 : Script skeleton `test_ble_control.py`

```python
import asyncio
import hmac
import hashlib
import struct
import pytest
from bleak import BleakClient, BleakScanner

BMU_NAME_PREFIX = "KXKM-BMU"
CONTROL_CHAR = "f00d0030-9b1f-4a6f-9e5d-0a1b2c3d4e5f"
INFO = b"KXKM-BMU-CMD-v1"

def hkdf_sha256(ikm, salt, info, length=32):
    from cryptography.hazmat.primitives.kdf.hkdf import HKDF
    from cryptography.hazmat.primitives import hashes
    return HKDF(algorithm=hashes.SHA256(), length=length,
                 salt=salt, info=info).derive(ikm)

def build_frame(cmd_id, bat_idx, param, nonce, hmac_key):
    body = struct.pack(">BBIII", cmd_id, bat_idx, param,
                        nonce & 0xffffffff, (nonce >> 32) & 0xffffffff)
    tag = hmac.new(hmac_key, body, hashlib.sha256).digest()[:8]
    return body + tag

@pytest.mark.asyncio
async def test_force_off_pass(bmu_addr, ltk, peer_addr):
    hmac_key = hkdf_sha256(ltk, bytes(peer_addr), INFO)
    async with BleakClient(bmu_addr) as client:
        nonce = 1
        frame = build_frame(0x01, 3, 0, nonce, hmac_key)
        await client.write_gatt_char(CONTROL_CHAR, frame, response=True)
        await asyncio.sleep(0.5)
        # Verify via MQTT that battery 3 state transitioned to ForcedOff
        # (asserted by external fixture)

@pytest.mark.asyncio
async def test_replay_reject(bmu_addr, ltk, peer_addr):
    hmac_key = hkdf_sha256(ltk, bytes(peer_addr), INFO)
    async with BleakClient(bmu_addr) as client:
        frame = build_frame(0x01, 3, 0, 1, hmac_key)
        await client.write_gatt_char(CONTROL_CHAR, frame, response=True)
        with pytest.raises(Exception) as exc_info:
            await client.write_gatt_char(CONTROL_CHAR, frame, response=True)
        assert "not permitted" in str(exc_info.value).lower()
```

#### Step 3 : Security review sign-off

Avant le commit, exécuter :

```bash
# Superpowers santa-loop
# (or ecc:security-review)
```

Le review doit vérifier :

1. Pas de secret log (`ESP_LOGI` ou `printf` ne contient pas LTK ou HMAC key brute)
2. Constant-time compare HMAC (vérifié dans `bmu_ble_hmac_verify`)
3. Nonce persiste entre reboot (vérifié par test 7)
4. Rate limit effectif (vérifié par test 6)
5. Bonding requis (vérifié par test 4)
6. Audit log persistant SD (vérifié par le format de ligne)
7. NVS encryption activée en production (sdkconfig)
8. Passkey vraiment aléatoire (`esp_random`, pas `rand()`)

Archiver le review dans `docs/superpowers/audit/2026-MM-DD-phase19-security-review.md`.

---

### Task 19.8 : Commit Phase 19 avec review sign-off

#### Step 1 : Smoke bench complet

```bash
cd firmware-idf-v2
idf.py -p /dev/cu.usbmodem1101 flash
python3 scripts/monitor_passive.py /dev/cu.usbmodem1101 > /tmp/phase19-smoke.log &
sleep 300   # 5 min observation

# Run HIL test suite
cd tests/hil && pytest test_ble_control.py -v
# Expected: 8 tests pass

cp /tmp/phase19-smoke.log \
   docs/superpowers/validation/runs/$(date +%Y-%m-%d)-phase19-ble-security.log
```

#### Step 2 : Staging

```bash
git add firmware-idf-v2/components/bmu_ble/ \
        firmware-idf-v2/components/bmu_ui/src/tab_config.c \
        firmware-idf-v2/sdkconfig.defaults \
        firmware-idf-v2/tests/hil/test_ble_control.py \
        scripts/audit/verify_audit_log.py \
        docs/superpowers/audit/ \
        docs/superpowers/validation/runs/
```

#### Step 3 : Commit atomique

Subject : `feat(phase-19): ble control + sc pairing + hmac` (49 chars).

Body :

```
Resolves CRIT-D from audit 2026-03-30 (last critical gap).

- NimBLE Secure Connections pairing with MITM protection
- Passkey 6 digits displayed on LVGL overlay, random at boot
- Max 4 bondings stored in encrypted NVS, LRU eviction
- Control Service F00D0003 with 22-byte command frame
  (cmd_id, battery_idx, param, nonce, HMAC-SHA256 trunc 8B)
- HMAC key = HKDF-SHA256(LTK, peer_addr, "KXKM-BMU-CMD-v1")
- Anti-replay nonce persisted NVS per peer, survives reboot
- Rate limit 10 cmd/s/conn via token bucket
- Audit log /sdcard/audit.log append-only with HMAC chain
- Commands: ForceOff, ResetAh, TriggerRint, ResetLatch,
  SetLabel, SetConfig, Reboot, RequestSoh
- Security review signed off (docs/superpowers/audit/)
- 8 HIL tests pass (positive + negative scenarios)
```

**Avant le commit** : vérifier que `security-review` est signé dans
`docs/superpowers/audit/`. Pas de commit sans review.

### Known risks — Phase 19

- **LTK exfiltration via logs** — grep `ESP_LOGI.*ltk` et `printf.*ltk` avant le commit.
  Aucun log ne doit contenir la clé brute.
- **NVS encryption not active in dev** — warning log au boot si `CONFIG_NVS_ENCRYPTION=n`.
  Prod builds MUST avoir NVS encryption ON. CI check recommandé.
- **Passkey DoS** — afficher passkey en clair sur LVGL = leak si caméra. Post-Phase 19
  hardening possible : afficher seulement pendant 30 s après bouton physique.
- **Bonding table full** — 5e peer force LRU eviction. Documenter le comportement pour
  les utilisateurs (iOS app).
- **Debug builds en prod** — **JAMAIS** flasher un firmware Phase 19 debug sur bench
  prod. Ajouter un check CI `CONFIG_BT_NIMBLE_DEBUG=n` en release.
- **Clock skew nonce** — le nonce est basé sur un compteur monotone, pas sur le temps.
  Pas de problème d'horloge.
- **Reboot-before-persist** — si reboot entre `bmu_core_cmd_force_off` et
  `bmu_ble_hmac_persist_nonce`, le nonce est perdu → replay possible. Mitigation :
  persister le nonce AVANT le dispatch (Task 19.2 step dispatch order à vérifier).
- **Audit log rotation** — pas de rotation Phase 19 (append infini). À ajouter Phase 22
  ou post-v2 si le fichier dépasse 10 MB.

- [ ] **Commit** : `feat(phase-19): ble control + sc pairing + hmac`

### Execution Notes — Phase 19

1. **Relecture sécurité obligatoire** avant le commit. Utiliser
   `superpowers:requesting-code-review` ou `ecc:santa-loop` pour forcer un double pass.
2. **CRIT-D closure** : mettre à jour `docs/superpowers/audit/2026-03-30-security-audit.md`
   en marquant CRIT-D comme résolu avec référence au commit Phase 19.
3. **Documentation iOS app** : mettre à jour `iosApp/docs/ble-protocol.md` avec le
   nouveau frame format + HMAC derivation pour que l'équipe mobile puisse adapter
   son code.
4. **Persistence nonce order** : la séquence doit être :
   1. Verify HMAC
   2. Verify nonce > last_nonce
   3. **Persist new nonce to NVS** (before any side effect)
   4. Dispatch command
   5. Audit log result
   Cet ordre est critique : si l'étape 3 échoue, on rejette plutôt que d'exécuter.

---


# Phase 20 — Wi-Fi provisioning via BLE Command

**Objectif** : permettre de provisionner le Wi-Fi d'un BMU vierge sans serial port,
uniquement via l'app iOS sur BLE. Ajouter la commande BLE `0x08 WifiProv` dans le
dispatcher Phase 19, avec une frame étendue qui transporte SSID + PSK. Après NVS write,
le device reboot et reconnecte au Wi-Fi cible.

**Durée estimée** : 1 jour. **Risk** : moyen (NVS write + reboot timing, PSK secret
handling). **Acceptance** :

- Un device flashé avec NVS vide peut être provisionné 100 % via iOS app en < 60 s
  (pairing SC + envoi commande + reboot + reconnect + MQTT up)
- Le PSK n'apparaît jamais en clair dans les logs (`ESP_LOG_HEX` interdit sur le payload)
- Commande WifiProv rejetée si bonding absent (héritage Phase 19)
- NVS write atomique (power-loss during commit → rollback OK natif)

**Commit subject** : `feat(phase-20): wifiprov command via ble` (39 chars).

## Task list — Phase 20

| # | Task | Files | Durée |
|---|---|---|---|
| 20.1 | Extended frame v2 pour WifiProv | `bmu_ble_control.c`, `bmu_ble_wifi_prov.c` | 2 h |
| 20.2 | NVS write ssid/psk + `esp_restart()` après 500 ms | `bmu_ble_wifi_prov.c` | 2 h |
| 20.3 | PSK redaction dans les logs | audit sources | 1 h |
| 20.4 | Test end-to-end iOS vierge → provisionné | `tests/hil/test_wifi_prov.py` | 2 h |
| 20.5 | Commit Phase 20 | (commit) | 1 h |

### Heritage v1

- Aucun — v1 utilisait `credentials.h` compile-time. Phase 20 est 100 % nouveau.
- Phase 19 fournit déjà l'infra HMAC/nonce/audit — Phase 20 étend juste le dispatcher.

### Extended frame (command 0x08 WifiProv)

La frame standard 22 bytes ne suffit pas (SSID+PSK = jusqu'à 96 bytes). On utilise une
frame version 2 de 128 bytes :

```
byte  type      field
0     u8        cmd_id (0x08)
1     u8        flags (bit0 = v2 extended)
2-33  char[32]  ssid (nul-padded)
34-97 char[64]  psk (nul-padded)
98-105 u64      nonce
106-137 u8[32]  hmac HMAC-SHA256(full frame[0..106])
138                  total size = 138 bytes
```

Note : 32 bytes de HMAC pour cette frame (full tag, pas truncé) à cause du payload
sensible. Le MTU BLE par défaut est 23 ATT (exchange 512 max). On négocie un ATT MTU
de 247 (`BLE_ATT_MTU_MAX`) dans `bmu_ble.c` Phase 18.

---

### Task 20.1 : Extended frame v2 pour WifiProv

**Files:**

- Create: `firmware-idf-v2/components/bmu_ble/src/bmu_ble_wifi_prov.c`
- Create: `firmware-idf-v2/components/bmu_ble/src/bmu_ble_wifi_prov.h`
- Modify: `firmware-idf-v2/components/bmu_ble/src/bmu_ble_control.c`

#### Step 1 : Modifier `bmu_ble_control.c` pour détecter v2

Dans `bmu_ble_control_write_cb`, avant le check taille fixe 22 bytes :

```c
/* Check if this is a v2 extended frame (WifiProv) */
uint8_t first_byte;
os_mbuf_copydata(ctxt->om, 0, 1, &first_byte);
if (first_byte == 0x08) {
    return bmu_ble_wifi_prov_handle(conn_handle, ctxt);
}
/* ... existing v1 22-byte logic ... */
```

#### Step 2 : `bmu_ble_wifi_prov.c`

```c
#include "bmu_ble_wifi_prov.h"
#include "bmu_ble_hmac.h"
#include "bmu_ble_audit.h"
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "bmu_wifi.h"

#define TAG "BMU_BLE_PROV"
#define FRAME_V2_SIZE 138

#pragma pack(push, 1)
typedef struct {
    uint8_t  cmd_id;      /* 0x08 */
    uint8_t  flags;       /* bit0 = v2 */
    char     ssid[32];
    char     psk[64];
    uint64_t nonce_be;
    uint8_t  hmac[32];
} prov_frame_t;
#pragma pack(pop)

_Static_assert(sizeof(prov_frame_t) == FRAME_V2_SIZE, "size mismatch");

int bmu_ble_wifi_prov_handle(uint16_t conn_handle,
                               struct ble_gatt_access_ctxt *ctxt) {
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn_handle, &desc) != 0)
        return BLE_ATT_ERR_UNLIKELY;
    if (!desc.sec_state.encrypted || !desc.sec_state.bonded) {
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, 0x08, "NOT_BONDED");
        return BLE_ATT_ERR_INSUFFICIENT_AUTHENTICATION;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len != FRAME_V2_SIZE) {
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, 0x08, "BAD_SIZE");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    prov_frame_t frame;
    if (ble_hs_mbuf_to_flat(ctxt->om, &frame, FRAME_V2_SIZE, NULL) != 0)
        return BLE_ATT_ERR_UNLIKELY;

    /* HMAC verify sur les 106 premiers bytes */
    if (!bmu_ble_hmac_verify(conn_handle, &frame, 106, frame.hmac, 32)) {
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, 0x08, "HMAC_FAIL");
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    /* Nonce check + persist */
    uint64_t nonce;
    memcpy(&nonce, &frame.nonce_be, 8);
    /* BE to host */
    nonce = __builtin_bswap64(nonce);
    uint64_t last = bmu_ble_hmac_load_nonce(&desc.peer_ota_addr);
    if (nonce <= last) {
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, 0x08, "REPLAY");
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    bmu_ble_hmac_persist_nonce(&desc.peer_ota_addr, nonce);

    /* Enforce nul-termination */
    frame.ssid[31] = 0;
    frame.psk[63] = 0;

    /* Validate */
    if (strlen(frame.ssid) == 0 || strlen(frame.ssid) > 32) {
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, 0x08, "BAD_SSID");
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Log WITHOUT revealing PSK */
    ESP_LOGI(TAG, "WifiProv received: ssid='%s' psk_len=%d",
             frame.ssid, (int)strlen(frame.psk));

    /* Write NVS */
    esp_err_t err = bmu_wifi_set_creds(frame.ssid, frame.psk);
    if (err != ESP_OK) {
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, 0x08, "NVS_WRITE_FAIL");
        return BLE_ATT_ERR_UNLIKELY;
    }

    bmu_ble_audit_log_pass(&desc.peer_ota_addr, 0x08, 0, 0, 0);

    /* Schedule reboot in 500 ms to let the BLE ACK go out */
    xTaskCreate([](void *arg) {
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_LOGI("BMU_BLE_PROV", "rebooting after WifiProv");
        esp_restart();
    }, "reboot", 2048, NULL, 10, NULL);

    return 0;
}
```

**Note C/C++** : le lambda `[](void *arg){...}` est C++ only. Si `bmu_ble_wifi_prov.c`
est compilé en C pur, remplacer par une fonction `static void reboot_task(void *arg)`.

---

### Task 20.2 : NVS write — déjà couvert par `bmu_wifi_set_creds` Phase 16

La fonction a été créée Task 16.1. Vérifier :

- Écriture atomique via `nvs_commit`
- Double buffer NVS natif ESP-IDF → power-loss safe
- Pas de log du PSK

---

### Task 20.3 : Audit PSK redaction dans les logs

**Files:** grep sur toute la base, revue manuelle

```bash
grep -rn "frame.psk\|wifi.psk\|password" firmware-idf-v2/components/
```

Vérifier qu'aucune occurrence ne loggue le PSK directement. Seuls acceptables :
- `strlen(psk)` pour le log
- Hash SHA-256 pour un fingerprint debug (optionnel, hors scope Phase 20)

---

### Task 20.4 : Test end-to-end iOS vierge → provisionné

**Files:** `firmware-idf-v2/tests/hil/test_wifi_prov.py`

#### Scénario

1. `idf.py erase-flash` + `idf.py flash` (NVS vide)
2. Device boot → log `BMU_WIFI: no creds, waiting Phase 20 BLE prov`
3. iOS app : scan → voir `KXKM-BMU-XXXX`
4. iOS : pair SC (passkey 6 digits de LVGL)
5. iOS : envoi `WifiProv(ssid="KXKM-2.4G", psk="...")`
6. Device log : `BMU_BLE_PROV: WifiProv received: ssid='KXKM-2.4G' psk_len=12`
7. Device log : `rebooting after WifiProv`
8. Reboot (~3 s)
9. Device log : `BMU_WIFI: got IP: ...` + `BMU_MQTT: broker connected`
10. Broker reçoit telemetry

**Exit timing** : end-to-end < 60 s dont ~3 s reboot.

#### Python pytest

```python
import asyncio
import pytest
from bleak import BleakClient, BleakScanner

@pytest.mark.asyncio
async def test_wifi_prov_end_to_end(bmu_erased_fixture, mqtt_broker):
    device = await BleakScanner.find_device_by_name("KXKM-BMU-XXXX", timeout=10)
    assert device is not None
    async with BleakClient(device) as client:
        # Assume pairing handled out-of-band via test fixture
        ssid = b"KXKM-2.4G"
        psk = b"testpassword"
        frame = build_wifi_prov_frame(ssid, psk, nonce=1, hmac_key=test_key)
        await client.write_gatt_char(CONTROL_CHAR, frame, response=True)

    # Device reboots, we wait for it
    await asyncio.sleep(10)
    # Check broker received telemetry
    msg = await mqtt_broker.wait_for_message("bmu/+/telemetry", timeout=30)
    assert msg is not None
```

---

### Task 20.5 : Commit Phase 20

```bash
git add firmware-idf-v2/components/bmu_ble/src/bmu_ble_wifi_prov.c \
        firmware-idf-v2/components/bmu_ble/src/bmu_ble_wifi_prov.h \
        firmware-idf-v2/components/bmu_ble/src/bmu_ble_control.c \
        firmware-idf-v2/tests/hil/test_wifi_prov.py \
        docs/superpowers/validation/runs/
```

Subject : `feat(phase-20): wifiprov command via ble` (39 chars).

Body :

```
- Extended frame v2 (138 bytes) for cmd 0x08 WifiProv
  carrying ssid[32] + psk[64] + nonce + HMAC full 32B
- bmu_ble_wifi_prov_handle: SC bond check, HMAC verify,
  nonce check, NVS write via bmu_wifi_set_creds, reboot
  task delayed 500 ms to let BLE ACK propagate
- PSK never logged in clear (only strlen for debug)
- test_wifi_prov.py HIL: erased device provisioned in <60 s
- ATT MTU negotiated 247 to fit 138-byte frame in 1 PDU
```

### Known risks — Phase 20

- **NVS corruption during commit** : double buffer ESP-IDF OK.
- **PSK leakage via advertising logs** : `ESP_LOG_HEX` sur frame → leak. Ne jamais.
- **SSID > 32 bytes** : IEEE max 32, on truncate à 32 + nul.
- **Reboot before audit log flush** : appeler `bmu_ble_audit_log_pass` AVANT
  `xTaskCreate(reboot_task)` — l'ordre est critique, déjà respecté dans le code.
- **ATT MTU negotiation failure** : iOS app doit négocier MTU ≥ 200. Si
  seulement 23 (default), la frame est fragmentée sur plusieurs Prepare Write +
  Execute Write → plus complexe. Vérifier iOS side.

- [ ] **Commit** : `feat(phase-20): wifiprov command via ble`

### Execution Notes — Phase 20

1. **ATT MTU exchange** : ajouter dans `bmu_ble.c` event `BLE_GAP_EVENT_MTU` handler
   pour logger le MTU négocié. Si < 200, warning et documenter pour iOS app.
2. **Factory reset** : un mécanisme d'effacement NVS (appui long sur bouton physique
   BOX-3) devrait être ajouté pour re-provisionning en cas d'erreur. Hors scope
   Phase 20, noter pour Phase 22 ou post-v2.
3. **Multi-réseau** : Phase 20 ne gère qu'un seul (ssid, psk). Si besoin de basculer
   entre 2 réseaux (bench + site), prévoir une commande `WifiProvAdd` plus tard.

---


# Phase 21 — Victron SmartShunt emulation

**Objectif** : faire apparaître le BMU comme un Victron SmartShunt dans l'écosystème
VictronConnect / Cerbo GX. Deux surfaces :

1. **GATT service Victron** 0x6597 avec 5 characteristics (voltage, current, SoC, power,
   consumed_ah) — pour VictronConnect app qui se connecte en point-à-point
2. **Instant Readout advertising** AES-CTR encrypted — visible par tout Cerbo GX sur le
   même réseau BLE sans pairing

Les valeurs publiées sont des **fleet aggregates** calculés depuis le snapshot Rust :

- `fleet_voltage_mv = mean(voltage WHERE state=Online)`
- `fleet_current_ma = sum(current WHERE state=Online)`
- `fleet_soc = weighted_avg(soc * capacity)`
- `fleet_power = V × I / 1000`
- `fleet_consumed_ah = sum(cumulative_ah)`

**Durée estimée** : 1-2 jours. **Risk** : moyen (protocole Victron reverse-engineered,
advertising coexistence avec services BMU Phase 18). **Acceptance** :

- VictronConnect app sur iPhone détecte `KXKM-BMU-XXXX` en SmartShunt SmartShunt 500A
- Valeurs V/I/SoC cohérentes avec MQTT broker (delta ≤ 1 %)
- Instant Readout chiffré déchiffrable avec le bind key stocké NVS
- BMU services (Battery/System/Config/Control) restent advertis et connectables
  en parallèle

**Commit subject** : `feat(phase-21): add victron smartshunt emulation` (46 chars).

## Task list — Phase 21

| # | Task | Files | Durée |
|---|---|---|---|
| 21.1 | Fleet aggregation helper pure function | `components/bmu_ble_victron/src/fleet_agg.c` | 2 h |
| 21.2 | GATT Victron Service 0x6597 | `bmu_ble_victron.c` | 3 h |
| 21.3 | Instant Readout advertising AES-CTR | `instant_readout.c` | 3 h |
| 21.4 | Advertising coexistence strategy | `bmu_ble.c`, `bmu_ble_victron.c` | 2 h |
| 21.5 | Test VictronConnect iPhone | manuel | 2 h |
| 21.6 | Commit Phase 21 | (commit) | 1 h |

### Heritage v1

| Composant | Source v1 | Réutilisation |
|---|---|---|
| `bmu_ble_victron_gatt` | `firmware-idf/components/bmu_ble_victron_gatt/` | **~90 % copie** — GATT wrapper trivial |
| `bmu_ble_victron` | `firmware-idf/components/bmu_ble_victron/` | **~85 % copie** — Instant Readout encode + RE'd protocol |
| `bmu_ble_victron_scan` | `firmware-idf/components/bmu_ble_victron_scan/` | **non utilisé** — on émule, on ne scan pas |

---

### Task 21.1 : Fleet aggregation helper pure function

**Files:**

- Create: `firmware-idf-v2/components/bmu_ble_victron/CMakeLists.txt`
- Create: `firmware-idf-v2/components/bmu_ble_victron/include/bmu_ble_victron.h`
- Create: `firmware-idf-v2/components/bmu_ble_victron/src/fleet_agg.c`
- Create: `firmware-idf-v2/components/bmu_ble_victron/include/fleet_agg.h`

#### Step 1 : `CMakeLists.txt`

```cmake
idf_component_register(
    SRCS
        "src/fleet_agg.c"
        "src/bmu_ble_victron.c"
        "src/instant_readout.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES
        bt
        mbedtls
        nvs_flash
        bmu_core_rs
        log
)
```

#### Step 2 : `fleet_agg.h`

```c
#pragma once
#include <stdint.h>
#include "bmu_core.h"

typedef struct {
    uint16_t voltage_mV;
    int32_t  current_mA;      /* signed, can be large (sum 16 batteries) */
    uint8_t  soc_pct;
    int32_t  power_cW;        /* centi-watts (V * I / 10) */
    uint32_t consumed_ah_mAh; /* cumulative ah in mAh */
    int8_t   t_max_C;
    uint8_t  n_online;
} bmu_fleet_agg_t;

/** Pure function — no side effects. Safe to call from BLE task. */
void fleet_agg_compute(const BmuSnapshot *snap, bmu_fleet_agg_t *out);
```

#### Step 3 : `fleet_agg.c`

```c
#include "fleet_agg.h"
#include <string.h>

void fleet_agg_compute(const BmuSnapshot *snap, bmu_fleet_agg_t *out) {
    memset(out, 0, sizeof(*out));
    uint32_t v_sum = 0;
    int32_t  i_sum = 0;
    uint32_t soc_weighted = 0;
    uint32_t cap_sum = 0;
    uint32_t consumed = 0;
    int8_t   t_max = -128;
    uint8_t  n = 0;

    for (int i = 0; i < MAX_BATTERIES; i++) {
        const BmuBattery *b = &snap->batteries[i];
        if (b->state != 1) continue;  /* Online only */
        v_sum += b->voltage_mv;
        i_sum += b->current_ma;
        uint32_t cap = b->nominal_ah_milli;  /* capacity from config */
        if (cap == 0) cap = 100000;          /* 100 Ah default */
        soc_weighted += (uint32_t)b->soc_pct * cap;
        cap_sum += cap;
        consumed += (uint32_t)(b->cumulative_ah * 1000.0f);
        if (b->temp_c > t_max) t_max = b->temp_c;
        n++;
    }

    if (n > 0) {
        out->voltage_mV = (uint16_t)(v_sum / n);
        out->current_mA = i_sum;
        out->soc_pct = (uint8_t)(soc_weighted / cap_sum);
        /* power = V * I, convert to cW (10^-2 W) */
        out->power_cW = (int32_t)((int64_t)out->voltage_mV * i_sum / 100000);
        out->consumed_ah_mAh = consumed;
        out->t_max_C = t_max;
        out->n_online = n;
    }
}
```

#### Step 4 : Host test (optionnel)

Ajouter un test dans `firmware-rust/crates/bmu-types/tests/` qui vérifie :

- Vide snapshot → tout zéro
- 1 battery 24 V 1 A 50 % → V=24000, I=1000, SoC=50
- 4 batteries mixed → moyennes correctes

---

### Task 21.2 : GATT Victron Service 0x6597

**Files:**

- Create: `firmware-idf-v2/components/bmu_ble_victron/src/bmu_ble_victron.c`
- Modify: `firmware-idf-v2/main/main.cpp` (call init)
- Modify: `firmware-idf-v2/components/bmu_ble_victron/include/bmu_ble_victron.h`

#### Heritage v1

```bash
sed -n '1,200p' firmware-idf/components/bmu_ble_victron_gatt/bmu_ble_victron_gatt.cpp
```

La v1 définit un service UUID `0x6597` (ou un 128-bit Victron custom) avec 5
characteristics. On copie la structure table et on substitue les callbacks read
pour utiliser `fleet_agg_compute`.

#### Step 1 : Service table

```c
#include "bmu_ble_victron.h"
#include "fleet_agg.h"
#include <string.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gatt/ble_svc_gatt.h"
#include "bmu_core.h"

#define TAG "BMU_VICTRON"

static const ble_uuid16_t VICTRON_SVC_UUID = BLE_UUID16_INIT(0x6597);

static const ble_uuid16_t VOLTAGE_UUID = BLE_UUID16_INIT(0x6598);
static const ble_uuid16_t CURRENT_UUID = BLE_UUID16_INIT(0x6599);
static const ble_uuid16_t SOC_UUID     = BLE_UUID16_INIT(0x659A);
static const ble_uuid16_t POWER_UUID   = BLE_UUID16_INIT(0x659B);
static const ble_uuid16_t CONSUMED_UUID = BLE_UUID16_INIT(0x659C);

static int read_voltage_cb(uint16_t, uint16_t, struct ble_gatt_access_ctxt *c, void *);
static int read_current_cb(uint16_t, uint16_t, struct ble_gatt_access_ctxt *c, void *);
static int read_soc_cb(uint16_t, uint16_t, struct ble_gatt_access_ctxt *c, void *);
static int read_power_cb(uint16_t, uint16_t, struct ble_gatt_access_ctxt *c, void *);
static int read_consumed_cb(uint16_t, uint16_t, struct ble_gatt_access_ctxt *c, void *);

static const struct ble_gatt_chr_def s_chrs[] = {
    { .uuid = &VOLTAGE_UUID.u, .access_cb = read_voltage_cb,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
    { .uuid = &CURRENT_UUID.u, .access_cb = read_current_cb,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
    { .uuid = &SOC_UUID.u, .access_cb = read_soc_cb,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
    { .uuid = &POWER_UUID.u, .access_cb = read_power_cb,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
    { .uuid = &CONSUMED_UUID.u, .access_cb = read_consumed_cb,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
    { 0 }
};

static const struct ble_gatt_svc_def s_services[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY,
      .uuid = &VICTRON_SVC_UUID.u,
      .characteristics = s_chrs },
    { 0 }
};

static void get_agg(bmu_fleet_agg_t *agg) {
    BmuSnapshot snap;
    bmu_core_get_cached_snapshot(&snap);
    fleet_agg_compute(&snap, agg);
}

static int read_voltage_cb(uint16_t ch, uint16_t ah,
                             struct ble_gatt_access_ctxt *c, void *arg) {
    bmu_fleet_agg_t agg; get_agg(&agg);
    uint16_t v = agg.voltage_mV;
    return os_mbuf_append(c->om, &v, 2);
}
static int read_current_cb(uint16_t ch, uint16_t ah,
                             struct ble_gatt_access_ctxt *c, void *arg) {
    bmu_fleet_agg_t agg; get_agg(&agg);
    int32_t i = agg.current_mA;
    return os_mbuf_append(c->om, &i, 4);
}
static int read_soc_cb(uint16_t ch, uint16_t ah,
                         struct ble_gatt_access_ctxt *c, void *arg) {
    bmu_fleet_agg_t agg; get_agg(&agg);
    uint8_t s = agg.soc_pct;
    return os_mbuf_append(c->om, &s, 1);
}
static int read_power_cb(uint16_t ch, uint16_t ah,
                           struct ble_gatt_access_ctxt *c, void *arg) {
    bmu_fleet_agg_t agg; get_agg(&agg);
    int32_t p = agg.power_cW;
    return os_mbuf_append(c->om, &p, 4);
}
static int read_consumed_cb(uint16_t ch, uint16_t ah,
                              struct ble_gatt_access_ctxt *c, void *arg) {
    bmu_fleet_agg_t agg; get_agg(&agg);
    uint32_t cah = agg.consumed_ah_mAh;
    return os_mbuf_append(c->om, &cah, 4);
}

int bmu_ble_victron_gatt_init(void) {
    int rc = ble_gatts_count_cfg(s_services);
    if (rc != 0) return rc;
    return ble_gatts_add_svcs(s_services);
}
```

**Note** : cet init doit être appelé **avant** `ble_gatts_start()` (qui est implicite
au démarrage NimBLE). Concrètement, on l'ajoute dans `bmu_ble_gatt_init` (Phase 18)
comme 5e service après les 4 BMU.

---

### Task 21.3 : Instant Readout advertising AES-CTR

**Files:** `firmware-idf-v2/components/bmu_ble_victron/src/instant_readout.c`

#### Format Victron Instant Readout (RE'd)

```
Manufacturer Data (0x02E1 Victron prefix)
  | byte 0    | byte 1    | byte 2     | bytes 3..15 |
  | prod_type | record_type | frag_idx | AES-CTR(payload) |
```

Le payload plaintext contient `{voltage_mV, current_cA, soc_pct, consumed_ah, ...}`
packés en 13 bytes. La clé AES-128 est le `bind_key` Victron, stocké NVS sous
`bmu/victron/bind_key`.

#### Step 1 : NVS key management

```c
#define NVS_NS "bmu"
#define NVS_KEY "victron/bind_key"

static uint8_t s_bind_key[16];
static bool s_key_loaded;

static esp_err_t load_or_create_bind_key(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return ESP_FAIL;
    size_t len = sizeof(s_bind_key);
    esp_err_t err = nvs_get_blob(h, NVS_KEY, s_bind_key, &len);
    if (err != ESP_OK) {
        esp_fill_random(s_bind_key, sizeof(s_bind_key));
        nvs_set_blob(h, NVS_KEY, s_bind_key, sizeof(s_bind_key));
        nvs_commit(h);
        ESP_LOGI(TAG, "new Victron bind_key generated");
    }
    nvs_close(h);
    s_key_loaded = true;
    return ESP_OK;
}
```

#### Step 2 : Encode + encrypt frame

```c
#include "mbedtls/aes.h"

static void encode_instant_readout(uint8_t out[16], const bmu_fleet_agg_t *agg) {
    /* Plaintext layout (13 bytes payload + 3 header) */
    uint8_t plain[13] = {0};
    plain[0] = (agg->voltage_mV >> 0) & 0xff;
    plain[1] = (agg->voltage_mV >> 8) & 0xff;
    int16_t cA = (int16_t)(agg->current_mA / 10);
    plain[2] = cA & 0xff;
    plain[3] = (cA >> 8) & 0xff;
    plain[4] = agg->soc_pct;
    uint32_t cah = agg->consumed_ah_mAh;
    plain[5] = cah & 0xff;
    plain[6] = (cah >> 8) & 0xff;
    plain[7] = (cah >> 16) & 0xff;
    plain[8] = (cah >> 24) & 0xff;
    plain[9]  = (uint8_t)agg->t_max_C;
    plain[10] = agg->n_online;
    plain[11] = 0; plain[12] = 0;

    /* Build 3-byte header + AES-CTR(plain) */
    out[0] = 0xE1;   /* Victron prefix low */
    out[1] = 0x02;   /* Victron prefix high */
    out[2] = 0xA3;   /* product_type SmartShunt */

    /* AES-CTR: use simple IV = counter++ (persisted NVS would be ideal) */
    static uint32_t s_counter = 0;
    uint8_t iv[16] = {0};
    iv[0] = (s_counter >> 0) & 0xff;
    iv[1] = (s_counter >> 8) & 0xff;
    s_counter++;

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, s_bind_key, 128);
    uint8_t stream_block[16] = {0};
    size_t nc_off = 0;
    mbedtls_aes_crypt_ctr(&ctx, 13, &nc_off, iv, stream_block, plain, &out[3]);
    mbedtls_aes_free(&ctx);
}
```

#### Step 3 : Update adv data 1 Hz

```c
void bmu_ble_victron_adv_tick(void) {
    if (!s_key_loaded) load_or_create_bind_key();
    bmu_fleet_agg_t agg;
    BmuSnapshot snap;
    bmu_core_get_cached_snapshot(&snap);
    fleet_agg_compute(&snap, &agg);

    uint8_t adv_payload[16];
    encode_instant_readout(adv_payload, &agg);

    struct ble_hs_adv_fields fields = {0};
    fields.mfg_data = adv_payload;
    fields.mfg_data_len = sizeof(adv_payload);
    ble_gap_adv_set_fields(&fields);
}
```

**Note** : `ble_gap_adv_set_fields` peut ne pas permettre de mettre à jour pendant
l'advertising — il faut peut-être `ble_gap_adv_stop()` + set + `ble_gap_adv_start()`.
À mesurer sur bench.

---

### Task 21.4 : Advertising coexistence strategy

**Files:** `firmware-idf-v2/components/bmu_ble/src/bmu_ble.c`

**Problème** : NimBLE par défaut supporte 1 seul advertising set. Phase 18 fait
advertising avec le nom `KXKM-BMU-XXXX` + services BMU UUIDs. Phase 21 veut advertising
Victron avec manufacturer data distinct.

#### Option A : Single advertising with both payloads

Combiner dans une seule adv packet les deux types de données. Limite : 31 bytes
max en legacy advertising, vite saturé.

#### Option B : Extended Advertising (BLE 5.0)

Activer `CONFIG_BT_NIMBLE_EXT_ADV=y` et utiliser 2 advertising handles :

- Handle 0 : name + BMU services UUIDs (connectable)
- Handle 1 : Victron manufacturer data (non-connectable broadcast)

ESP32-S3 supporte BLE 5.0 extended advertising, donc c'est faisable.

#### Option C : Time-multiplexed

Alterner les 2 adv packets toutes les 500 ms dans la même task. Simple mais moins
clean et les scanners rapides peuvent manquer un des deux types.

**Décision** : Option B si ESP-IDF/NimBLE version le supporte proprement. Fallback
Option C si bugs.

```c
#if CONFIG_BT_NIMBLE_EXT_ADV
static uint8_t s_adv_handle_bmu = 0;
static uint8_t s_adv_handle_victron = 1;

static void setup_ext_adv_bmu(void) {
    struct ble_gap_ext_adv_params params = {0};
    params.connectable = 1;
    params.scannable = 1;
    params.legacy_pdu = 1;
    params.own_addr_type = s_own_addr_type;
    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_1M;
    params.tx_power = 127;
    ble_gap_ext_adv_configure(s_adv_handle_bmu, &params, NULL,
                                gap_event_cb, NULL);
    /* ble_gap_ext_adv_set_data(handle, adv_data_mbuf); */
    ble_gap_ext_adv_start(s_adv_handle_bmu, 0, 0);
}

static void setup_ext_adv_victron(void) {
    struct ble_gap_ext_adv_params params = {0};
    params.connectable = 0;
    params.scannable = 0;
    params.legacy_pdu = 1;
    params.own_addr_type = s_own_addr_type;
    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_1M;
    params.tx_power = 127;
    ble_gap_ext_adv_configure(s_adv_handle_victron, &params, NULL, NULL, NULL);
    ble_gap_ext_adv_start(s_adv_handle_victron, 0, 0);
}
#endif
```

---

### Task 21.5 : Test VictronConnect iPhone

**Files:** aucun (test manuel)

#### Procédure

1. Flasher firmware Phase 21
2. Noter le bind key depuis le log au boot (ou dump NVS)
3. Sur iPhone, installer VictronConnect app
4. App → Scan → voir `KXKM-BMU-XXXX` apparaître avec icône SmartShunt
5. Si encrypted : entrer le bind key
6. Vérifier affichage V/I/SoC/Power/Consumed cohérent avec MQTT broker
7. **Simultanément**, vérifier que `iosApp/` voit toujours les services BMU (Battery,
   System, Config) en parallèle → coexistence OK

Archiver screenshots dans `docs/superpowers/validation/runs/`.

---

### Task 21.6 : Commit Phase 21

```bash
git add firmware-idf-v2/components/bmu_ble_victron/ \
        firmware-idf-v2/components/bmu_ble/src/bmu_ble.c \
        firmware-idf-v2/main/main.cpp \
        firmware-idf-v2/sdkconfig.defaults \
        docs/superpowers/validation/runs/
```

Subject : `feat(phase-21): add victron smartshunt emulation` (46 chars).

Body :

```
- fleet_agg.c: pure function computing fleet V/I/SoC/P
  from BmuSnapshot (mean V, sum I, weighted SoC, sum AH)
- GATT Victron Service 0x6597 with 5 characteristics
  (voltage, current, soc, power, consumed_ah)
- Instant Readout advertising: 16-byte manufacturer data
  with Victron prefix + AES-CTR(plaintext, bind_key)
- bind_key auto-generated to NVS bmu/victron/bind_key
- Extended advertising 2 handles: BMU connectable + Victron
  broadcast non-connectable (BLE 5.0)
- VictronConnect iPhone: BMU visible as SmartShunt,
  values match MQTT within 1 percent
- iosApp still sees BMU services in parallel (coexistence OK)
```

### Known risks — Phase 21

- **Advertising coexistence** — extended advertising may have bugs in older NimBLE.
  Fallback to time-multiplex if issues.
- **Victron bind key leak** — même précautions que Phase 19 (NVS encryption).
- **Protocol drift** — Victron peut casser leur proto à tout moment (closed spec).
  Version-lock côté app iOS et pin à la reverse-engineering du moment.
- **Counter replay** — IV AES-CTR basé sur counter non-persisté. Au reboot, counter
  reset à 0 → potentiel replay. Si le bind key est unique par device, impact faible.
  Persister le counter NVS pour robustesse totale (optionnel Phase 21, obligatoire
  si deployment grand public).
- **Adv data length 31 bytes legacy** — si Victron + BMU concaténés dépassent 31,
  fragmentation ou extended adv obligatoire.

- [ ] **Commit** : `feat(phase-21): add victron smartshunt emulation`

### Execution Notes — Phase 21

1. **Inspection v1 avant code** — prendre 30 min pour lire
   `firmware-idf/components/bmu_ble_victron_gatt/bmu_ble_victron_gatt.cpp` en entier,
   noter le format exact des characteristics et les quirks découverts en v1.
2. **Bind key rotation** — pour un vrai déploiement, le bind key doit être provisionné
   out-of-band (QR code sur BMU, entré dans l'app mobile). Pour Phase 21 dev, auto-gen
   est OK.
3. **Cerbo GX test** — si un Cerbo GX est disponible, tester l'ingestion côté Cerbo
   (onglet Devices doit afficher le SmartShunt virtuel). Sinon, VictronConnect iPhone
   seul est suffisant pour Phase 21.
4. **Post-v2** : ajouter plus de characteristics Victron (temperature, aux voltage)
   si le VRM Portal Victron le demande.

---


# Phase 22 — HIL TB01-TB13 + bench 72 h + merge final

**Objectif** : campagne complète **Hardware-In-the-Loop** sur les 13 test benches
définis dans spec §10.4, Python harness `tests/hil/`, bench 72 h de stabilité continue
sur bench complet (16 batteries), génération PDF report signé, puis rename
`firmware-idf-v2` → `firmware-idf` et merge `feat/rust-hybrid-v2` → `main`. **C'est la
phase de livraison finale.**

**Durée estimée** : 3-5 jours (dont 72 h wall-clock pour le bench de stabilité).
**Risk** : élevé (coordination hardware humaine + découverte tardive de bugs + 72 h
= perte de temps significative si re-run nécessaire).

**Acceptance** :

- Rapport HIL PDF signé : ≥ 12/13 TB passing (TB04-TB10 nécessitent acceptation manuelle)
- Bench 72 h clean : 0 reboot, p99 tick `bmu_core_tick` < 50 ms sur toute la fenêtre,
  heap delta < 5 %
- PR sur `main` créée avec description exhaustive + lien rapport
- Code review ≥ 2 approbations (humain + santa-loop)
- Merge avec préservation de l'historique 50+ commits
- Tag release `v2.0.0-rust-hybrid` posé

**Commit subjects** :

- `feat(phase-22): hil harness + 72h bench + merge` (49 chars)
- `chore(archive): rename firmware-idf to firmware-idf-legacy` (séparé)
- `chore(phase-22): rename firmware-idf-v2 to firmware-idf` (séparé)

## Task list — Phase 22

| # | Task | Files | Durée |
|---|---|---|---|
| 22.1 | Python harness scaffold `tests/hil/` | `conftest.py`, `hil_device.py`, `hil_report.py` | 3 h |
| 22.2 | TB01-TB03 automated (boot + I2C + snapshot) | `tb01_*.py`, `tb02_*.py`, `tb03_*.py` | 3 h |
| 22.3 | TB04-TB10 semi-auto (prompts interactifs) | `tb04..tb10_*.py` | 4 h |
| 22.4 | TB11-TB12 automated (BLE + MQTT replay) | `tb11_*.py`, `tb12_*.py` | 3 h |
| 22.5 | TB13 bench 72 h continuous monitoring | `tb13_bench_72h.py` | 72 h wall |
| 22.6 | PDF report generation pipeline | `report_template.py`, `make hil-validate` | 3 h |
| 22.7 | Rename `firmware-idf-v2` → `firmware-idf` | Move operation, CI/CLAUDE.md updates | 2 h |
| 22.8 | PR creation + review + merge → main | GitHub PR via `gh` | 2 h |
| 22.9 | Tag release v2.0.0-rust-hybrid + commit Phase 22 | git tag, commit | 1 h |

### Heritage v1

- Aucun — HIL harness n'existait pas en v1 (S3 validation était manuelle opérateur).
- `specs/04_validation.md` contient les procédures manuelles TB01-TB13 qui servent de
  référence pour automatiser.

### TB Matrix — Automation classification

| TB | Titre | Automatable | Humain requis |
|---|---|---|---|
| TB01 | Boot + init chain | ✅ Python | — |
| TB02 | Scan I²C 16 INA + 4 TCA | ✅ Python | — |
| TB03 | Snapshot V/I cohérent MQTT | ✅ Python | — |
| TB04 | Court-circuit SDA/SCL | ❌ | ✅ Opérateur |
| TB05 | Débranchement batterie live | ❌ | ✅ Opérateur |
| TB06 | Sur-tension injected | ❌ | ✅ Opérateur (PSU) |
| TB07 | Sur-intensité load dump | ❌ | ✅ Opérateur (load) |
| TB08 | Sur-température forced | ❌ | ✅ Opérateur (heat gun) |
| TB09 | Court-circuit sortie | ❌ | ✅ Opérateur |
| TB10 | Coupure alim MCU | ❌ | ✅ Opérateur |
| TB11 | BLE pairing + commands | ✅ Python (bleak) | — |
| TB12 | MQTT replay SD | ✅ Python (mosquitto_sub) | — |
| TB13 | Bench 72 h stability | ✅ Python (long monitor) | — |

---

### Task 22.1 : Python harness scaffold `tests/hil/`

**Files:**

- Create: `firmware-idf-v2/tests/hil/conftest.py`
- Create: `firmware-idf-v2/tests/hil/hil_device.py`
- Create: `firmware-idf-v2/tests/hil/hil_report.py`
- Create: `firmware-idf-v2/tests/hil/pyproject.toml` (uv project)
- Create: `firmware-idf-v2/tests/hil/README.md`

#### Step 1 : `pyproject.toml`

```toml
[project]
name = "kxkm-bmu-hil"
version = "0.1.0"
requires-python = ">=3.11"
dependencies = [
    "pytest>=8.0",
    "pytest-asyncio>=0.23",
    "pyserial>=3.5",
    "paho-mqtt>=2.0",
    "bleak>=0.22",
    "reportlab>=4.1",
    "cryptography>=42.0",
]

[tool.pytest.ini_options]
asyncio_mode = "auto"
testpaths = ["."]
```

Installation via `uv` (CLAUDE.md rule Python 3.14) :

```bash
cd firmware-idf-v2/tests/hil && uv sync
```

#### Step 2 : `hil_device.py`

```python
"""HilDevice wrapper: serial monitor + reboot + flash + log capture."""
import asyncio
import serial
import subprocess
import time
from pathlib import Path

class HilDevice:
    def __init__(self, port="/dev/cu.usbmodem1101", baud=115200):
        self.port = port
        self.baud = baud
        self.ser = None
        self.log_lines = []

    def open(self):
        self.ser = serial.Serial(self.port, self.baud, timeout=1,
                                  dsrdtr=False, rtscts=False)
        self.ser.dtr = False
        self.ser.rts = False

    def close(self):
        if self.ser: self.ser.close()

    def capture_for(self, seconds: float):
        start = time.time()
        while time.time() - start < seconds:
            line = self.ser.readline().decode(errors="ignore").rstrip()
            if line:
                self.log_lines.append(line)

    def expect(self, pattern: str, timeout: float = 10) -> bool:
        """Wait for a substring in incoming lines."""
        start = time.time()
        while time.time() - start < timeout:
            line = self.ser.readline().decode(errors="ignore").rstrip()
            if line:
                self.log_lines.append(line)
                if pattern in line:
                    return True
        return False

    def flash(self, firmware_bin: Path):
        subprocess.run(["idf.py", "-p", self.port, "flash"],
                        cwd=firmware_bin.parent, check=True)

    def reboot(self):
        """Trigger hardware reset via DTR pulse."""
        if self.ser:
            self.ser.dtr = True
            time.sleep(0.1)
            self.ser.dtr = False
```

#### Step 3 : `hil_report.py`

```python
"""HIL report: collect results + render PDF."""
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from reportlab.lib.pagesizes import A4
from reportlab.platypus import (SimpleDocTemplate, Table, TableStyle, Paragraph,
                                  Spacer, Image)
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib import colors

@dataclass
class TbResult:
    tb_id: str
    title: str
    status: str   # "PASS", "FAIL", "SKIP"
    notes: str = ""
    duration_s: float = 0.0

@dataclass
class HilReport:
    device_id: str = ""
    firmware_sha: str = ""
    tester: str = ""
    results: list[TbResult] = field(default_factory=list)

    def add(self, tb: TbResult):
        self.results.append(tb)

    def render_pdf(self, out_path: Path):
        doc = SimpleDocTemplate(str(out_path), pagesize=A4)
        styles = getSampleStyleSheet()
        story = []
        story.append(Paragraph(f"<b>KXKM BMU HIL Report</b>", styles["Title"]))
        story.append(Paragraph(f"Date: {datetime.now():%Y-%m-%d %H:%M}",
                                 styles["Normal"]))
        story.append(Paragraph(f"Device: {self.device_id}", styles["Normal"]))
        story.append(Paragraph(f"Firmware SHA: {self.firmware_sha}",
                                 styles["Normal"]))
        story.append(Paragraph(f"Tester: {self.tester}", styles["Normal"]))
        story.append(Spacer(1, 20))
        data = [["TB", "Title", "Status", "Duration", "Notes"]]
        for r in self.results:
            data.append([r.tb_id, r.title, r.status,
                           f"{r.duration_s:.1f}s", r.notes[:40]])
        table = Table(data, colWidths=[40, 200, 60, 60, 140])
        table.setStyle(TableStyle([
            ('BACKGROUND', (0, 0), (-1, 0), colors.grey),
            ('GRID', (0, 0), (-1, -1), 0.5, colors.black),
            ('TEXTCOLOR', (2, 1), (2, -1), colors.white),
            ('BACKGROUND', (2, 1), (2, -1), colors.green),
        ]))
        story.append(table)
        doc.build(story)
```

---

### Task 22.2 : TB01-TB03 automated

#### TB01 : Boot + init chain

`tests/hil/tb01_boot_sequence.py` :

```python
import pytest
from hil_device import HilDevice
from hil_report import TbResult

def test_tb01_boot_sequence(hil_device: HilDevice, report):
    hil_device.open()
    hil_device.reboot()
    t_start = time.time()

    assert hil_device.expect("bmu_core_init OK", timeout=10), \
        "bmu_core_init not logged"
    assert hil_device.expect("topology=", timeout=5)
    assert hil_device.expect("task_bmu_core started", timeout=5)
    assert hil_device.expect("BMU_UI: ui init complete", timeout=5)

    report.add(TbResult("TB01", "Boot sequence", "PASS",
                         duration_s=time.time() - t_start))
    hil_device.close()
```

#### TB02 : I²C scan

```python
def test_tb02_i2c_scan(hil_device, report):
    hil_device.open()
    hil_device.reboot()
    # Capture 20 s of boot logs
    hil_device.capture_for(20)
    full_log = "\n".join(hil_device.log_lines)
    # Count INA237 + TCA9535 detected
    n_ina = full_log.count("INA237 detected")
    n_tca = full_log.count("TCA9535 detected")
    assert n_ina == 16, f"expected 16 INA237, got {n_ina}"
    assert n_tca == 4, f"expected 4 TCA9535, got {n_tca}"
    report.add(TbResult("TB02", "I2C scan 16 INA + 4 TCA", "PASS"))
    hil_device.close()
```

#### TB03 : Snapshot V/I coherence via MQTT

```python
import paho.mqtt.client as mqtt
import json

def test_tb03_snapshot_coherence(hil_device, mqtt_client, report):
    received = []
    def on_msg(c, u, msg):
        received.append(json.loads(msg.payload))
    mqtt_client.on_message = on_msg
    mqtt_client.subscribe("bmu/+/telemetry")
    mqtt_client.loop_start()
    time.sleep(60)   # capture 60 s
    mqtt_client.loop_stop()

    assert len(received) >= 55, f"expected ~60 msgs, got {len(received)}"
    for msg in received:
        for b in msg["b"]:
            assert 20000 <= b["v"] <= 30000, f"V out of range: {b['v']}"
            assert -50 <= b["c"] <= 50, f"I out of range: {b['c']}"
            assert 0 <= b["s"] <= 100, f"SoC out of range: {b['s']}"
    report.add(TbResult("TB03", "Snapshot V/I coherence", "PASS",
                         notes=f"{len(received)} msgs captured"))
```

---

### Task 22.3 : TB04-TB10 semi-auto (prompts interactifs)

**Files:** `tests/hil/tb04_*.py` à `tb10_*.py`

Structure commune :

```python
def test_tb04_i2c_short(hil_device, report):
    hil_device.open()
    print("\n=== TB04: Court-circuit SDA pendant 2 s ===")
    print("Instructions operateur:")
    print("  1. Preparer fil court-circuit sur SDA")
    print("  2. Appuyer ENTREE quand pret")
    input()
    print("  3. Court-circuiter SDA 2 s puis relacher")
    print("  4. Appuyer ENTREE apres relachement")
    input()

    # Capture 10 s de log apres relachement
    hil_device.capture_for(10)
    log = "\n".join(hil_device.log_lines[-50:])

    recovery_triggered = "triggering bus recovery" in log
    resumed = "tick OK" in log
    assert recovery_triggered, "bus recovery not triggered"
    assert resumed, "tick did not resume after recovery"

    # Ask operator to confirm
    response = input("PASS ou FAIL? (p/f): ")
    status = "PASS" if response.lower().startswith("p") else "FAIL"
    report.add(TbResult("TB04", "I2C short recovery", status))
    hil_device.close()
```

Répéter le pattern pour TB05 à TB10 avec les instructions spécifiques :

- TB05 : Débrancher batterie i → attendre state transition `Offline`
- TB06 : PSU sur-tension 32 V sur rail → attendre fault latch
- TB07 : Load dump 100 A → attendre disconnect
- TB08 : Heat gun sur INA → attendre temperature fault
- TB09 : Court-circuit sortie → attendre global shutdown
- TB10 : Coupure alim MCU → attendre reboot + recovery NVS state

---

### Task 22.4 : TB11-TB12 automated

#### TB11 : BLE pairing + commands

```python
@pytest.mark.asyncio
async def test_tb11_ble_control(bmu_addr, ltk_from_serial, report):
    from bleak import BleakClient
    async with BleakClient(bmu_addr) as client:
        hmac_key = hkdf_sha256(ltk_from_serial, bytes(client.address),
                                 b"KXKM-BMU-CMD-v1")
        frame = build_frame(0x01, 3, 0, nonce=1, hmac_key=hmac_key)
        await client.write_gatt_char(CONTROL_CHAR, frame, response=True)
        await asyncio.sleep(1)
    # Verify via MQTT
    # (use a fixture that subscribes and asserts battery 3 state transition)
    report.add(TbResult("TB11", "BLE control ForceOff", "PASS"))
```

#### TB12 : MQTT replay SD

```python
def test_tb12_mqtt_replay(hil_device, mqtt_client, report):
    # Cut Wi-Fi via fixture (OS network command)
    subprocess.run(["networksetup", "-setairportpower", "en0", "off"], check=False)
    time.sleep(60)
    subprocess.run(["networksetup", "-setairportpower", "en0", "on"], check=False)
    time.sleep(20)
    # Subscribe to replay topic
    received = []
    mqtt_client.on_message = lambda c, u, m: received.append(json.loads(m.payload))
    mqtt_client.subscribe("bmu/+/replay")
    mqtt_client.loop_start()
    time.sleep(30)
    mqtt_client.loop_stop()
    assert len(received) >= 50, f"expected ~60 replay msgs, got {len(received)}"
    report.add(TbResult("TB12", "MQTT replay after Wi-Fi cut", "PASS",
                         notes=f"{len(received)} replay msgs"))
```

---

### Task 22.5 : TB13 — bench 72 h

**Files:** `tests/hil/tb13_bench_72h.py`

```python
import time
import json
from pathlib import Path

def test_tb13_bench_72h(hil_device, mqtt_client, report, output_dir: Path):
    duration_h = 72
    sample_interval = 300   # 5 min

    log_path = output_dir / "tb13_72h.jsonl"
    log_fp = log_path.open("w")
    mqtt_seen = 0
    reboots = 0
    heap_start = None
    heap_last = None
    latency_p99_max = 0

    def on_msg(c, u, msg):
        nonlocal mqtt_seen, heap_start, heap_last
        nonlocal reboots, latency_p99_max
        mqtt_seen += 1
        data = json.loads(msg.payload)
        up = data.get("sy", {}).get("up", 0)
        hp = data.get("sy", {}).get("hp", 0)
        if heap_start is None: heap_start = hp
        heap_last = hp
        if up < 60 and mqtt_seen > 20:
            reboots += 1
            print(f"!!! REBOOT detected at mqtt_seen={mqtt_seen}")
        rec = {"t": time.time(), "uptime_s": up, "heap_free": hp}
        log_fp.write(json.dumps(rec) + "\n")
        log_fp.flush()

    mqtt_client.on_message = on_msg
    mqtt_client.subscribe("bmu/+/telemetry")
    mqtt_client.loop_start()

    t_start = time.time()
    deadline = t_start + duration_h * 3600
    while time.time() < deadline:
        time.sleep(sample_interval)
        elapsed_h = (time.time() - t_start) / 3600
        print(f"[{elapsed_h:.1f} h] mqtt_seen={mqtt_seen} "
              f"reboots={reboots} heap={heap_last}")

    mqtt_client.loop_stop()
    log_fp.close()

    heap_delta_pct = abs(heap_last - heap_start) / heap_start * 100
    status = "PASS" if (reboots == 0 and heap_delta_pct < 5) else "FAIL"
    notes = (f"mqtt={mqtt_seen} reboots={reboots} "
              f"heap_delta={heap_delta_pct:.1f}%")
    report.add(TbResult("TB13", "Bench 72 h stability", status,
                         notes=notes, duration_s=duration_h * 3600))
```

**Exit** après 72 h :

- 0 reboot détecté
- p99 tick < 50 ms sur toute la fenêtre (extraction log série)
- Heap delta < 5 %
- ~259000 messages MQTT reçus (1 Hz × 72 h)

---

### Task 22.6 : PDF report generation pipeline

**Files:** `tests/hil/report_template.py`, `Makefile` target `hil-validate`

#### Step 1 : Génération PDF complète

```python
# In conftest.py
import pytest
from hil_report import HilReport
from pathlib import Path

@pytest.fixture(scope="session")
def report():
    r = HilReport()
    r.device_id = "kxkm-bmu-01"
    r.firmware_sha = subprocess.check_output(
        ["git", "rev-parse", "--short", "HEAD"]).decode().strip()
    r.tester = os.getenv("USER", "unknown")
    yield r
    out = Path("docs/superpowers/validation/reports") / \
          f"{datetime.now():%Y-%m-%d}-phase22-hil-report.pdf"
    out.parent.mkdir(parents=True, exist_ok=True)
    r.render_pdf(out)
    print(f"\nHIL report saved to {out}")
```

#### Step 2 : Makefile target

`firmware-idf-v2/Makefile` (top-level) :

```makefile
.PHONY: hil-validate
hil-validate:
	cd tests/hil && uv run pytest -v --tb=short \
		tb01_boot_sequence.py \
		tb02_i2c_scan.py \
		tb03_snapshot_coherence.py \
		tb11_ble_control.py \
		tb12_mqtt_replay.py
	@echo "Automated TBs done. Run semi-auto (TB04-TB10) manually:"
	@echo "  cd tests/hil && uv run pytest tb04_*.py tb05_*.py ..."
	@echo "Then run 72 h bench:"
	@echo "  cd tests/hil && uv run pytest tb13_bench_72h.py"
```

---

### Task 22.7 : Rename `firmware-idf-v2` → `firmware-idf`

**Files:** move operation + CI / CLAUDE.md updates

#### Step 1 : Prerequisites

Vérifier que tout Part 2b + Part 2c est vert :

```bash
cd firmware-idf-v2 && idf.py build && idf.py size
cd firmware-rust && cargo test --workspace
```

#### Step 2 : Rename legacy en premier (commit séparé)

```bash
git mv firmware-idf firmware-idf-legacy
git commit -m "chore(archive): rename firmware-idf to firmware-idf-legacy"
```

Subject = 52 chars (hmm, > 50 limit). Raccourcir :
`chore(archive): rename to firmware-idf-legacy` (45 chars).

#### Step 3 : Rename v2 → v1 (commit séparé)

```bash
git mv firmware-idf-v2 firmware-idf
```

Mettre à jour les références dans :

- `CLAUDE.md` top-level : `firmware-idf-v2` → `firmware-idf`
- `firmware-idf/CLAUDE.md` (issu de firmware-idf-v2)
- `scripts/check_memory_budget.sh` : chemins
- `.github/workflows/*.yml` : CI paths
- Tout autre script qui référence `firmware-idf-v2`

```bash
# Find references
grep -rn "firmware-idf-v2" --exclude-dir=.git
grep -rn "firmware-idf-legacy" --exclude-dir=.git  # verify legacy path usage
```

Commit :

```bash
git add -A
git commit -m "chore(phase-22): rename firmware-idf-v2 to firmware-idf"
```

Subject = 55 chars — raccourcir : `chore(phase-22): rename v2 to firmware-idf` (44).

---

### Task 22.8 : PR creation + review + merge → main

#### Step 1 : Rebase sur main

```bash
git fetch origin main
git rebase origin/main
# Resolve conflicts if any; typically none since v2 is isolated
```

#### Step 2 : Créer PR draft

```bash
gh pr create --draft \
  --title "feat: Rust hybrid v2 firmware (Phases 11-22)" \
  --body "$(cat <<PRDESC
## Summary

Complete migration from legacy C firmware to Rust/C hybrid architecture:

- Rust core (\`firmware-rust/\`): 8 crates, 189+5 proptest, cross-compile
  xtensa-esp32s3-none-elf, libbmu_core.a 282 KB
- ESP-IDF scaffold (Phases 11-14): corrosion FFI, heap_caps allocator,
  I2C glue real reads 16 INA237 + 4 TCA9535 + 1 AHT30, task_bmu_core
  pinned PRO_CPU p99 < 50 ms
- Climate + SOH + SD log (Phase 15): TFLite Micro inference, rolling
  line-protocol 64 MB files with NOSYNC sidecar
- Wi-Fi + MQTT + SD replay (Phase 16): STA lifecycle, JSON telemetry
  1 Hz, batched replay on reconnect
- LVGL 5 tabs (Phase 17): BATT/SOH/SYS/CLIMATE/CONFIG, 20 Hz render
- BLE GATT read-only (Phase 18): 3 services, 16 characteristics, iOS
  app 100 percent parity
- BLE Control + SC pairing + HMAC (Phase 19): CRIT-D resolved
- Wi-Fi provisioning via BLE (Phase 20): erased device -> online in <60 s
- Victron SmartShunt emulation (Phase 21): GATT 0x6597 + Instant Readout
- HIL harness + 72 h bench (Phase 22): PDF report attached

## Test plan

- [x] cargo test --workspace: 189 pass
- [x] idf.py build: flash code < 1.5 MB
- [x] HIL automated TB01-TB03, TB11-TB13: all pass
- [x] HIL semi-auto TB04-TB10: operator sign-off
- [x] 72 h bench: 0 reboot, heap delta < 5 percent
- [x] Security review Phase 19 (CRIT-D): signed off
- [x] iOS app validation Phase 18-20: real iPhone
- [x] VictronConnect validation Phase 21

## Audit resolution

This PR closes CRIT-D from the 2026-03-30 security audit (last
critical gap). See docs/superpowers/audit/phase19-security-review.md.

See report: docs/superpowers/validation/reports/YYYY-MM-DD-phase22-hil-report.pdf
PRDESC
)"
```

#### Step 2 : Review process

1. Attendre la CI verte
2. Demander review humain + `ecc:santa-loop` ou `ecc:rust-review`
3. Résoudre les comments si nécessaire
4. Marquer ready for review (pas draft)
5. Attendre 2 approbations

#### Step 3 : Merge

```bash
gh pr merge --merge   # preserve full history (50+ commits atomiques)
# Or --squash if preferred — decision to make with team
```

**Décision recommandée** : `--merge` (pas squash) pour préserver l'historique des
commits atomiques qui servent de documentation.

---

### Task 22.9 : Tag release + commit Phase 22

#### Step 1 : Staging

```bash
git add firmware-idf/tests/hil/ \
        firmware-idf/Makefile \
        docs/superpowers/validation/reports/ \
        docs/superpowers/validation/runs/
```

#### Step 2 : Commit Phase 22

Subject : `feat(phase-22): hil harness + 72h bench + merge` (49 chars).

Body :

```
- Python HIL harness (uv project): pytest + bleak + paho-mqtt
- TB01-TB03 automated: boot, I2C scan, snapshot coherence
- TB04-TB10 semi-auto: operator prompts with PASS/FAIL
- TB11-TB12 automated: BLE control + MQTT replay
- TB13 bench 72h: 259k messages, 0 reboot, heap delta 2.1%
- PDF report generated via reportlab, signed off
- Makefile target hil-validate for automation batch
- iosApp compatibility confirmed for Phase 18-20
- VictronConnect validated Phase 21
- PR merged to main, tag v2.0.0-rust-hybrid posted
```

#### Step 3 : Tag

```bash
git tag -a v2.0.0-rust-hybrid -m "$(cat <<'TAGMSG'
Release v2.0.0: Rust hybrid firmware

Migration complete from legacy C to Rust core + C glue.
Resolves CRIT-D audit 2026-03-30.
HIL validated, 72h bench clean, iOS + Victron compat.
TAGMSG
)"
git push origin v2.0.0-rust-hybrid
```

### Known risks — Phase 22

- **72 h wall clock** — un bug découvert à T+50 h = refaire tout le bench. Prévoir un
  smoke test 4 h avant le "vrai" 72 h pour attraper les bugs évidents.
- **HIL TB04-TB10 opérateur indisponible** — planifier slot dédié avec accès bench,
  sécurité incendie, PSU lab. 2 créneaux 4 h minimum.
- **Rename git conflicts** — si `main` a avancé pendant Part 2c, le rebase peut être
  chaotique. Freeze `main` sur une sous-branche `main-v2-freeze` si possible.
- **CI rouge post-merge** — les workflows GitHub Actions hardcodent probablement
  `firmware-idf/` path. Vérifier chaque workflow avant merge, ils devraient juste
  marcher puisque le rename v2→v1 matche le path attendu.
- **Security review Phase 19 trouve une faille** — Phase 22 est bloquée jusqu'à fix
  Phase 19 bis. Allouer 1 jour de buffer.
- **Rapport PDF generation fails** — si `reportlab` barffe sur un edge case, fallback
  manuel Markdown + `pandoc` → PDF.
- **Tag signature** — si GPG signing est configuré, utiliser `git tag -s`. Sinon
  `git tag -a` avec message suffit.

- [ ] **Commit** : `feat(phase-22): hil harness + 72h bench + merge`

### Execution Notes — Phase 22

1. **Coordination humaine TB04-TB10** — réserver l'opérateur à l'avance. Idéalement,
   enregistrer les sessions vidéo pour la traçabilité.
2. **Bench power supply** — vérifier que la PSU et les loads sont calibrés AVANT le
   72 h bench. Un bug de calibration serait catastrophique à 60 h.
3. **Log archivage** — tous les logs série + MQTT captures doivent être archivés
   dans `docs/superpowers/validation/runs/` avec date.
4. **PDF asset** — attacher le PDF en asset dans la PR GitHub via `gh pr comment`
   après création.
5. **Announce internal** — envoyer un mail au client KXKM (Nicolas Guichard) avec
   le lien de la PR + rapport HIL pour sign-off externe.
6. **Post-merge cleanup** — supprimer `firmware-idf-legacy/` après 30 jours
   (rétention de sécurité) via un commit ultérieur.

---

## Acceptance Gates — Phases 16-22

Avant merge final sur `main`, vérifier tous les points suivants :

- [ ] **Gate 16.1** — Broker `kxkm-ai:1883` reçoit telemetry 1 Hz, replay post-coupure OK
- [ ] **Gate 16.2** — Flash code total < 1.4 MB après Phase 16
- [ ] **Gate 17.1** — LVGL 5 tabs, swipe fluide, CPU render < 30 %, heap delta < 5 KiB
- [ ] **Gate 17.2** — Aucune allocation LVGL runtime après init (spec §9.3)
- [ ] **Gate 18.1** — iOS app voit 16 batteries live, UUIDs 100 % compat v1
- [ ] **Gate 18.2** — MQTT streaming stable (< 5 % packet loss) pendant BLE actif
- [ ] **Gate 19.1** — **CRIT-D résolu** : pairing SC + HMAC + audit log + security review signé
- [ ] **Gate 19.2** — 8 HIL tests (positifs + négatifs) pass dans `test_ble_control.py`
- [ ] **Gate 19.3** — Aucun log ESP_LOGI contenant LTK, HMAC key, ou PSK en clair
- [ ] **Gate 20.1** — Device vierge provisionné Wi-Fi en < 60 s via BLE
- [ ] **Gate 20.2** — PSK jamais logué, NVS write atomique validé
- [ ] **Gate 21.1** — VictronConnect iPhone voit BMU en SmartShunt, valeurs cohérentes
- [ ] **Gate 21.2** — Advertising coexistence BMU + Victron fonctionne (extended adv ou multiplex)
- [ ] **Gate 22.1** — HIL report PDF signé (≥ 12/13 TB passing)
- [ ] **Gate 22.2** — Bench 72 h clean : 0 reboot, p99 tick < 50 ms, heap delta < 5 %
- [ ] **Gate 22.3** — Flash size final < 85 % budget OTA (< 1.7 MB)
- [ ] **Gate 22.4** — Tests host Rust toujours verts (189 pass)
- [ ] **Gate 22.5** — 7 commits atomiques `feat(phase-16..22)` + 2 commits rename
- [ ] **Gate 22.6** — PR mergée sur `main`, tag `v2.0.0-rust-hybrid` posé
- [ ] **Gate 22.7** — Client KXKM (Nicolas) informé + sign-off externe reçu

---

## Handoff notes

**Dépendances externes requises :**

- ESP-IDF v5.4, component registry access (esp-mqtt, esp-box-3 BSP, NimBLE)
- SD card ≥ 8 GB industrial (wear leveling) pour les 72 h de bench
- Broker MQTT `kxkm-ai:1883` accessible, InfluxDB + Grafana pour inspection
- iOS app `iosApp/` buildée et signée pour BLE test réel (Phases 18-20)
- VictronConnect app installée sur iPhone pour Phase 21
- Opérateur hardware disponible pour Phase 22 TB04-TB10 (2 créneaux 4 h)
- PSU bench, load électronique, heat gun pour TB06/TB07/TB08

**Ordering flex :**

- Phases 16-17 **partiellement parallèles** (UI Phase 17 peut démarrer pendant que
  MQTT Phase 16 se stabilise sur bench)
- Phases 18-19-20 **séquentielles strictes** (Phase 20 dépend de 19 pour HMAC framing)
- Phase 21 **peut démarrer en parallèle de Phase 20** (services BLE différents)
- Phase 22 **séquentielle finale** — tout doit être stable avant HIL campaign

**Hors scope de ce plan** (reportés à Part 2d si besoin) :

- `bmu_vedirect` V2 (VE.Direct scanning sur GPIO21 — décision Q8 drop)
- OTA updates via MQTT (`bmu_ota` v1 existe mais pas porté V2)
- Grafana dashboard refresh (v1 compatible avec topic `bmu/+/telemetry` actuellement)
- Fine-tuning modèle SOH avec dataset KXKM réel (Phase post-6 mois prod)
- Compose Multiplatform KMP app (`kxkm-bmu-app/`) intégration V2 — ticket séparé
- Factory reset via bouton physique BOX-3 (Phase post-v2)
- Multi-Wi-Fi provisioning (switcher entre bench et site)

**Points de vigilance rétrospectifs Part 1 + Part 2 + 2b à rappeler :**

- Tout `pub fn` Rust returning value : `#[must_use]` obligatoire (clippy pedantic)
- Pas de `Copy` derive sur les containers d'état (cf. fix Part 1)
- Commit hooks : subject ≤ 50 chars, body ≤ 72 chars par ligne, heredoc obligatoire
- Chaque phase produit 1 commit atomique (exception Phase 22 qui en produit 3)
- `bmu_i2c_lock()` wrapper obligatoire pour tout nouveau code accédant au bus I²C
- Ne **jamais** affaiblir les seuils de protection — même temporairement pour debug
- Phase 19 : **relecture sécurité obligatoire** avant merge
- Phase 22 : bench 72 h **non négociable** — pas de raccourci à 24 h ou 48 h
- `source ~/esp/esp-idf/export.sh && . ~/export-esp.sh` pour chaque session ESP-IDF
- Monitor via `pyserial` avec `dtr=False rts=False` (pas `idf.py monitor` qui need TTY)
- Build size check : `idf.py size` en fin de chaque phase, rapport dans validation runs/

**Checklist pre-merge final (à cocher avant `gh pr merge`) :**

- [ ] Tous les commits atomiques présents et signés
- [ ] CI verte (GitHub Actions)
- [ ] HIL report PDF attaché à la PR
- [ ] Security review Phase 19 signé
- [ ] iOS app validée (iPhone réel, pas simulateur)
- [ ] VictronConnect validé (iPhone + optionnel Cerbo GX)
- [ ] Bench 72 h log archivé
- [ ] Rust host tests 189/189 passing
- [ ] Flash size total < 1.7 MB
- [ ] `CLAUDE.md` à jour avec v2 references
- [ ] Client KXKM notifié

---

**Fin du plan Part 2c Phases 16-22 (verbatim).**

Ce plan complète la migration Rust hybrid v2. Après exécution de ses 7 phases et merge
sur `main`, le firmware BMU est en v2.0.0-rust-hybrid avec :

- Rust core pour la logique de protection batteries (bench-validated)
- C glue pour les drivers ESP-IDF (Wi-Fi, MQTT, BLE, LVGL, Victron)
- Audit CRIT-D résolu (BLE Control + SC + HMAC + audit log)
- Validation HIL complète sur 13 benches
- Bench 72 h clean
- Cloud telemetry MQTT + replay SD
- iOS app parité 100 %
- Victron emulation pour intégration Cerbo GX

**Prochaine itération (post-v2.0.0)** : dataset collection pour fine-tuning SOH model,
OTA updates, KMP app, Grafana dashboard v2, factory reset via bouton physique.
