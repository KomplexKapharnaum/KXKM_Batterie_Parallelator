# KXKM Batterie Parallelator

BMU (Battery Management Unit) ESP32 — gestion sécurisée de batteries en parallèle 24-30V pour scénographie hors-réseau.

Client : KompleX KapharnaüM (Villeurbanne) — contact `nicolas.guichard@lehoop.fr`

## Where to Look

| Tâche | Location |
|-------|----------|
| Modifier le firmware ESP-IDF (production) | `firmware-idf/` |
| Toucher l'Arduino legacy (sim-host tests) | `firmware/` |
| App iOS native (CoreBluetooth) | `iosApp/` |
| KMP Shared Kotlin | `kxkm-bmu-app/` |
| Stack cloud Docker (Mosquitto, InfluxDB, FastAPI) | `kxkm-api/` |
| Schémas/PCB KiCad | `hardware/` |
| Specs Kill_LIFE (S0-S3) | `specs/` |
| Plans + designs superpowers | `docs/superpowers/` |

## Project Status

- Kill_LIFE gates : S0-S2 ✅, S3 ⬜ (validation terrain)
- Tests : 13 PlatformIO sim-host + 13 ESP-IDF host = 26 ✅
- Memory budget : 81% flash (19% headroom sur 2MB OTA)
- iOS ↔ firmware GATT : 100% parité (audit 2026-04-08)

## Safety-Critical Rules

- Ne **jamais** affaiblir les seuils protection (V/I/délai/topology)
- Toute lecture I2C passe par `I2CLockGuard` (firmware/) ou `bmu_i2c_lock()` (firmware-idf/)
- Topology constraint : `Nb_TCA × 4 == Nb_INA` — mismatch → fail-safe (all OFF)
- Tâches FreeRTOS non-bloquantes
- Routes web mutation : token auth obligatoire

## Conventions

- Commentaires + identifiants en **français** (cohérence avec le legacy)
- Seuils dans `firmware/src/config.h` (mV/mA) ou Kconfig ESP-IDF
- Secrets WiFi/MQTT : `credentials.h.example` template (jamais commit credentials.h)
- Python via `uv` (pas pip/venv)

## Agent Workflow

Explore finds → Librarian reads → You plan → Worker implements → Validator checks

- Tâches indépendantes : lancer Task() en parallèle (un seul message, plusieurs calls)
- Avant grosse modif firmware : vérifier `pio test -e sim-host` et `idf.py build`
- Memory budget : `scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85`

## Guidance

Context-specific guidance lives in nested `CLAUDE.md` files (firmware-idf/, iosApp/, kxkm-api/, etc.).
Closest CLAUDE.md to the file being edited takes precedence.
