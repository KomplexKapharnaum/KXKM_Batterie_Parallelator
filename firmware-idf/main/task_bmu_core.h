// firmware-idf-v2/main/task_bmu_core.h
//
// Phase 14 : tache FreeRTOS dediee `task_bmu_core` pinnee sur PRO_CPU (core 0),
// priorite 5, periode 200 ms (5 Hz), enregistree aupres du task WDT (3 s).
// Remplace la `tick_loop_real()` 1 Hz de Phase 13.
//
// Responsabilites :
//   - Lire les entrees brutes via `bmu_i2c_glue_read_inputs`
//   - Appeler `bmu_core_tick` et consommer snapshot/actions
//   - Mesurer latence par tick via `esp_timer_get_time()`, log p50/p99 toutes
//     les 50 ticks (10 s)
//   - Compter les echecs consecutifs de read_inputs : 3 echecs -> bus recovery
//   - Tenir le task WDT (esp_task_wdt_reset) a chaque debut de periode
//
// Usage :
//   task_bmu_core_start(s_core);
//   // ... app_main continue et idle

#pragma once

#include "bmu_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Demarre la tache `task_bmu_core` pinnee sur PRO_CPU (core 0).
 *
 * Cette fonction ne retourne pas d'erreur : en cas d'echec de creation de la
 * tache, un log ESP_LOGE est emis et le systeme halt en panic implicite.
 *
 * @param core Handle Rust BmuCore deja initialise (non-NULL)
 */
void task_bmu_core_start(BmuCore *core);

#ifdef __cplusplus
}
#endif
