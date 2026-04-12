# Plan Part 2b — BMU Rust Hybrid V2 — Phases 15-22 (Outline)

> **Ce plan est la continuation de Part 2 (Phases 11-14).** Il couvre les phases 15 à 22 de
> l'intégration ESP-IDF du firmware Rust hybrid V2, de l'ajout des composants climate/SOH/SD
> jusqu'au merge final sur `main` après bench 72 h et campagne HIL TB01-TB13.
>
> **Niveau de détail** : outline léger — description des tâches, fichiers créés/modifiés, étapes
> clefs et critères de sortie. Les détails d'implémentation (code verbatim) seront rédigés au
> moment de l'exécution de chaque phase, en s'appuyant massivement sur le code v1 archivé dans
> `firmware-idf/components/` qui sert de base copie-collée adaptée pour V2.
>
> **Branche** : `feat/rust-hybrid-v2` (continuation — 35+ commits post Part 2 Phases 11-14).
> **Spec autoritative** : `docs/superpowers/specs/2026-04-09-bmu-rust-hybrid-v2-design.md`
> (sections §6.3 SOH TFLite, §7 BLE GATT, §8 Wi-Fi/MQTT/SD, §9 LVGL, §10.4 HIL, §12 steps 14-21).

---

## Héritages critiques depuis Part 2 (Phases 11-14)

Avant d'entamer Phase 15, les 10 acceptance gates de Part 2 doivent être vertes :

- ✅ `firmware-idf-v2/` compile et flashe, splash LVGL visible
- ✅ `bmu_core_rs` intégré via corrosion, allocator `heap_caps` opérationnel
- ✅ `bmu_i2c_glue` lit 16 INA237 + 4 TCA9535 + 1 AHT30 en raw
- ✅ `task_bmu_core` pinné PRO_CPU, WDT 3 s, bench 1 h p99 ≤ 50 ms
- ✅ Snapshot Rust cohérent avec archive v1 (delta ≤ 1 % V, ≤ 5 % I)

Ce que Part 2b reprend de Part 2 :

- Le bus I²C déjà géré par `bmu_i2c_glue` — les composants climate/SOH/BLE passent par lui ou
  par un mutex dédié `bmu_i2c_lock` (wrapper FreeRTOS).
- L'allocator `heap_caps_malloc` — tous les nouveaux composants l'utilisent automatiquement.
- Le `BmuSnapshot` exposé via `bmu_core_snapshot()` — source unique de vérité read-only pour
  LVGL, BLE, MQTT, SD log, Victron emulation.
- Le scheduling `task_bmu_core` à 5 Hz — les tâches secondaires (climate 0.1 Hz, SOH 0.1 Hz,
  LVGL 20 Hz, MQTT 1 Hz) s'ajoutent sans toucher au chemin critique.

---

## Execution Notes & Known Deviations (Part 2b)

Déviations spécifiques à cette section du plan, à valider avec le user avant exécution :

1. **VE.Direct drop-in Q8** — la spec §8 mentionne un composant `bmu_vedirect` V2, mais la
   sub-spec Victron de Q8 a retiré le scan VE.Direct du roadmap (GPIO21 RX-only dédié SmartSolar
   monitoring externe). **Phase 15 ne portera pas `bmu_vedirect`**. Si besoin, un plan Part 2c
   dédié sera ajouté post-bench 72 h.

2. **Wi-Fi provisioning en Phase 16 = NVS direct** — on utilise un premier jet `credentials.h`
   flashé manuellement ou une écriture NVS via `idf.py nvs-partition-gen`. **La provisioning via
   BLE Command `WifiProv` arrive en Phase 20** et remplace ce mécanisme temporaire.

3. **Commit-par-phase** (identique Part 2) — pas de commit intermédiaire par task. Chaque phase
   produit 1 commit `feat(phase-NN): <subject ≤ 50 chars>`.

4. **TFLite Micro = composant managed** (`esp-tflite-micro` du component registry) — pas de
   vendorisation. Pin version dans `idf_component.yml`. Fallback vendorisation si version cassée.

5. **BLE stack = NimBLE** (pas Bluedroid). Héritage archive v1 qui utilise NimBLE depuis la
   migration Phase 3 originelle. Économise ~60 KB flash vs Bluedroid.

6. **HIL TB04-TB10 non automatables** — ces test benches exigent une intervention humaine
   (court-circuit volontaire, débranchement batterie, surchauffe forcée). Le Python harness
   couvre TB01-TB03 + TB11-TB13 automatiquement et fournit des prompts interactifs pour TB04-TB10.

7. **Rename `firmware-idf-v2` → `firmware-idf`** en fin de Phase 22, dans un commit dédié
   `chore(phase-22): rename firmware-idf-v2 → firmware-idf`. Git détecte le rename via similarity,
   l'archive v1 est déplacée en `firmware-idf-legacy/` en amont (commit séparé).

8. **Bench 72 h vs 1 h Phase 14** — la phase 22 exige **72 h continues** sans reboot sur bench
   complet (16 batteries connectées, charge + décharge automatisées via relay test jig). Aucun
   déviation tolérée sur cette métrique.

---

## File Structure Overview — Part 2b delta

```
firmware-idf-v2/
├── components/
│   ├── bmu_core_rs/              # Part 2 — inchangé
│   ├── bmu_i2c_glue/             # Part 2 — inchangé (mais thread-safe wrapper ajouté Phase 15)
│   ├── bmu_climate/              # Phase 15 — nouveau (copie v1 adaptée)
│   ├── bmu_soh/                  # Phase 15 — nouveau (TFLite Micro wrapper)
│   ├── bmu_sd_log/               # Phase 15 — nouveau (line-protocol rolling)
│   ├── bmu_wifi/                 # Phase 16 — nouveau (copie v1)
│   ├── bmu_mqtt/                 # Phase 16 — nouveau (copie v1 + replay SD)
│   ├── bmu_ui/                   # Phase 17 — nouveau (LVGL 5 tabs)
│   ├── bmu_ble/                  # Phase 18-20 — nouveau (NimBLE read-only → Control → Prov)
│   ├── bmu_ble_victron/          # Phase 21 — nouveau (copie v1 GATT + scan)
│   └── bmu_hil_stub/             # Phase 22 — optionnel, stub hooks pour HIL harness
├── main/
│   ├── main.cpp                  # init chain étendu progressivement
│   ├── task_bmu_core.cpp         # Part 2 — inchangé
│   ├── task_climate.cpp          # Phase 15
│   ├── task_soh.cpp              # Phase 15
│   ├── task_sd_log.cpp           # Phase 15
│   ├── task_mqtt.cpp             # Phase 16
│   ├── task_ui.cpp               # Phase 17
│   ├── task_ble.cpp              # Phase 18
│   └── task_victron.cpp          # Phase 21
├── models/
│   └── fpnn_soh_v3_int8.tflite   # Phase 15 — vendoré depuis models/ racine via EMBED_FILES
├── tests/hil/                    # Phase 22 — Python harness
│   ├── tb01_boot.py
│   ├── tb02_scan_i2c.py
│   ├── ...
│   └── report_gen.py
└── docs/superpowers/validation/
    ├── runs/YYYY-MM-DD-phase15-soh-bench.log
    ├── runs/YYYY-MM-DD-phase22-72h-bench.log
    └── reports/phase22-hil-report.pdf
```

---

## Phase Plan — Summary Table

| # | Phase | Tasks | Exit criterion | Risk | Commit subject |
|---|---|---|---|---|---|
| **15** | `bmu_climate` + `bmu_soh` + `bmu_sd_log` | 15.1 → 15.6 | 3 composants intégrés, SD produit `.lp`, inference TFLite < 100 ms | moyen | `feat(phase-15): climate/soh/sdlog components` |
| **16** | `bmu_wifi` + `bmu_mqtt` + SD replay | 16.1 → 16.5 | Telemetry visible broker, replay post-coupure OK | moyen | `feat(phase-16): wifi + mqtt + sd replay` |
| **17** | LVGL 5 tabs (BATT/SOH/SYS/CLIMATE/CONFIG) | 17.1 → 17.6 | Display cohérent avec MQTT, swipe fluide 20 Hz render | moyen | `feat(phase-17): lvgl 5 tabs read-only ui` |
| **18** | BLE GATT read-only (Battery+System+Config) | 18.1 → 18.5 | iOS app voit 16 batteries live en BLE | moyen | `feat(phase-18): ble gatt read-only services` |
| **19** | BLE Control + pairing SC + HMAC audit | 19.1 → 19.7 | iOS peut commander, audit log SD, CRIT-D résolu | **TRÈS ÉLEVÉ (sécurité)** | `feat(phase-19): ble control + sc pairing + hmac` |
| **20** | Wi-Fi provisioning via BLE Command | 20.1 → 20.4 | Device vierge + iOS → Wi-Fi configuré sans serial | moyen | `feat(phase-20): ble wifi provisioning cmd` |
| **21** | Victron SmartShunt emulation | 21.1 → 21.5 | VictronConnect voit BMU comme SmartShunt | moyen | `feat(phase-21): victron smartshunt emulation` |
| **22** | HIL TB01-TB13 + bench 72 h + merge | 22.1 → 22.8 | HIL report signé, 72 h clean, PR mergée | élevé | `feat(phase-22): hil harness + 72h bench + merge` |

**Total tasks** : ~42. **Durée estimée** : 10-15 jours focalisés avec bench disponible en
permanence à partir de Phase 18 (BLE exige iOS réel).

---

# Phase 15 — `bmu_climate` + `bmu_soh` + `bmu_sd_log`

**Objectif** : ajouter les 3 composants secondaires de lecture/écriture (climat AHT30 agrégé,
inférence SOH via TFLite Micro, log SD line-protocol rolling). Ces 3 composants sont
**parallélisables** entre eux — aucune dépendance croisée — et consomment tous le `BmuSnapshot`
exposé par `bmu_core_snapshot()`.

**Durée estimée** : 2-3 jours. **Risk** : moyen (TFLite Micro peut poser problème de taille).
**Acceptance** : log série montre `climate tick`, `soh inference OK <latency>ms`, `sd_log
written N lines to bmu_YYYYMMDD.lp`.

### Heritage v1

| Composant | Source v1 | Réutilisation | Notes |
|---|---|---|---|
| `bmu_climate` | `firmware-idf/components/bmu_climate/` | **~80 % copie** | Retirer lecture I²C directe (AHT30 déjà lu par `bmu_i2c_glue`), garder agrégation stats (min/max/avg/hum) |
| `bmu_soh` | `firmware-idf/components/bmu_soh/` | **~60 % copie** | TFLite Micro wrapper réutilisable ; adapter input tensor pour lire depuis `BmuSnapshot` plutôt que struct C legacy |
| `bmu_sd_log` | `firmware-idf/components/bmu_storage/` | **~50 % copie** | Le v1 `bmu_storage` est plus large ; on en extrait juste la rolling line-protocol writer + NOSYNC cursor |

### Task 15.1 : `bmu_climate` — extraction stats depuis snapshot Rust

**Files:**
- Create: `firmware-idf-v2/components/bmu_climate/CMakeLists.txt`
- Create: `firmware-idf-v2/components/bmu_climate/include/bmu_climate.h`
- Create: `firmware-idf-v2/components/bmu_climate/src/bmu_climate.c`
- Create: `firmware-idf-v2/main/task_climate.cpp`

- Copier `bmu_climate/` v1 comme base de départ
- Supprimer toute lecture I²C directe — AHT30 est déjà lu par `bmu_i2c_glue` et exposé dans
  `BmuRawInputs.aht30_raw`
- Exposer 3 fonctions : `bmu_climate_init()`, `bmu_climate_update(const BmuSnapshot*)`, `bmu_climate_get_stats(BmuClimateStats*)`
- Stats : min/max/avg température MCU + ambient + humidité sur fenêtre glissante 60 s
- Task `task_climate` 0.1 Hz (10 s period), prio 3, core 1, stack 4 KiB
- Test : log `climate: T_box=25.1°C Hum=45% T_mcu=32.0°C` toutes les 10 s

**Dépendance** : Phase 14 `task_bmu_core` doit tourner — `bmu_core_snapshot()` est la source.

### Task 15.2 : `bmu_soh` — TFLite Micro + `fpnn_soh_v3_int8.tflite`

**Files:**
- Create: `firmware-idf-v2/components/bmu_soh/CMakeLists.txt` (avec `EMBED_FILES models/fpnn_soh_v3_int8.tflite`)
- Create: `firmware-idf-v2/components/bmu_soh/include/bmu_soh.h`
- Create: `firmware-idf-v2/components/bmu_soh/src/bmu_soh.cpp` (C++ pour l'API TFLite)
- Create: `firmware-idf-v2/models/fpnn_soh_v3_int8.tflite` (copie de `models/fpnn_soh_v3_int8.tflite`)
- Modify: `firmware-idf-v2/main/idf_component.yml` (add `espressif/esp-tflite-micro`)
- Create: `firmware-idf-v2/main/task_soh.cpp`

- Pin `esp-tflite-micro` dans `idf_component.yml` (version stable ≥ 1.3)
- Allocator tensor arena : 8 KiB static `alignas(16) uint8_t tensor_arena[8192]`
- Input tensor : 5 features × 16 batteries int8 quantisé (spec §6.3) — construits depuis
  `BmuSnapshot` (V, I, cycles, cumul_ah, temp) avec quantisation maison (scale/zero_point connus)
- Output : 16 × SOH float 0-1, désquantisé et poussé dans `bmu_soh_set_values(float*, 16)`
  qui fournit une API `bmu_soh_get(uint8_t idx) -> float`
- Task `task_soh` 0.1 Hz, prio 2, core 1, stack 16 KiB (TFLite heavy)
- Mesurer latence inférence avec `esp_timer_get_time()` ; log à chaque tick
- **Test** : sur bench, log montre `soh inference OK 42ms soh[0]=0.98 soh[15]=0.87`
- **Exit latency** : < 100 ms (spec §6.3)

**Risque** : taille binaire TFLite Micro ~200 KB. Si flash dépasse 85 %, retirer debug logs ou
envisager compilation size-optimized. Mesurer flash size avant/après ce task.

### Task 15.3 : `bmu_sd_log` — rolling line-protocol 1 GB + NOSYNC cursor

**Files:**
- Create: `firmware-idf-v2/components/bmu_sd_log/CMakeLists.txt`
- Create: `firmware-idf-v2/components/bmu_sd_log/include/bmu_sd_log.h`
- Create: `firmware-idf-v2/components/bmu_sd_log/src/bmu_sd_log.c`
- Create: `firmware-idf-v2/main/task_sd_log.cpp`

- Mount SD via BSP `esp-box-3` (SPI interface configurée par BSP)
- Format ligne : `bmu,device=<id>,battery=<n> v=<mV>i,i=<mA>i,soc=<%>i <timestamp_ns>`
- Rolling : fichiers `bmu_YYYYMMDD_HHMMSS.lp` de 64 MB max, total directory < 1 GB, FIFO rotation
- Sidecar `.nosync` : fichier par .lp contenant offset byte du dernier record MQTT-ACK. Remis à
  0 à la création, mis à jour par `bmu_sd_log_mark_sent(offset)` depuis `task_mqtt` (Phase 16)
- Curseur rehydrate au boot : scan sidecars, position reader au 1er .lp avec offset < filesize
- Task `task_sd_log` 1 Hz, prio 4, core 1, stack 4 KiB. Écrit batch de 16 lignes (1 par battery)
  à chaque tick — `fflush` + `fsync` groupé pour limiter wear SD
- **Test** : après 60 s de run, `ls /sdcard/*.lp` montre fichier > 0, contenu parseable
  `head -5 /sdcard/bmu_*.lp`

### Task 15.4 : Intégration dans `main.cpp`

**Files:**
- Modify: `firmware-idf-v2/main/main.cpp`
- Modify: `firmware-idf-v2/main/CMakeLists.txt`

- Ajouter init sequence après `task_bmu_core_start()` :
  `bmu_climate_init()` → `bmu_soh_init()` → `bmu_sd_log_init(/sdcard)` → start des 3 tasks
- Logs boot enrichis : heap free avant/après chaque init, flash size estimée
- `CMakeLists.txt` main : ajout `task_climate.cpp task_soh.cpp task_sd_log.cpp` dans SRCS
- Top-level `CMakeLists.txt` : `EXTRA_COMPONENT_DIRS` déjà OK via Phase 11
- **Test** : `idf.py monitor` après flash montre séquence boot complète, heap free ≥ 150 KB
  après init de tous les tasks

### Task 15.5 : Smoke test bench

**Files:** aucun (exécution / mesure)

- Flasher sur BOX-3 + SD card 8 GB formaté FAT32
- Laisser tourner 15 min
- Vérifier : (1) climate stats cohérents, (2) SOH inference sans panic, (3) SD log écrit
  lignes valides line-protocol, (4) pas de WDT reset, (5) heap stable
- Archiver log dans `docs/superpowers/validation/runs/YYYY-MM-DD-phase15-smoke.log`

### Task 15.6 : Commit Phase 15

- Subject : `feat(phase-15): climate/soh/sdlog components` (48 chars ✓)
- Body :
  - `bmu_climate`: stats min/max/avg from BmuSnapshot, task 0.1 Hz
  - `bmu_soh`: TFLite Micro wrapper, fpnn_soh_v3_int8 (12 KB), p99 < 100 ms
  - `bmu_sd_log`: rolling 64 MB * 16 line-protocol files, NOSYNC sidecar
  - Integrated in main init chain, smoke test 15 min clean

### Known risks — Phase 15

- **TFLite Micro flash bloat** — si binaire dépasse 1.2 MB, CI size gate échoue. Mitigation :
  `-Os` override, strip debug symbols ESP, retirer composants inutilisés.
- **SD card wear** — écriture 1 Hz × 16 lignes = ~5 MB/h. Sur 72 h = 360 MB. Rolling OK, mais
  wear leveling FAT32 est naïf ; utiliser `wear_levelling` component si SD non-industriel.
- **`i2c_lock` contention** — AHT30 n'est plus lu ici (délégué à `bmu_i2c_glue`), donc pas de
  contention. Mais si futur besoin lecture direct, passer par `bmu_i2c_lock()` wrapper.

- [ ] **Commit** : `feat(phase-15): climate/soh/sdlog components`

---

# Phase 16 — `bmu_wifi` + `bmu_mqtt` + SD replay

**Objectif** : connecter le firmware au cloud KXKM (`kxkm-ai:1883`), publier telemetry JSON à
1 Hz, implémenter le replay SD → MQTT lorsque la connexion revient après une coupure.
Provisioning Wi-Fi via NVS pré-flashé ou `credentials.h` (provisioning BLE arrive Phase 20).

**Durée estimée** : 1-2 jours. **Risk** : moyen (replay logic est subtile).
**Acceptance** : broker MQTT sur `kxkm-ai:1883` reçoit `bmu/<device_id>/telemetry` à 1 Hz ;
après coupure Wi-Fi de 60 s, le replay drain les fichiers NOSYNC et les records manquants
arrivent dans l'ordre sur le broker.

### Heritage v1

| Composant | Source v1 | Réutilisation |
|---|---|---|
| `bmu_wifi` | `firmware-idf/components/bmu_wifi/` | **~90 % copie** — init STA mode, reconnect logic, event handlers. Adapter pour lire config depuis NVS au lieu de `credentials.h`. |
| `bmu_mqtt` | `firmware-idf/components/bmu_mqtt/` | **~70 % copie** — `esp-mqtt` client setup. Réécrire callback publish pour sérialiser `BmuSnapshot` en JSON. |
| SD replay | nouveau | Pas d'équivalent v1 (v1 écrivait en direct InfluxDB HTTP). |

### Task 16.1 : `bmu_wifi` — STA mode avec NVS config

**Files:**
- Create: `firmware-idf-v2/components/bmu_wifi/{CMakeLists.txt,include/bmu_wifi.h,src/bmu_wifi.c}`

- Copier `bmu_wifi` v1 comme base
- Supprimer dépendance à `credentials.h` ; lire SSID/password depuis NVS namespace `wifi_creds`
  avec keys `ssid` et `psk`
- Si NVS vide au boot, logger warning et rester offline (Phase 20 ajoutera BLE prov)
- Event handlers : `WIFI_EVENT_STA_DISCONNECTED` → retry avec backoff 1/2/5/10 s, cap 10 s
- Expose API : `bmu_wifi_init()`, `bmu_wifi_is_connected() -> bool`, `bmu_wifi_get_ip(char*)`
- **Test** : flash device avec NVS pré-rempli (`idf.py nvs-partition-gen`), monitor montre
  `wifi: connected ip=192.168.x.y`

### Task 16.2 : `bmu_mqtt` — publish telemetry JSON 1 Hz

**Files:**
- Create: `firmware-idf-v2/components/bmu_mqtt/{CMakeLists.txt,include/bmu_mqtt.h,src/bmu_mqtt.c}`
- Create: `firmware-idf-v2/main/task_mqtt.cpp`

- Client `esp-mqtt` vers `mqtt://kxkm-ai:1883` (hardcoded pour le moment ; NVS configurable Phase 20)
- Topic : `bmu/<device_id>/telemetry` ; `device_id` = MAC lower-case 6 bytes
- Payload JSON : `{"ts":<unix_ms>,"batteries":[{"idx":0,"v":24512,"i":-1230,"soc":87}, ...],
  "soh":[0.98,...],"climate":{"t_box":25.1,"hum":45},"sys":{"uptime":...}}`
- Task `task_mqtt` 1 Hz, prio 3, core 1, stack 8 KiB
- QoS 0 (best effort) ; QoS 1 utilisé par replay path (Task 16.3)
- On publish success, appeler `bmu_sd_log_mark_sent(offset_cur)` pour avancer le curseur NOSYNC
- **Test** : `mosquitto_sub -h kxkm-ai -t 'bmu/+/telemetry'` affiche un message par seconde

### Task 16.3 : SD replay logic

**Files:**
- Modify: `firmware-idf-v2/components/bmu_mqtt/src/bmu_mqtt.c` (ajout fonction replay)
- Modify: `firmware-idf-v2/components/bmu_sd_log/src/bmu_sd_log.c` (cursor API)

- Au reconnect MQTT (`MQTT_EVENT_CONNECTED`), spawner un job oneshot `replay_backlog`
- Lire les `.nosync` files, trouver le premier offset < filesize
- Pour chaque ligne non-envoyée, reconstruire un JSON minimal (depuis le line-protocol) et
  publish sur `bmu/<id>/telemetry/replay` en QoS 1
- Batch 50 messages à la fois, yield 10 ms entre batch pour pas bloquer MQTT event loop
- Après succès complet, update cursor à filesize ; rotate file si full
- **Test** : couper Wi-Fi pendant 60 s (`nmcli dev disconnect wifi` côté AP), reconnecter,
  vérifier côté broker l'arrivée des ~60 messages replay dans l'ordre

### Task 16.4 : Integration main + boot sequence

**Files:**
- Modify: `firmware-idf-v2/main/main.cpp`
- Modify: `firmware-idf-v2/main/CMakeLists.txt`

- Init order : `nvs` → `bmu_wifi_init()` → `bmu_mqtt_init()` → `task_mqtt_start()`
- Logger heap free après chaque init
- Flag boot-time pour bypass MQTT si `bmu_wifi_is_connected()` == false après 30 s

### Task 16.5 : Commit Phase 16

- Subject : `feat(phase-16): wifi + mqtt + sd replay` (39 chars ✓)

### Known risks — Phase 16

- **MQTT event loop blocked by replay** — si replay publie 1000 messages d'un coup, event loop
  MQTT est bloqué. Batch + yield obligatoires.
- **NVS config oubliée** → device silent en Wi-Fi. Log warning clair et continuer boot (BLE
  peut encore fonctionner en Phase 18+).
- **MQTT broker down** → reconnect spam. Backoff obligatoire côté `esp-mqtt` (déjà inclus).

- [ ] **Commit** : `feat(phase-16): wifi + mqtt + sd replay`

---

# Phase 17 — LVGL 5 tabs (BATT / SOH / SYS / CLIMATE / CONFIG)

**Objectif** : afficher sur le display BOX-3 un UI 5 onglets read-only alimenté par le
`BmuSnapshot`. Cadences distinctes : data refresh 5 Hz (snapshot pull), render 20 Hz (LVGL tick).
L'onglet BATT est critique (temps de réponse visuel) — les autres peuvent déprioriser.

**Durée estimée** : 2 jours. **Risk** : moyen (LVGL layout + font/asset budget).
**Acceptance** : swipe entre onglets fluide, affichage cohérent avec MQTT broker, aucun tearing,
heap LVGL stable.

### Heritage v1

| Composant | Source v1 | Réutilisation |
|---|---|---|
| `bmu_ui` | `firmware-idf/components/bmu_display/` | **~60 % copie** — architecture tabs + screen lifecycle OK, mais data binding réécrit pour taper `bmu_core_snapshot()` au lieu de struct C legacy |
| Assets | `firmware-idf/components/bmu_display/assets/` | **100 % copie** — fonts + icons réutilisés tels quels |

### Task 17.1 : `bmu_ui` — scaffold composant + LVGL port

**Files:**
- Create: `firmware-idf-v2/components/bmu_ui/{CMakeLists.txt,include/bmu_ui.h,src/bmu_ui.c}`
- Create: `firmware-idf-v2/components/bmu_ui/assets/` (copie depuis v1)

- Init LVGL via `esp_lvgl_port` (déjà en requirements Phase 11)
- Créer 5 screens, TabView LVGL avec swipe horizontal
- Placeholder labels pour chaque tab

### Task 17.2 : Tab BATT — 16 cellules V/I/SoC

- Grid 4×4 de "cell widgets" : chaque cell affiche `V (mV)`, `I (mA)`, `SoC %`, indicateur
  ON/OFF (couleur)
- Palette : vert = healthy, jaune = warning V/I, rouge = fault / forced off
- Data pull via `bmu_core_snapshot()` toutes les 200 ms (cadence 5 Hz)
- Render async LVGL 20 Hz (timer LVGL natif)

### Task 17.3 : Tabs SOH + SYS + CLIMATE + CONFIG

- **SOH** : bars horizontales 16 batteries, valeur `0.00`-`1.00` issue de `bmu_soh_get()`
- **SYS** : uptime, heap free, Wi-Fi status, MQTT status, WDT count, IP, device_id
- **CLIMATE** : 3 big numbers (T_box, T_mcu, Humidity), mini-graph 60 s issue de `bmu_climate_get_stats()`
- **CONFIG** : read-only dump des seuils (Vmin/Vmax/Imax/Tmax/cycles) depuis `BmuConfigC`

### Task 17.4 : `task_ui` + cadence séparée data/render

**Files:**
- Create: `firmware-idf-v2/main/task_ui.cpp`

- Task UI : prio 2, core 1, stack 16 KiB
- Deux timers LVGL internes : `lv_timer_create(data_pull, 200, NULL)` et render par `lv_task_handler()`
  dans la boucle principale à 50 ms
- **Test** : bench montre UI stable 10 min, swipe tabs fluide, CPU LVGL < 30 %

### Task 17.5 : Heap + flash budget check

- Mesurer : flash avant/après, heap LVGL avant/après
- Budget LVGL alloué : 100 KB PSRAM pour framebuffer + 50 KB heap widgets
- Si dépassement : réduire font size, simplifier widgets

### Task 17.6 : Commit Phase 17

- Subject : `feat(phase-17): lvgl 5 tabs read-only ui` (40 chars ✓)

### Known risks — Phase 17

- **LVGL heap leak** : widgets non détruits au screen change. Utiliser `lv_obj_clean()` et
  toujours parent propre.
- **Double buffer PSRAM** : sans PSRAM, LVGL tombe en single buffer + tearing. BOX-3 a PSRAM,
  mais vérifier config `CONFIG_SPIRAM_USE_MALLOC`.
- **Flash bloat assets** : fonts TTF custom peuvent peser 300 KB chacun. Garder ≤ 2 fonts.

- [ ] **Commit** : `feat(phase-17): lvgl 5 tabs read-only ui`

---

# Phase 18 — BLE GATT services read-only

**Objectif** : exposer via BLE (NimBLE stack) 3 services GATT read-only conformes spec §7 :
Battery Service (16 cells × V/I/SoC), System Service (uptime, temp, heap, status), Config
Service (thresholds read-only). L'app iOS voit 16 batteries en live via subscribe notify.
**Aucune commande write pour cette phase** — le Control Service arrive Phase 19.

**Durée estimée** : 1-2 jours. **Risk** : moyen (coherence UUID v1 ↔ v2 pour app iOS).
**Acceptance** : iOS app (`iosApp/`) scan + connect + voit 16 batteries mises à jour live
via BLE notifications. Valeurs cohérentes avec MQTT broker et LVGL display.

### Heritage v1

| Composant | Source v1 | Réutilisation |
|---|---|---|
| `bmu_ble` | `firmware-idf/components/bmu_ble/` | **~70 % copie** — service definitions + UUIDs OK, réécrire `bmu_ble_read_cb` pour lire `bmu_core_snapshot()` |
| UUIDs | spec §7.1 | Identiques v1 pour garantir compat iOS app (audit 2026-04-08 = 100 % parité) |

### Task 18.1 : `bmu_ble` scaffold + NimBLE init

**Files:**
- Create: `firmware-idf-v2/components/bmu_ble/{CMakeLists.txt,include/bmu_ble.h,src/bmu_ble.c}`
- Modify: `firmware-idf-v2/sdkconfig.defaults` (CONFIG_BT_ENABLED=y, CONFIG_BT_NIMBLE_ENABLED=y, disable Bluedroid)

- Init NimBLE stack, GAP advertising name `KXKM-BMU-<device_id_4last>`
- Services définis : Battery (0xFFB1), System (0xFFB2), Config (0xFFB3) — UUIDs spec §7.1

### Task 18.2 : Battery Service — 16 characteristics + notify

- 1 characteristic = 1 batterie, payload 12 bytes : `{uint16_t v_mV; int16_t i_cA; uint8_t soc;
  uint8_t flags; uint32_t cycles;}`
- Notify subscribable par iOS, push à 2 Hz (cadence conforme spec §7.2)
- Lire depuis `bmu_core_snapshot()` dans la task BLE, pas depuis callback GATT

### Task 18.3 : System Service + Config Service

- System : 1 characteristic agrégée 32 bytes `{uptime_s, heap_free, t_mcu, wifi_rssi,
  mqtt_connected, sd_mounted, n_batteries}`
- Config : read-only characteristic avec dump du `BmuConfigC` (V/I/T thresholds, topology)
- Notify 0.5 Hz

### Task 18.4 : `task_ble` + integration

**Files:**
- Create: `firmware-idf-v2/main/task_ble.cpp`
- Modify: `firmware-idf-v2/main/main.cpp`

- Task BLE : prio 3, core 1, stack 8 KiB
- Init après Wi-Fi (pour éviter BT/Wi-Fi coexistence issues)

### Task 18.5 : Commit Phase 18

- Subject : `feat(phase-18): ble gatt read-only services` (45 chars ✓)

### Known risks — Phase 18

- **BT/Wi-Fi coexistence** : ESP32-S3 partage le radio. Wi-Fi actif + BLE scan peut drop packets.
  Prioriser Wi-Fi (MQTT critical) ; BLE avec `CONFIG_BT_NIMBLE_SOFTWARE_COEX=y`.
- **UUID drift v1 ↔ v2** : iOS app hardcode les UUIDs. Valider avec `iosApp/` avant flash.
- **Notify storm** : 16 characs × 2 Hz = 32 notify/s. iOS peut perdre. Packer en 1 characteristic
  "all batteries" si performance poor.

- [ ] **Commit** : `feat(phase-18): ble gatt read-only services`

---

# Phase 19 — BLE Control + pairing SC + HMAC audit (CRIT-D)

**Objectif** : ajouter le **BLE Control Service** avec commandes write (`ForceOff`, `ResetAh`,
`TriggerRint`, `SetThreshold`). Sécurité obligatoire : **pairing Secure Connections (SC) avec
passkey 6 chiffres + HMAC-SHA256 sur chaque commande dérivé du LTK + audit log persistent SD**.
**Cette phase résout la vulnérabilité CRIT-D de l'audit 2026-03-30.**

**Durée estimée** : 2-3 jours. **Risk** : **TRÈS ÉLEVÉ — SÉCURITÉ CRITIQUE**. Toute erreur ici
compromet la sécurité du système déployé sur site chez KXKM.
**Acceptance** : iOS app peut envoyer commandes authentifiées, chaque commande est vérifiée
HMAC, audit log SD contient les entrées signées, commande non-pairée rejetée immédiatement.

### Heritage v1

| Composant | Source v1 | Réutilisation |
|---|---|---|
| NimBLE SM | ESP-IDF | **0 % copie** — pairing SC est nouveau, v1 utilisait `BLE_SM_PAIR_AUTHREQ_BOND` sans SC |
| HMAC derivation | aucun | **0 % copie** — nouveau, cf spec §7.5 |
| Audit log | `firmware-idf/components/bmu_storage/audit.c` | **~30 % copie** — format log similaire mais signature HMAC nouvelle |

### Task 19.1 : NimBLE pairing Secure Connections + passkey

**Files:**
- Modify: `firmware-idf-v2/components/bmu_ble/src/bmu_ble.c`
- Modify: `firmware-idf-v2/sdkconfig.defaults` (CONFIG_BT_NIMBLE_SM_SC=y, CONFIG_BT_NIMBLE_SM_LEGACY=n)

- Activer `BLE_SM_IO_CAP_DISP_ONLY` — BMU affiche passkey, iOS la saisit
- Génération passkey : **pseudo-aléatoire dérivé du device MAC + boot counter NVS**
  (pas de hardcoded 000000 !) ; display sur LVGL tab CONFIG pendant pairing
- Bond storage NVS namespace `ble_bonds` ; max 5 bonds
- Reject write sur Control Service si `ble_gap_conn_find().sec_state.authenticated == 0`

### Task 19.2 : Control Service + 4 commandes

**Files:**
- Modify: `firmware-idf-v2/components/bmu_ble/include/bmu_ble.h`
- Modify: `firmware-idf-v2/components/bmu_ble/src/bmu_ble.c`

- Nouveau service UUID 0xFFB4 "Control"
- 1 write characteristic `0xFFC0` — payload 32 bytes : `{cmd_id:u8, battery_idx:u8,
  param:i32, nonce:u64, hmac:u256}` (total 1 + 1 + 4 + 8 + 32 = 46 bytes ; ajuster alignment)
- Commandes :
  - `0x01 ForceOff` — battery_idx 0-15 ou 0xFF = all ; appelle `bmu_core_cmd_force_off()`
  - `0x02 ResetAh` — battery_idx ; appelle `bmu_core_cmd_reset_ah()`
  - `0x03 TriggerRint` — battery_idx ; appelle `bmu_core_cmd_trigger_rint()`
  - `0x04 SetThreshold` — param encode `(threshold_id << 24) | value`
- Dispatcher C → Rust via `bmu_core_submit_cmd(BmuCommandC)` (ajouté en Part 1 Phase 10)

### Task 19.3 : HMAC-SHA256 derivation + verification

- Key derivation : HKDF-SHA256 (mbedtls) depuis LTK (32 bytes post-bond) + info string
  `"KXKM-BMU-CMD-v1"` → 32 bytes HMAC key
- Stockage NVS namespace `cmd_keys` indexé par peer address ; refresh à chaque pairing
- Sur réception command :
  1. Vérifier nonce strictement croissant (anti-replay) stocké par peer
  2. Calculer HMAC-SHA256 sur `(cmd_id || battery_idx || param || nonce)`
  3. Comparer constant-time avec `hmac` field reçu
  4. Si mismatch → reject + audit log `REJECTED HMAC mismatch`
- **Test unitaire** : vecteurs HMAC de test dans `tests/hil/test_hmac.py` pour valider côté iOS

### Task 19.4 : Audit log SD signé

**Files:**
- Create: `firmware-idf-v2/components/bmu_ble/src/audit_log.c`
- Modify: `firmware-idf-v2/components/bmu_sd_log/` (ajout writer audit fichier séparé)

- Fichier `/sdcard/audit.log` append-only, rotation 1 MB
- Format ligne : `<unix_ms> <peer_mac> <cmd_id> <bat> <param> <result> <hmac_hex8>`
- Ajout d'une signature HMAC de chaîne (log chain) : chaque ligne inclut HMAC(line || prev_hmac)
  pour détection tampering
- **Test** : sur bench, envoyer 5 commandes, vérifier `cat /sdcard/audit.log` montre 5 lignes
  avec HMAC chaînés valides

### Task 19.5 : Reject policy + rate limiting

- Max 10 commandes/seconde par peer (token bucket)
- Blacklist peer après 5 HMAC mismatch consécutifs pour 60 s
- Log warning LVGL tab CONFIG : `SECURITY: peer XX blacklisted`

### Task 19.6 : Test bench sécurité

**Files:** aucun (tests manuels + scripts)

- Script Python `tests/hil/test_ble_control.py` (mode semi-auto) :
  - Tente commande non-pairée → attend reject
  - Pairing SC normal → envoi ForceOff → vérifier battery off
  - Replay même commande → attend reject (nonce)
  - Mauvais HMAC → attend reject
  - 11 commandes/s → attend rate limit
- Tester depuis iOS app réelle pour end-to-end
- **Exit latency** : command → action < 1 s

### Task 19.7 : Commit Phase 19

- Subject : `feat(phase-19): ble control + sc pairing + hmac` (49 chars ✓)
- Body : note explicite "Resolves CRIT-D from audit 2026-03-30"

### Known risks — Phase 19

- **CRITIQUE** : **test unitaire HMAC iOS ↔ firmware obligatoire** avant tout déploiement.
  Un mismatch endian ou encoding = commandes toutes rejetées en prod.
- **Key exfiltration** : LTK en flash NVS non chiffré par défaut. **Activer
  `CONFIG_NVS_ENCRYPTION=y`** et eFuse key partition dédiée (voir ESP-IDF security docs).
- **Anti-replay nonce** : persister en NVS pour survivre au reboot, sinon replay possible
  post-reboot.
- **Passkey DoS** : afficher passkey en clair sur LVGL = fuite si caméra ; envisager display
  seulement pendant 30 s après bouton physique (post-Phase 19 hardening).
- **Bonding table full** : 5 peers max ; plus → eviction LRU. Documenter comportement.
- **Debug builds** : ne JAMAIS flasher un firmware Phase 19 debug en prod — les logs peuvent
  exposer LTK ou HMAC keys. Mettre `#if !defined(NDEBUG)` gates autour des traces sensibles.

**Before exécution Phase 19** : lire **intégralement** spec §7.5 (BLE Security Model) + audit
2026-03-30 CRIT-D section. Faire relecture santa-loop ou rust-review + ecc:security-review
sur le PR Phase 19 **avant merge**.

- [ ] **Commit** : `feat(phase-19): ble control + sc pairing + hmac`

---

# Phase 20 — Wi-Fi provisioning via BLE Command `WifiProv`

**Objectif** : permettre de provisionner le Wi-Fi d'un BMU vierge sans serial, uniquement via
l'app iOS sur BLE. Ajout d'une commande BLE (kind `0x08` = `WifiProv`) qui écrit SSID + PSK
en NVS, déclenche un reboot, et le firmware reconnecte au nouveau réseau.

**Durée estimée** : 1 jour. **Risk** : moyen (NVS write + reboot timing).
**Acceptance** : un device flashé sans credentials peut être provisionné 100 % via iOS app
en < 60 s, puis se connecte au Wi-Fi cible et publie MQTT.

### Heritage v1

- Aucun — v1 utilisait `credentials.h` compile-time. Phase 20 est nouveau.
- `bmu_wifi` Phase 16 déjà prêt à lire NVS — juste ajouter le path write.

### Task 20.1 : Nouvelle command `WifiProv` (kind 0x08)

**Files:**
- Modify: `firmware-idf-v2/components/bmu_ble/src/bmu_ble.c` (add cmd_id 0x08)
- Modify: `firmware-idf-v2/components/bmu_wifi/src/bmu_wifi.c` (add `bmu_wifi_set_creds(ssid, psk)`)

- Payload payload étendu : au lieu de param i32, `WifiProv` utilise un buffer 96 bytes
  `{ssid[32], psk[64]}`
- Nécessite extension du framing BLE command — bumper la characteristic size à 128 bytes
- HMAC toujours requis (Phase 19)
- Sur réception : NVS write namespace `wifi_creds`, keys `ssid` + `psk`, commit, log audit

### Task 20.2 : Reboot + reconnect

- Après write NVS success, schedule reboot dans 2 s via `esp_restart()` (laisser BLE ACK partir)
- Au reboot, Phase 16 `bmu_wifi_init()` lit la nouvelle config et se connecte

### Task 20.3 : Test end-to-end

- Effacer NVS (`idf.py erase-flash` + flash app only) → device boot sans Wi-Fi
- iOS : scan + pair SC + envoi `WifiProv(ssid="KXKM", psk="...")`
- Vérifier reboot + `wifi: connected ip=...` + MQTT publish redémarre
- **Exit timing** : end-to-end < 60 s dont reboot ~3 s

### Task 20.4 : Commit Phase 20

- Subject : `feat(phase-20): ble wifi provisioning cmd` (42 chars ✓)

### Known risks — Phase 20

- **NVS write power-loss** : si coupure pendant commit, NVS corrompu → device unbootable Wi-Fi.
  Mitigation : double buffer NVS natif ESP-IDF + audit log avant commit.
- **PSK in BLE advertising logs** : s'assurer que le payload write ne s'affiche **jamais** en log
  clair. `ESP_LOG_HEX` → remplacer par checksum short.
- **SSID longer than 32 bytes** (IEEE max 32) — truncate silencieusement + log warning.

- [ ] **Commit** : `feat(phase-20): ble wifi provisioning cmd`

---

# Phase 21 — Victron SmartShunt emulation

**Objectif** : faire apparaître le BMU comme un Victron SmartShunt dans l'écosystème
VictronConnect / Cerbo GX. Implémentation de 2 surfaces : (1) GATT service 0x6597
(VictronSmartShunt custom service), (2) Instant Readout advertising AES-CTR encrypted.
Les valeurs publiées sont des **fleet aggregates** : V moyen, I total, SoC pondéré, puissance
totale, température max, depth of discharge.

**Durée estimée** : 1-2 jours. **Risk** : moyen (reverse-engineered protocol).
**Acceptance** : VictronConnect app sur iOS détecte le BMU comme "SmartShunt KXKM-BMU-XXXX",
affiche les valeurs fleet cohérentes avec le MQTT broker.

### Heritage v1

| Composant | Source v1 | Réutilisation |
|---|---|---|
| `bmu_ble_victron` | `firmware-idf/components/bmu_ble_victron/` | **~85 % copie** — protocole RE déjà fait v1, UUIDs + frame format identiques |
| `bmu_ble_victron_gatt` | `firmware-idf/components/bmu_ble_victron_gatt/` | **~90 % copie** — GATT wrapper trivial |
| `bmu_ble_victron_scan` | `firmware-idf/components/bmu_ble_victron_scan/` | **Non utilisé** — on émule, on ne scan pas |

### Task 21.1 : Fleet aggregation helper

**Files:**
- Create: `firmware-idf-v2/components/bmu_ble_victron/src/fleet_agg.c`

- Pure function : `fleet_agg_compute(const BmuSnapshot*, BmuFleetAgg*)`
- `BmuFleetAgg` : `{v_avg_mV, i_sum_cA, soc_weighted_pct, p_total_cW, t_max, dod_pct}`
- Weighted SoC : `sum(soc_i * capacity_i) / sum(capacity_i)` — capacité depuis config
- **Test host** : un test unitaire C avec struct snapshot factice

### Task 21.2 : GATT Victron Service 0x6597

**Files:**
- Create: `firmware-idf-v2/components/bmu_ble_victron/{CMakeLists.txt,include/bmu_ble_victron.h,src/bmu_ble_victron.c}`

- Copier service definitions v1 `bmu_ble_victron_gatt`
- Substituer les reads au fleet_agg computed live depuis `bmu_core_snapshot()`
- Notify 1 Hz

### Task 21.3 : Instant Readout advertising (AES-CTR)

- Format advertising manufacturer data 0x02E1 (Victron prefix) + encrypted payload
- Key : dérivée depuis un "bind key" 16 bytes stocké NVS namespace `victron_keys`
  (généré au premier boot ou imposé par pairing out-of-band)
- Frame : `{model_id, iv, aes_ctr(payload_16)}`
- Payload : fleet V/I/SoC/P packed Victron-style (cf v1 `bmu_ble_victron.c:encode_frame()`)
- Update adv data 1 Hz via `ble_gap_adv_set_fields()`

### Task 21.4 : Test VictronConnect

- Depuis iPhone : VictronConnect app → scan → voir "KXKM-BMU-XXXX" apparaître
- Cliquer → voir valeurs V/I/SoC cohérentes avec MQTT broker (delta ≤ 1 %)
- Si bind key incorrect → VictronConnect affiche "encrypted device, enter key"

### Task 21.5 : Commit Phase 21

- Subject : `feat(phase-21): victron smartshunt emulation` (44 chars ✓)

### Known risks — Phase 21

- **Advertising coexistence avec BLE GATT services Phase 18-20** : 1 seul advertising channel.
  Switcher entre NimBLE advertising BMU vs Victron → complexe. Alternative : les intégrer en
  un seul advertising packet avec 2 service data entries.
- **Victron bind key leak** : même précautions que Phase 19 (NVS encryption eFuse).
- **Protocol drift** : Victron peut casser leur proto à tout moment (closed). Version-lock
  côté app iOS.

- [ ] **Commit** : `feat(phase-21): victron smartshunt emulation`

---

# Phase 22 — HIL TB01-TB13 + bench 72 h + merge

**Objectif** : campagne complète **Hardware-In-the-Loop** sur les 13 test benches définis dans
spec §10.4, Python harness automatisé dans `tests/hil/`, bench 72 h de stabilité continue sur
bench complet (16 batteries), génération PDF report signé, puis rename `firmware-idf-v2` →
`firmware-idf` et merge `feat/rust-hybrid-v2` → `main`. **C'est la phase de livraison finale.**

**Durée estimée** : 3-5 jours (dont 72 h wall-clock pour le bench). **Risk** : élevé
(coordination hardware humaine + découverte tardive de bugs).
**Acceptance** : rapport HIL signé (≥ 12/13 TB passing, TB04-TB10 nécessitent acceptation
manuelle), bench 72 h clean (0 reboot, p99 tick < 50 ms, heap stable), PR mergée sur `main`.

### Heritage v1

- Aucun — HIL harness n'existait pas en v1 (S3 validation était manuelle).
- Format PDF report : template réutilisable pour futures campagnes.

### TB Matrix — Automation classification

| TB | Titre | Automatable | Humain requis |
|---|---|---|---|
| TB01 | Boot + init chain | ✅ Python | — |
| TB02 | Scan I²C 16 INA + 4 TCA | ✅ Python | — |
| TB03 | Snapshot V/I cohérent | ✅ Python | — |
| TB04 | Court-circuit SDA/SCL | ❌ | ✅ Opérateur |
| TB05 | Débranchement batterie live | ❌ | ✅ Opérateur |
| TB06 | Sur-tension injected | ❌ | ✅ Opérateur (PSU) |
| TB07 | Sur-intensité load dump | ❌ | ✅ Opérateur (load) |
| TB08 | Sur-température forced | ❌ | ✅ Opérateur (heat gun) |
| TB09 | Court-circuit sortie | ❌ | ✅ Opérateur |
| TB10 | Coupure alim MCU | ❌ | ✅ Opérateur |
| TB11 | BLE pairing + commands | ✅ Python (iOS sim) | — |
| TB12 | MQTT replay SD | ✅ Python | — |
| TB13 | Bench 72 h stability | ✅ Python (monitor) | — |

### Task 22.1 : Python harness scaffold `tests/hil/`

**Files:**
- Create: `firmware-idf-v2/tests/hil/{conftest.py,hil_device.py,hil_report.py}`
- Create: `firmware-idf-v2/tests/hil/tb{01..13}_*.py`

- Framework : `pytest` + `pyserial` (monitor log) + `paho-mqtt` (verify broker) + `bleak` (BLE
  scan client-side)
- `HilDevice` : wrapper serial + reboot + flash + log capture
- `HilReport` : collect results, render PDF via `reportlab`

### Task 22.2 : TB01-TB03 (boot + I²C + snapshot cohérence)

- TB01 : flash firmware clean, monitor boot chain, assert `task_bmu_core started` in ≤ 10 s
- TB02 : parse `i2c scan` log, assert 16 INA + 4 TCA détectés
- TB03 : subscribe MQTT `bmu/+/telemetry`, capture 60 s, assert V/I/SoC dans limites sanity

### Task 22.3 : TB04-TB10 (prompts interactifs)

- Script affiche : `"[TB04] Veuillez court-circuiter SDA pendant 2 s puis relâcher. Appuyer
  ENTRÉE quand prêt."`
- Pendant ce temps, capture log série
- Assert : ≥ 1 message `triggering bus recovery` ET reprise `tick OK` < 5 s après release
- Chaque TB demande confirmation opérateur pour PASS/FAIL

### Task 22.4 : TB11-TB12 (BLE + MQTT replay)

- TB11 : utiliser `bleak` Python pour scan, pair SC (passkey lu depuis log serial), send
  `ForceOff` command, vérifier effet sur MQTT telemetry + audit log SD
- TB12 : couper Wi-Fi via commande système, attendre 60 s, reconnecter, vérifier replay
  messages dans topic `bmu/+/telemetry/replay`

### Task 22.5 : TB13 — bench 72 h

- Script monitor : lance `idf.py monitor` en background, capture logs continus
- Parse `latency tick` toutes les 5 min, build histogram p50/p99
- Check MQTT messages arrivent à 1 Hz continu
- Check heap free via log `heap report` toutes les 10 min
- **Exit** après 72 h : 0 reboot, p99 tick < 50 ms sur toute la fenêtre, heap delta < 5 %

### Task 22.6 : PDF report generation

**Files:**
- Create: `firmware-idf-v2/tests/hil/report_template.py`

- Header : date, device_id, firmware SHA, tester name
- Table TB01-TB13 avec PASS/FAIL + notes
- Graphs : latency histogram, heap timeline, MQTT msg rate
- Footer : signature zone + disclaimer
- Output : `docs/superpowers/validation/reports/YYYY-MM-DD-phase22-hil-report.pdf`

### Task 22.7 : Rename `firmware-idf-v2` → `firmware-idf`

**Files:**
- Move: `firmware-idf/` → `firmware-idf-legacy/` (commit séparé préliminaire)
- Move: `firmware-idf-v2/` → `firmware-idf/` (commit séparé)
- Modify: toutes références dans `CLAUDE.md`, `scripts/`, `CMakeLists.txt` top-level, CI configs

- Commit 1 : `chore(archive): rename firmware-idf to firmware-idf-legacy`
- Commit 2 : `chore(phase-22): rename firmware-idf-v2 to firmware-idf`
- Git detect le rename via similarity — diff minimal

### Task 22.8 : PR + merge → main

- Rebase `feat/rust-hybrid-v2` sur `main` latest
- Ouvrir PR avec description comprehensive + link vers rapport HIL PDF + bench 72 h log
- Code review : santa-loop + rust-review + ecc:security-review
- Merge après ≥ 2 approbations
- Tag release `v2.0.0-rust-hybrid`
- Commit Phase 22 (combiné avec rename) : `feat(phase-22): hil harness + 72h bench + merge`

### Known risks — Phase 22

- **72 h wall clock** — un bug découvert à T+50 h = refaire tout le bench. Prévoir run à blanc
  avant le "vrai" 72 h (smoke test 2 h).
- **HIL TB04-TB10 humains** — opérateur indisponible = blocage. Planifier slot dédié avec
  accès bench + sécurité incendie + PSU lab.
- **Rename git conflicts** — si `main` a avancé pendant Part 2b, rebase peut être chaotique.
  Mitigation : freeze `main` sur une sous-branche `main-v2-freeze` pour la durée du merge.
- **CI rouge post-merge** — scripts CI hardcodent probablement `firmware-idf/` path. Vérifier
  chaque workflow GitHub Actions avant le rename.
- **Security review Phase 19 trouve faille** — Phase 22 est bloquée jusqu'à fix Phase 19 bis.
  Allouer 1 jour de buffer pour hardening sécurité.

- [ ] **Commit** : `feat(phase-22): hil harness + 72h bench + merge`

---

## Acceptance Gates — Phases 15-22

Avant merge final sur `main`, vérifier tous les points suivants :

- [ ] **Gate 15** — `bmu_climate` + `bmu_soh` + `bmu_sd_log` opérationnels, TFLite inference < 100 ms
- [ ] **Gate 16** — Broker `kxkm-ai:1883` reçoit telemetry 1 Hz, replay post-coupure validé
- [ ] **Gate 17** — LVGL 5 tabs, swipe fluide, CPU render < 30 %
- [ ] **Gate 18** — iOS app voit 16 batteries live, UUIDs compat v1 100 %
- [ ] **Gate 19** — **CRIT-D résolu** : pairing SC + HMAC + audit log ; ecc:security-review OK
- [ ] **Gate 20** — Device vierge provisionné Wi-Fi en < 60 s via BLE
- [ ] **Gate 21** — VictronConnect voit BMU en SmartShunt, valeurs cohérentes
- [ ] **Gate 22** — HIL report PDF signé (≥ 12/13 TB passing) + bench 72 h clean
- [ ] **Gate 23** — Flash size final < 85 % budget OTA (< 1.7 MB)
- [ ] **Gate 24** — Tests host Rust toujours verts (`cargo test --workspace` dans `firmware-rust/`)
- [ ] **Gate 25** — 8 commits atomiques `feat(phase-15..22)` + 2 commits rename
- [ ] **Gate 26** — PR mergée sur `main`, tag `v2.0.0-rust-hybrid` posé

---

## Handoff notes

**Dépendances externes :**
- ESP-IDF v5.4, component registry access (TFLite Micro, esp-mqtt, esp-box-3 BSP)
- SD card ≥ 8 GB industrial (wear leveling) pour les 72 h de bench
- Broker MQTT `kxkm-ai:1883` accessible, InfluxDB + Grafana pour inspection
- iOS app `iosApp/` buildée et signée pour BLE test réel (Phase 18-20)
- VictronConnect app installée sur iPhone pour Phase 21
- Opérateur hardware disponible pour Phase 22 TB04-TB10 (prévoir 2 créneaux 4 h)

**Hors scope de ce plan** (reportés à Part 2c si besoin) :
- `bmu_vedirect` V2 (VE.Direct scanning sur GPIO21 — décision Q8 drop)
- OTA updates via MQTT (`bmu_ota` v1 existe mais pas porté V2)
- Grafana dashboard refresh (actuels v1 compatibles topic `bmu/+/telemetry`, à vérifier)
- Fine-tuning modèle SOH (dataset KXKM réel, Phase après 6 mois de prod)
- Compose Multiplatform KMP app (`kxkm-bmu-app/`) intégration V2 — ticket séparé

**Points de vigilance rétrospectifs Part 1 + Part 2 à rappeler :**
- Tout `pub fn` Rust returning value : `#[must_use]` obligatoire
- Pas de `Copy` derive sur containers d'état
- Commit hooks : subject ≤ 50 chars, body ≤ 72 chars, heredoc obligatoire
- Chaque phase 1 commit atomique — jamais de commit intermédiaire par task
- `bmu_i2c_lock()` wrapper obligatoire pour tout nouveau code accédant au bus I²C
- Ne **jamais** affaiblir les seuils protection — même temporairement pour debug
- Phase 19 : relecture sécurité obligatoire avant merge (santa-loop ou ecc:security-review)
- Phase 22 : bench 72 h non négociable — pas de raccourci à 24 h ou 48 h

**Ordering flex :**
- Phase 15 sous-tâches (climate, SOH, SD) **parallélisables** entre elles (agents indépendants OK)
- Phase 16-17 **partiellement parallèles** (UI Phase 17 peut commencer pendant que MQTT Phase 16
  se stabilise)
- Phase 18-19-20 **séquentielles strictes** (Phase 20 dépend de 19 pour HMAC framing)
- Phase 21 **peut démarrer en parallèle de Phase 20** (services BLE différents)
- Phase 22 **séquentielle finale** — tout doit être stable avant HIL campaign

---

**Fin du plan Part 2b Phases 15-22 (outline).** La version détaillée de chaque phase sera
rédigée au moment de l'exécution, avec les enseignements du bench Part 2 Phases 11-14 et
les ajustements nécessaires (calibration seuils, gestion contention I²C inter-tasks, tuning
LVGL heap). Livraison cible : PR sur `main` + tag `v2.0.0-rust-hybrid` + rapport HIL signé.
