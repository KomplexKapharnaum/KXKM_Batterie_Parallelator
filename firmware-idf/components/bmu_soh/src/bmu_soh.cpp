// firmware-idf-v2/components/bmu_soh/src/bmu_soh.cpp
//
// Phase 15 -- TFLite Micro wrapper pour fpnn_soh_v3_int8.tflite (~12 KB).
//
// Comportement bench (Phase 15) :
//   1. bmu_soh_init() :
//      - lit le modele EMBED_FILES,
//      - alloue 24 KiB arena (heap_caps internal SRAM),
//      - resout les ops du FPNN (FullyConnected, Reshape, Logistic, Mul,
//        Quantize, Dequantize, Concatenation, Gather, Add),
//      - AllocateTensors,
//      - dry-run sur features zero pour confirmer Invoke() OK,
//      - log SHA-style banner et arena_used_bytes.
//   2. task_soh : APP_CPU prio 2, periode 60 s. Lit snapshot cache du core,
//      pour chaque batterie 0..n_bat-1 :
//        - construit features minimal (V mean, I mean, ah_remaining, temp,
//          switch_count) padde a la taille input du modele,
//        - normalise + quantise,
//        - Invoke,
//        - dequantise output -> soh_pct (0..100),
//        - log "soh[i] = X%% lat=Yus".
//      Pas de bmu_core_command(UpdateSoh) pour Phase 15 -- la push-back est
//      reportee a Phase 16 avec le command queue pattern (pas de mutex
//      inter-tache disponible).
//
// Sur le bench actuel (n_bat=0) la boucle interne est skippee mais le
// dry-run boot prouve que la chaine TFLite Micro est integre.

#include "bmu_soh.h"

extern "C" {
#include "bmu_core.h"
}

#include <cstdint>
#include <cstring>
#include <cmath>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

static const char *TAG = "soh";

// --- Embedded model symbols (EMBED_FILES) ---
extern "C" const uint8_t fpnn_model_start[] asm("_binary_fpnn_soh_v3_int8_tflite_start");
extern "C" const uint8_t fpnn_model_end[]   asm("_binary_fpnn_soh_v3_int8_tflite_end");

// --- Tensor arena ---
// 24 KiB sur SRAM interne. v3 modele ~12 KiB, plus tenseurs intermediares.
// Si OOM, augmenter ou deplacer en PSRAM via heap_caps_malloc(MALLOC_CAP_SPIRAM).
static constexpr size_t kArenaSize = 24 * 1024;
static uint8_t *s_arena = nullptr;

static tflite::MicroInterpreter *s_interpreter = nullptr;
static bool s_ready = false;

// Normalisations SoH (13 features) chargees au runtime depuis bmu_core_get_soh_norm().
// Initialisees a identite (means=0, stds=1) -- ecrasees par bmu_soh_init(core).
#define NUM_FEATURES 13
static float s_feat_means[NUM_FEATURES] = {0};
static float s_feat_stds[NUM_FEATURES]  = {1,1,1,1,1,1,1,1,1,1,1,1,1};

bool bmu_soh_is_ready(void) { return s_ready; }

esp_err_t bmu_soh_init(struct BmuCore *core)
{
    if (s_ready) return ESP_OK;

    // Charge les normalisations SoH depuis la config Rust du core.
    if (core != nullptr) {
        int32_t rc = bmu_core_get_soh_norm(core, s_feat_means, s_feat_stds);
        if (rc == 0) {
            ESP_LOGI(TAG, "SoH norms loaded from core: means[0]=%.4f stds[0]=%.4f",
                     (double)s_feat_means[0], (double)s_feat_stds[0]);
        } else {
            ESP_LOGW(TAG, "bmu_core_get_soh_norm failed (%ld), using identity norms",
                     (long)rc);
        }
    } else {
        ESP_LOGW(TAG, "core==NULL, SoH norms = identity (no normalization)");
    }

    const size_t model_size = (size_t)(fpnn_model_end - fpnn_model_start);
    ESP_LOGI(TAG, "loading TFLite model: %u bytes (fpnn_soh_v3_int8)",
             (unsigned)model_size);

    const tflite::Model *model = tflite::GetModel(fpnn_model_start);
    if (model == nullptr) {
        ESP_LOGE(TAG, "tflite::GetModel returned NULL");
        return ESP_FAIL;
    }
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema %lu != expected %d",
                 (unsigned long)model->version(), TFLITE_SCHEMA_VERSION);
        return ESP_FAIL;
    }

    s_arena = (uint8_t *)heap_caps_aligned_alloc(
        16, kArenaSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_arena == nullptr) {
        ESP_LOGE(TAG, "heap_caps_aligned_alloc(%u) failed", (unsigned)kArenaSize);
        return ESP_ERR_NO_MEM;
    }

    // Resolver -- ops typiques d'un FPNN quantise + ops bonus pour v3
    static tflite::MicroMutableOpResolver<12> resolver;
    resolver.AddFullyConnected();
    resolver.AddReshape();
    resolver.AddQuantize();
    resolver.AddDequantize();
    resolver.AddLogistic();
    resolver.AddMul();
    resolver.AddAdd();
    resolver.AddGather();
    resolver.AddConcatenation();
    resolver.AddRelu();
    resolver.AddTanh();
    resolver.AddPack();

    static tflite::MicroInterpreter interpreter(model, resolver, s_arena, kArenaSize);
    if (interpreter.AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors failed -- arena too small ?");
        heap_caps_free(s_arena);
        s_arena = nullptr;
        return ESP_FAIL;
    }
    s_interpreter = &interpreter;

    TfLiteTensor *in = s_interpreter->input(0);
    TfLiteTensor *out = s_interpreter->output(0);
    ESP_LOGI(TAG, "model loaded: arena_used=%u/%u bytes input.type=%d in_size=%d out_size=%d",
             (unsigned)interpreter.arena_used_bytes(),
             (unsigned)kArenaSize,
             (int)in->type,
             (int)in->bytes,
             (int)out->bytes);

    // Dry-run : zero-fill input + Invoke pour valider la chaine
    if (in->type == kTfLiteInt8) {
        memset(in->data.int8, 0, in->bytes);
    } else if (in->type == kTfLiteFloat32) {
        memset(in->data.f, 0, in->bytes);
    }

    int64_t t0 = esp_timer_get_time();
    TfLiteStatus inv = s_interpreter->Invoke();
    int64_t dt_us = esp_timer_get_time() - t0;
    if (inv != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke (dry-run) failed");
        // On garde s_arena alloue pour ne pas leak ; s_ready reste false.
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "dry-run inference OK in %lld us", (long long)dt_us);

    s_ready = true;
    return ESP_OK;
}

// --- Inference per-battery (utilisee uniquement si n_bat > 0) ---
//
// Sur le bench Phase 15, n_bat = 0, donc cette fonction n'est jamais
// invoquee. Elle est definie pour valider la compilation et preparer
// Phase 16. Le modele v3 attend 13 features (heritage v1) -- si la signature
// diverge, la fonction se rabat sur un zero-fill (input->bytes octets).
static int infer_battery(const BmuBatteryC *bat, uint8_t *out_pct, uint32_t *out_lat_us)
{
    if (!s_ready || s_interpreter == nullptr || bat == nullptr || out_pct == nullptr) {
        return -1;
    }

    TfLiteTensor *in = s_interpreter->input(0);
    int input_count = (int)(in->bytes / (in->type == kTfLiteInt8 ? 1 : 4));

    // Features minimales depuis le snapshot (5 features sur 13 -- le reste
    // sera 0). Phase 16 enrichira via les accumulateurs glissants comme v1.
    float feats[NUM_FEATURES] = {0};
    feats[0]  = bat->voltage_mv / 1000.0f;
    feats[2]  = bat->current_ma / 1000.0f;
    feats[6]  = bat->ah_remaining_ma_h / 1000.0f;
    feats[8]  = bat->temp_c10 / 10.0f;
    feats[11] = (float)bat->switch_count;

    // Normalisation
    float normed[NUM_FEATURES];
    for (int f = 0; f < NUM_FEATURES; f++) {
        float std = s_feat_stds[f];
        if (std < 1e-6f) std = 1.0f;
        normed[f] = (feats[f] - s_feat_means[f]) / std;
    }

    if (in->type == kTfLiteInt8) {
        const float scale = in->params.scale;
        const int zp = in->params.zero_point;
        int8_t *data = in->data.int8;
        memset(data, zp, in->bytes);
        int n = (input_count < NUM_FEATURES) ? input_count : NUM_FEATURES;
        for (int f = 0; f < n; f++) {
            int32_t q = (int32_t)(normed[f] / scale) + zp;
            if (q < -128) q = -128;
            if (q > 127) q = 127;
            data[f] = (int8_t)q;
        }
    } else if (in->type == kTfLiteFloat32) {
        memset(in->data.f, 0, in->bytes);
        int n = (input_count < NUM_FEATURES) ? input_count : NUM_FEATURES;
        for (int f = 0; f < n; f++) {
            in->data.f[f] = normed[f];
        }
    }

    int64_t t0 = esp_timer_get_time();
    if (s_interpreter->Invoke() != kTfLiteOk) {
        return -1;
    }
    *out_lat_us = (uint32_t)(esp_timer_get_time() - t0);

    TfLiteTensor *out = s_interpreter->output(0);
    float soh = 0.0f;
    if (out->type == kTfLiteInt8) {
        soh = ((float)out->data.int8[0] - out->params.zero_point) * out->params.scale;
    } else if (out->type == kTfLiteFloat32) {
        soh = out->data.f[0];
    }
    if (soh < 0.0f) soh = 0.0f;
    if (soh > 1.0f) soh = 1.0f;
    *out_pct = (uint8_t)(soh * 100.0f + 0.5f);
    return 0;
}

// --- Task body ---
static constexpr uint32_t TASK_PERIOD_MS   = 60 * 1000;
static constexpr UBaseType_t TASK_PRIO     = 2;
static constexpr uint32_t TASK_STACK_BYTES = 16 * 1024;
static constexpr BaseType_t PIN_APP_CPU    = 1;

static void task_soh_body(void *arg)
{
    BmuCore *core = static_cast<BmuCore *>(arg);
    ESP_LOGI(TAG, "task_soh started: core=APP_CPU prio=%u period=%lu ms ready=%d",
             (unsigned)TASK_PRIO, (unsigned long)TASK_PERIOD_MS, (int)s_ready);

    BmuSnapshotC snap;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        if (s_ready && core != nullptr) {
            memset(&snap, 0, sizeof(snap));
            int rc = bmu_core_get_cached_snapshot(core, &snap);
            if (rc == 0 && snap.n_bat > 0) {
                for (uint8_t i = 0; i < snap.n_bat && i < MAX_BATTERIES; i++) {
                    uint8_t pct = 0;
                    uint32_t lat_us = 0;
                    if (infer_battery(&snap.batteries[i], &pct, &lat_us) == 0) {
                        ESP_LOGI(TAG, "soh[%u] = %u%% lat=%lu us (push-back deferred to Phase 16)",
                                 (unsigned)i, (unsigned)pct, (unsigned long)lat_us);
                    } else {
                        ESP_LOGW(TAG, "infer_battery[%u] failed", (unsigned)i);
                    }
                }
            } else {
                ESP_LOGI(TAG, "task_soh idle: n_bat=%u (bench has no INA fleet)",
                         (unsigned)(rc == 0 ? snap.n_bat : 0));
            }
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_MS));
    }
}

extern "C" void task_soh_start(BmuCore *core)
{
    TaskHandle_t handle = nullptr;
    BaseType_t ok = xTaskCreatePinnedToCore(
        task_soh_body,
        "soh",
        TASK_STACK_BYTES,
        (void *)core,
        TASK_PRIO,
        &handle,
        PIN_APP_CPU);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore(task_soh) failed");
        return;
    }
    ESP_LOGI(TAG, "task_soh created handle=%p pinned APP_CPU prio=%u",
             (void *)handle, (unsigned)TASK_PRIO);
}
