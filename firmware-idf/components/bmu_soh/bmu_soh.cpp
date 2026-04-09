/**
 * @file bmu_soh.cpp
 * @brief FPNN SOH prediction via TFLite Micro (INT8 quantized).
 *
 * Model: 13 features -> polynomial expansion (degree 2) -> FC(104,64) -> ReLU -> FC(64,1) -> Sigmoid
 * Trained on 11887 samples, MAPE 2.44% (float32), ~18% (INT8).
 */

#include "bmu_soh.h"
#include "bmu_ina237.h"
#if CONFIG_BMU_RINT_ENABLED
#include "bmu_rint.h"
#endif

#include "esp_log.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include <cmath>
#include <cstring>

static const char *TAG = "SOH";

/* ── Embedded model binary ────────────────────────────────────────── */

extern const uint8_t model_start[] asm("_binary_fpnn_soh_int8_tflite_start");
extern const uint8_t model_end[]   asm("_binary_fpnn_soh_int8_tflite_end");

/* ── Normalisation constants (from training checkpoint) ───────────── */

#define NUM_FEATURES 13

/* Updated 2026-04-08 from fpnn_soh.pt checkpoint (450K samples, 3 devices) */
static const float FEAT_MEANS[NUM_FEATURES] = {
    27.3286f, 0.3091f, 0.0820f, 0.4863f, -0.0699f, 0.0025f,
    0.1654f, 0.2979f, 27.0195f, 27.6376f, 0.9870f, 59.7926f, 0.5576f
};

static const float FEAT_STDS[NUM_FEATURES] = {
    1.5719f, 0.6584f, 0.8626f, 1.4825f, 0.5787f, 0.6206f,
    6.2368f, 5.2352f, 1.6740f, 1.7339f, 1.8827f, 21.1871f, 1.5358f
};

/* ── TFLite Micro state ───────────────────────────────────────────── */

static constexpr int kArenaSize = CONFIG_BMU_SOH_ARENA_KB * 1024;
static uint8_t s_arena[kArenaSize] __attribute__((aligned(16)));
static tflite::MicroInterpreter *s_interpreter = nullptr;
static bool s_ready = false;

/* ── SOH cache ────────────────────────────────────────────────────── */

static float s_soh_cache[BMU_MAX_BATTERIES];

/* ── Feature accumulation state (per battery) ─────────────────────── */

typedef struct {
    float v_sum, v_sq_sum;
    float i_sum, i_sq_sum;
    float v_min, v_max, i_max;
    float prev_v, prev_i;
    int   n;
} bmu_soh_accum_t;

static bmu_soh_accum_t s_accum[BMU_MAX_BATTERIES];

/* ── Init ─────────────────────────────────────────────────────────── */

esp_err_t bmu_soh_init(void)
{
    if (s_ready) return ESP_OK;

    for (int i = 0; i < BMU_MAX_BATTERIES; i++) {
        s_soh_cache[i] = -1.0f;
        memset(&s_accum[i], 0, sizeof(bmu_soh_accum_t));
        s_accum[i].v_min = 99999.0f;
    }

    const tflite::Model *model = tflite::GetModel(model_start);
    if (!model) {
        ESP_LOGE(TAG, "Failed to load TFLite model");
        return ESP_FAIL;
    }
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema %lu != expected %d",
                 (unsigned long)model->version(), TFLITE_SCHEMA_VERSION);
        return ESP_FAIL;
    }

    /* Register only the ops our FPNN needs */
    static tflite::MicroMutableOpResolver<6> resolver;
    resolver.AddFullyConnected();
    resolver.AddReshape();
    resolver.AddQuantize();
    resolver.AddDequantize();
    resolver.AddLogistic();  /* Sigmoid */
    resolver.AddMul();       /* Polynomial expansion element-wise multiply */

    static tflite::MicroInterpreter interpreter(model, resolver, s_arena, kArenaSize);
    if (interpreter.AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors failed");
        return ESP_FAIL;
    }

    s_interpreter = &interpreter;
    s_ready = true;

    ESP_LOGI(TAG, "TFLite Micro ready — arena %zu/%d bytes",
             interpreter.arena_used_bytes(), kArenaSize);
    return ESP_OK;
}

/* ── Single-battery inference ─────────────────────────────────────── */

float bmu_soh_predict(bmu_battery_manager_t *mgr,
                      bmu_protection_ctx_t *prot,
                      int idx)
{
    if (!s_ready || !mgr || !prot || idx < 0 || idx >= mgr->nb_ina)
        return -1.0f;

    /* ── Collect raw features from existing APIs ── */
    float v_mv = bmu_protection_get_voltage(prot, idx);
    float i_a = 0.0f;
    bmu_ina237_read_current(&mgr->ina_devices[idx], &i_a);
    float ah_d = bmu_battery_manager_get_ah_discharge(mgr, idx);
    float ah_c = bmu_battery_manager_get_ah_charge(mgr, idx);

    /* Update accumulator */
    bmu_soh_accum_t *a = &s_accum[idx];
    float v = v_mv / 1000.0f;  /* Convert mV -> V for model */
    a->v_sum += v;
    a->v_sq_sum += v * v;
    a->i_sum += i_a;
    a->i_sq_sum += i_a * i_a;
    if (v < a->v_min) a->v_min = v;
    if (v > a->v_max) a->v_max = v;
    if (fabsf(i_a) > a->i_max) a->i_max = fabsf(i_a);
    a->n++;

    if (a->n < 2) return -1.0f;  /* Need at least 2 samples for std/dV */

    float n = (float)a->n;
    float v_mean = a->v_sum / n;
    float v_std  = sqrtf(fmaxf(0.0f, a->v_sq_sum / n - v_mean * v_mean));
    float i_mean = a->i_sum / n;
    float i_std  = sqrtf(fmaxf(0.0f, a->i_sq_sum / n - i_mean * i_mean));
    float dv_dt  = (a->n > 1) ? (v - a->prev_v) : 0.0f;
    float di_dt  = (a->n > 1) ? (i_a - a->prev_i) : 0.0f;

    /* Resistance interne : priorité à la mesure rint cachée, fallback dV/dI */
    float r_int;
#if CONFIG_BMU_RINT_ENABLED
    bmu_rint_result_t rint_cached = bmu_rint_get_cached(idx);
    if (rint_cached.valid) {
        r_int = rint_cached.r_total_mohm / 1000.0f;  /* mΩ → Ω */
    } else {
        r_int = (fabsf(di_dt) > 0.001f) ? fabsf(dv_dt / di_dt) : 0.05f;
    }
#else
    r_int = (fabsf(di_dt) > 0.001f) ? fabsf(dv_dt / di_dt) : 0.05f;
#endif

    a->prev_v = v;
    a->prev_i = i_a;

    /* Build 13-feature vector (same order as training) */
    float features[NUM_FEATURES] = {
        v_mean, v_std, i_mean, i_std, dv_dt, di_dt,
        ah_d, ah_c, a->v_min, a->v_max, a->i_max,
        n, r_int
    };

    /* Normalise */
    float normed[NUM_FEATURES];
    for (int f = 0; f < NUM_FEATURES; f++) {
        float std = FEAT_STDS[f];
        if (std < 1e-6f) std = 1.0f;
        normed[f] = (features[f] - FEAT_MEANS[f]) / std;
    }

    /* ── Run TFLite inference ── */
    TfLiteTensor *input = s_interpreter->input(0);
    if (input->type == kTfLiteInt8) {
        const float scale = input->params.scale;
        const int zp = input->params.zero_point;
        int8_t *data = input->data.int8;
        for (int f = 0; f < NUM_FEATURES; f++) {
            int32_t q = (int32_t)(normed[f] / scale) + zp;
            if (q < -128) q = -128;
            if (q > 127) q = 127;
            data[f] = (int8_t)q;
        }
    } else {
        memcpy(input->data.f, normed, NUM_FEATURES * sizeof(float));
    }

    if (s_interpreter->Invoke() != kTfLiteOk) {
        ESP_LOGW(TAG, "Invoke failed for battery %d", idx);
        return -1.0f;
    }

    TfLiteTensor *output = s_interpreter->output(0);
    float soh;
    if (output->type == kTfLiteInt8) {
        soh = ((float)output->data.int8[0] - output->params.zero_point)
              * output->params.scale;
    } else {
        soh = output->data.f[0];
    }

    /* Clamp to [0, 1] */
    if (soh < 0.0f) soh = 0.0f;
    if (soh > 1.0f) soh = 1.0f;

    return soh;
}

/* ── Batch update ─────────────────────────────────────────────────── */

void bmu_soh_update_all(bmu_battery_manager_t *mgr,
                        bmu_protection_ctx_t *prot,
                        int nb_ina)
{
    for (int i = 0; i < nb_ina && i < BMU_MAX_BATTERIES; i++) {
        float soh = bmu_soh_predict(mgr, prot, i);
        if (soh >= 0.0f) {
            s_soh_cache[i] = soh;
        }
    }
}

float bmu_soh_get_cached(int idx)
{
    if (idx < 0 || idx >= BMU_MAX_BATTERIES) return -1.0f;
    return s_soh_cache[idx];
}
