// firmware-idf-v2/components/bmu_soh/include/bmu_soh.h
//
// Phase 15 -- wrapper TFLite Micro autour de fpnn_soh_v3_int8.tflite.
//
// API minimale (boot bench Phase 15) :
//   - bmu_soh_init() : alloue tensor arena, charge le modele.
//   - task_soh_start(core) : spawn une tache pinned APP_CPU prio 2 qui
//     toutes les 60 s lit `bmu_core_get_cached_snapshot()`, infere SOH par
//     batterie, log le resultat. La push-back via `bmu_core_command(UpdateSoh)`
//     est differee a Phase 16+ (besoin d'un mutex inter-tache).
//
// Sur le bench Phase 15 (1 INA seul, n_bat=0), aucune inference n'est faite
// par tick mais l'init TFLite est valide via un dry-run sur features zero
// au boot pour confirmer que la chaine de chargement / arena tient.

#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

struct BmuCore;

// Init TFLite Micro : charge le modele embarque, alloue les tenseurs.
// Idempotent. Si OOM ou modele invalide, log + retourne ESP_FAIL et le
// task_soh suivant se desactivera (no-op tick).
esp_err_t bmu_soh_init(void);

// True si bmu_soh_init() a reussi.
bool bmu_soh_is_ready(void);

// Spawne la tache periodique 60 s. Lit le snapshot cache du core Rust
// passe en argument.
void task_soh_start(struct BmuCore *core);

#ifdef __cplusplus
}
#endif
