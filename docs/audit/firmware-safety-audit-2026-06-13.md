# Audit sécurité firmware-idf — 2026-06-13

Audit multi-dimensions (4 axes : logique safety, concurrence FreeRTOS/I2C, sûreté mémoire, sécurité BLE/cloud) du firmware ESP-IDF du BMU. Méthodo : skill `bmu-firmware-safety-audit`. Statuts de remédiation : skill `tracked-remediation`.

Légende statut : ✅ corrigé · �doing · ⬜ à faire · 🧪 décision politique (validation requise) · ❌ écarté (faux positif)

## 🔴 CRITICAL

| # | Finding | Fichier:ligne | Statut |
|---|---------|---------------|--------|
| C1 | Sur-tension ne déclenche jamais ERROR/LOCKED → reconnexion en boucle d'une batterie défaillante | bmu_protection.cpp:231 | ✅ sur-tension → ERROR (`v_mv > MAX`) |
| C2 | Fail-safe topologie cosmétique : la tâche rallume malgré `all_off()` ; batteries sans canal TCA non coupables | bmu_protection.cpp:42, main.cpp:640 | ✅ gate `Nb_TCA×4≠Nb_INA` dans check_battery_ex |
| C3 | Write BLE R_int (0x0038) sans WRITE_ENC → action physique non authentifiée | bmu_ble_battery_svc.cpp:423 | ✅ `+ WRITE_ENC` |
| C4 | INA237 : sorties non initialisées sur échec lecture → donnée capteur fantôme | bmu_ina237.cpp:378 | ✅ init NAN en tête |
| C5 | INA237 bitbang : cast `(int16_t)` sur VBUS non signé → tension négative aberrante | bmu_ina237.cpp:528 | ✅ cast retiré |

## 🟠 HIGH

| # | Finding | Fichier:ligne | Statut |
|---|---------|---------------|--------|
| H1 | `publish_snapshot()` lit l'état sécurité SANS state_mutex (race avec hotplug) | bmu_protection.cpp:540 | ✅ copie sous state_mutex |
| H2 | `switch_battery()` (I2C + vTaskDelay) appelée en tenant state_mutex | bmu_protection.cpp:156,606 | ✅ commute hors mutex (health + balance) |
| H3 | Hotplug compacte tableaux/handles I2C partagés sans synchro → use-after-free | bmu_i2c_hotplug.cpp:131 | ⬜ **refactor dédié (validation HW requise)** |
| H4 | Topologie mutée par 2 tâches ; `topology_ok`/`nb_tca` écrits sans mutex | bmu_i2c_hotplug.cpp:189,286,359 | ⬜ **refactor dédié (validation HW requise)** |
| H5 | `web_switch` contourne le lock à 5 (pas de check nb_switch avant ON) | bmu_protection.cpp:435 | ✅ refus si nb_switch>MAX |
| H6 | Balancer rallume une batterie en défaut sans revalidation | bmu_protection.cpp:601 | ✅ guard ON (lock+plage) |
| H7 | Sur-courant : fenêtre 10–20 A réversible (facteur 2.0 large) | Kconfig + :229 | 🧪 politique |
| H8 | TCA9535 : cache OUTPUT mis à jour AVANT confirmation I2C | bmu_tca9535.cpp:240 | ✅ cache après succès I2C |
| H9 | Cloud non-TLS : token Influx en clair (HTTP), MQTT clair, VRM TLS sans vérif cert | bmu_influx/mqtt/vrm | ✅ VRM vérifie le cert (bundle CA). Influx/MQTT restent clairs **par design** (LAN/Tailscale, cf. CLAUDE.md) — durcissement TLS = option future si exposition publique |
| H10 | Clé Victron BLE par défaut commitée | bmu_ble_victron/Kconfig:9 | ✅ défaut vidé |
| H11 | Retours ignorés : publish MQTT/VRM, fputc/fclose store offline (corruption replay) | bmu_vrm/mqtt, bmu_influx_store.cpp:127 | ✅ fputc/fclose + VRM publish |
| H12 | Priorités tâches doc(5/4) ⇄ code(protection=8/balancer=3) | main.cpp:606,646 | ⬜ Vague 4 (dette) |
| H13 | Double source de vérité `nb_ina` + fallback non protégé | main.cpp:128 | ⬜ Vague 2 |

## 🟡 MEDIUM / 🟢 LOW (extraits)

- Injection line-protocol/MQTT via device name modifiable BLE (`device=%s` non échappé) — `main.cpp:264` ✅ device name restreint à `[A-Za-z0-9_-]` à la source
- Pairing Just Works + `min_key_size=0` + re-pairing auto-accepté — `bmu_ble.cpp:158,247` 🧪 politique
- NUL terminator manquant sur défauts Kconfig (device/wifi/mqtt) — `bmu_config.cpp:130` ⬜ Vague 4
- `fgets(512)` tronque lignes longues au replay — `bmu_influx_store.cpp:144` ⬜ Vague 3
- `esp_timer_create()` non vérifié (×3) — ⬜ Vague 4
- Ah comptés sur batteries OFF — `bmu_battery_manager.cpp:34` ⬜ Vague 4
- Clamp `BMU_MAX_BATTERIES` manquant sur GATT rint/soh — ⬜ Vague 4

## 🧹 Dette technique / incohérences

- Code mort non-mutexé : `find_fleet_max_mv()`, `bmu_influx_write_battery()` → supprimer/déléguer. ⬜ Vague 4
- Doc ⇄ code : priorités tâches ; commentaire bus-recover bitbang obsolète. ⬜ Vague 4
- Patterns incohérents : cache avant/après I2C (tca switch vs set_led) ; NUL terminator VRM oui / device non ; clamp partiel. ⬜ Vague 4

## ❌ Faux positifs écartés (vérifiés contre le code)

- influx.cpp:203 « overflow memcpy après flush échoué » → return avant le memcpy.
- influx_store « fwrite non vérifié » → l'est (comparé à len).
- ble_victron_scan « buffer over-read déchiffrement » (×2) → lectures bornées (≥15, raw[10]).
- ble_gatt arrays sans terminateur → terminateurs présents dans toutes les branches.

## Politique safety à valider (🧪)

Valeurs par défaut sûres proposées (à confirmer en revue) :
- **Sur-tension** → ERROR (fait, C1). Option future : ERROR répété N fois → LOCKED.
- **Sur-courant** : réduire `BMU_OVERCURRENT_FACTOR` 2.0 → ~1.3-1.5 (coupure ferme plus tôt).
- **Pairing BLE** : `min_key_size=16` + refuser re-pairing auto (confirmation écran).
