// firmware-idf-v2/main/task_bmu_core.cpp
//
// Voir task_bmu_core.h pour le contrat Phase 14.

#include "task_bmu_core.h"

#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "bmu_core.h"
#include "bmu_i2c_glue.h"
}

static const char *TAG = "task-core";

// --- Parametres d'ordonnancement (Phase 14) ---
static constexpr uint32_t TASK_PERIOD_MS   = 200;           // 5 Hz
static constexpr UBaseType_t TASK_PRIO     = 5;
static constexpr uint32_t TASK_STACK_BYTES = 8 * 1024;      // 8 KiB
static constexpr BaseType_t PIN_PRO_CPU    = 0;             // core 0
static constexpr uint32_t FAIL_THRESHOLD   = 3;             // 3 echecs I2C -> recover

// --- Latence histogram (fenetre glissante 300 ticks ~= 1 min a 5 Hz) ---
static constexpr size_t LAT_WINDOW         = 300;
static constexpr uint32_t LAT_LOG_EVERY    = 50;            // 10 s @ 5 Hz
static constexpr uint32_t STATE_LOG_EVERY  = 25;            // 5 s @ 5 Hz

// Variables statiques (evite de stacker BmuSnapshot ~412 B + BmuRawInputs ~340 B)
static BmuRawInputs  s_raw;
static BmuSnapshotC  s_snap;
static BmuActionsC   s_actions;
static BmuCore      *s_core = nullptr;

// Histogram buffer
static uint32_t s_lat_us[LAT_WINDOW] = {0};
static size_t   s_lat_count = 0;  // nb samples effectifs (plafonne a LAT_WINDOW)
static size_t   s_lat_idx   = 0;  // index d'insertion (rolling)

/**
 * Insertion sort partiel : calcule p50 et p99 sur les `n` premiers elements
 * de `s_lat_us`. Copie dans un buffer local puis trie (n <= 300, tri direct OK
 * a 10 s cadence).
 */
static void compute_p50_p99(uint32_t *out_p50, uint32_t *out_p99) {
    size_t n = s_lat_count;
    if (n == 0) {
        *out_p50 = 0;
        *out_p99 = 0;
        return;
    }
    // Copie locale (stack ~1.2 KiB max : 300 * 4 B)
    uint32_t tmp[LAT_WINDOW];
    memcpy(tmp, s_lat_us, n * sizeof(uint32_t));

    // Insertion sort (n petit, OK)
    for (size_t i = 1; i < n; i++) {
        uint32_t key = tmp[i];
        size_t j = i;
        while (j > 0 && tmp[j - 1] > key) {
            tmp[j] = tmp[j - 1];
            j--;
        }
        tmp[j] = key;
    }

    size_t i50 = (n * 50) / 100;
    size_t i99 = (n * 99) / 100;
    if (i50 >= n) i50 = n - 1;
    if (i99 >= n) i99 = n - 1;
    *out_p50 = tmp[i50];
    *out_p99 = tmp[i99];
}

static void latency_record(uint32_t us) {
    s_lat_us[s_lat_idx] = us;
    s_lat_idx = (s_lat_idx + 1) % LAT_WINDOW;
    if (s_lat_count < LAT_WINDOW) {
        s_lat_count++;
    }
}

/**
 * Corps de la tache FreeRTOS. Cadence `vTaskDelayUntil(..., 200 ms)`.
 *
 * NOTE static-analysis : `bmu_i2c_glue_recover_bus()` est invoque lorsque
 * `s_fail_streak >= FAIL_THRESHOLD`. Sur le banc Phase 14 actuel, le chemin de
 * recuperation compile + reachable mais n'est pas exerce physiquement (test
 * reporte a Phase 15+). Le counter `s_fail_streak` est reset apres recovery
 * succes ou apres un read OK.
 */
static void task_body(void *arg) {
    BmuCore *core = static_cast<BmuCore *>(arg);

    // Init WDT pour cette tache
    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    if (wdt_err != ESP_OK) {
        ESP_LOGW(TAG, "esp_task_wdt_add failed: %s", esp_err_to_name(wdt_err));
    }

    memset(&s_raw, 0, sizeof(s_raw));
    memset(&s_snap, 0, sizeof(s_snap));
    memset(&s_actions, 0, sizeof(s_actions));

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t tick_counter = 0;
    uint32_t s_fail_streak = 0;

    ESP_LOGI(TAG,
             "task_bmu_core started: core=PRO_CPU prio=%u period=%lu ms",
             (unsigned)TASK_PRIO, (unsigned long)TASK_PERIOD_MS);

    while (true) {
        // Feed WDT des l'entree de periode
        esp_task_wdt_reset();

        int64_t t0 = esp_timer_get_time();

        esp_err_t err = bmu_i2c_glue_read_inputs(&s_raw);
        if (err != ESP_OK) {
            s_fail_streak++;
            ESP_LOGW(TAG, "read_inputs failed: %s (streak=%lu)",
                     esp_err_to_name(err), (unsigned long)s_fail_streak);
            if (s_fail_streak >= FAIL_THRESHOLD) {
                ESP_LOGE(TAG, "FAIL_THRESHOLD reached -- calling recover_bus()");
                esp_err_t rec = bmu_i2c_glue_recover_bus();
                ESP_LOGW(TAG, "recover_bus returned: %s", esp_err_to_name(rec));
                s_fail_streak = 0;
            }
            // On n'appelle pas bmu_core_tick si lecture KO (raw indefini)
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_MS));
            tick_counter++;
            continue;
        }
        s_fail_streak = 0;

        int32_t rc = bmu_core_tick(core, &s_raw, &s_snap, &s_actions);

        int64_t t1 = esp_timer_get_time();
        uint32_t dt_us = (uint32_t)(t1 - t0);
        latency_record(dt_us);

        // Log d'etat (toutes les 5 s pour limiter le spam a 5 Hz)
        if ((tick_counter % STATE_LOG_EVERY) == 0) {
            if (rc != 0) {
                ESP_LOGW(TAG, "bmu_core_tick rc=%ld n_bat=%u topo=%u",
                         (long)rc,
                         (unsigned)s_snap.n_bat,
                         (unsigned)s_snap.system.topology_ok);
            } else if (s_snap.n_bat > 0) {
                ESP_LOGI(TAG,
                         "tick OK n_bat=%u topo=%u bat0: V=%ldmv I=%ldma state=%u "
                         "climate T=%d rh=%u heap=%u kB",
                         (unsigned)s_snap.n_bat,
                         (unsigned)s_snap.system.topology_ok,
                         (long)s_snap.batteries[0].voltage_mv,
                         (long)s_snap.batteries[0].current_ma,
                         (unsigned)s_snap.batteries[0].state,
                         (int)s_raw.climate_temp_c10,
                         (unsigned)s_raw.climate_rh_pct10,
                         (unsigned)(esp_get_free_heap_size() / 1024));
            } else {
                ESP_LOGI(TAG,
                         "tick OK n_bat=0 topo=%u heap=%u kB",
                         (unsigned)s_snap.system.topology_ok,
                         (unsigned)(esp_get_free_heap_size() / 1024));
            }
        }

        // Log latence toutes les LAT_LOG_EVERY ticks (10 s)
        if ((tick_counter % LAT_LOG_EVERY) == 0 && tick_counter > 0) {
            uint32_t p50 = 0, p99 = 0;
            compute_p50_p99(&p50, &p99);
            ESP_LOGI(TAG, "latency tick=%lu n=%u p50=%luus p99=%luus",
                     (unsigned long)tick_counter,
                     (unsigned)s_lat_count,
                     (unsigned long)p50,
                     (unsigned long)p99);
        }

        tick_counter++;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_MS));
    }
}

extern "C" void task_bmu_core_start(BmuCore *core) {
    if (core == nullptr) {
        ESP_LOGE(TAG, "task_bmu_core_start: core is NULL");
        return;
    }
    s_core = core;

    TaskHandle_t handle = NULL;
    BaseType_t ok = xTaskCreatePinnedToCore(
        task_body,
        "bmu_core",
        TASK_STACK_BYTES,
        (void *)core,
        TASK_PRIO,
        &handle,
        PIN_PRO_CPU
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore failed");
        return;
    }
    ESP_LOGI(TAG, "task_bmu_core created handle=%p pinned core=%d prio=%u",
             (void *)handle, (int)PIN_PRO_CPU, (unsigned)TASK_PRIO);
}
