/**
 * @file bmu_rint.cpp
 * @brief Mesure de résistance interne (R_int) des batteries BMU.
 *
 * Séquence de mesure active :
 *   1. Lecture V1/I1 sous charge
 *   2. Switch OFF → attente PULSE_FAST_MS → lecture V2 (R ohmique)
 *   3. Attente (PULSE_TOTAL_MS - PULSE_FAST_MS) → lecture V3 (R totale)
 *   4. Switch ON → calcul R_ohmic / R_total → cache → routage sortie
 *
 * Calcul :
 *   R_ohmic = (V2 - V1) / |I1|   [mΩ, V en mV, I en A]
 *   R_total  = (V3 - V1) / |I1|   [mΩ]
 *
 * Le contexte protection est passé via bmu_rint_set_ctx() avant toute mesure.
 * Cette fonction est appelée par main lors de l'intégration du composant.
 */

#include "bmu_rint.h"
#include "bmu_protection.h"
#include "bmu_ina237.h"
#include "bmu_tca9535.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <cmath>
#include <cstring>

static const char *TAG = "RINT";

/* ── Milliseconds depuis le boot ──────────────────────────────────────── */
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* ── Déclaration forward du routeur de sortie (implémenté dans Task 4) ── */
extern "C" void rint_output_route(uint8_t idx, bmu_rint_trigger_t trigger,
                                  const bmu_rint_result_t *res);

/* ── État statique du module ──────────────────────────────────────────── */
static bmu_protection_ctx_t *s_prot       = NULL;
static bmu_rint_result_t     s_cache[BMU_MAX_BATTERIES];
static SemaphoreHandle_t     s_mutex      = NULL;
static volatile bool         s_measuring  = false;
static TaskHandle_t          s_task_handle = NULL;

/**
 * @brief Injecte le contexte protection utilisé par toutes les mesures.
 *
 * Doit être appelée par main.cpp après bmu_protection_init() et avant
 * bmu_rint_measure() ou bmu_rint_start_periodic().
 *
 * Note : non déclarée dans le .h public car interne à l'intégration main.
 */
extern "C" void bmu_rint_set_ctx(bmu_protection_ctx_t *ctx)
{
    s_prot = ctx;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Helpers internes
 * ═══════════════════════════════════════════════════════════════════════ */

/* Nombre de batteries en état CONNECTED */
static int count_connected(void)
{
    if (s_prot == NULL) return 0;
    int count = 0;
    for (int i = 0; i < s_prot->nb_ina; i++) {
        if (bmu_protection_get_state(s_prot, i) == BMU_STATE_CONNECTED) {
            count++;
        }
    }
    return count;
}

/* Vérifie si au moins une batterie est en ERROR ou LOCKED */
static bool has_error_or_locked(void)
{
    if (s_prot == NULL) return true;
    for (int i = 0; i < s_prot->nb_ina; i++) {
        bmu_battery_state_t st = bmu_protection_get_state(s_prot, i);
        if (st == BMU_STATE_ERROR || st == BMU_STATE_LOCKED) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Vérifie toutes les préconditions de sécurité avant mesure.
 *
 * @return ESP_OK si la mesure peut commencer, code d'erreur sinon.
 */
static esp_err_t guard_check(uint8_t idx)
{
    /* Contexte disponible */
    if (s_prot == NULL) {
        ESP_LOGW(TAG, "guard: contexte protection non initialisé");
        return ESP_ERR_INVALID_STATE;
    }

    /* Index valide */
    if (idx >= s_prot->nb_ina) {
        ESP_LOGW(TAG, "guard: idx %d hors plage (nb_ina=%d)", idx, s_prot->nb_ina);
        return ESP_ERR_INVALID_ARG;
    }

    /* Mesure déjà en cours */
    if (s_measuring) {
        ESP_LOGD(TAG, "guard: mesure déjà en cours");
        return ESP_ERR_INVALID_STATE;
    }

    /* Minimum 2 batteries connectées (une doit rester en ligne pendant le OFF) */
    int nb_conn = count_connected();
    if (nb_conn < 2) {
        ESP_LOGW(TAG, "guard: seulement %d batterie(s) connectée(s)", nb_conn);
        return ESP_ERR_INVALID_STATE;
    }

    /* Aucune batterie en erreur ou verrouillée */
    if (has_error_or_locked()) {
        ESP_LOGW(TAG, "guard: batterie en erreur ou verrouillée");
        return ESP_ERR_INVALID_STATE;
    }

    /* La batterie cible doit être CONNECTED */
    bmu_battery_state_t state = bmu_protection_get_state(s_prot, idx);
    if (state != BMU_STATE_CONNECTED) {
        ESP_LOGD(TAG, "guard: batterie %d non connectée (état=%d)", idx, (int)state);
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

/**
 * @brief Calcule R_ohmic et R_total à partir des mesures et valide le résultat.
 */
static bmu_rint_result_t compute_result(float v_load, float i_load,
                                        float v_fast, float v_stable,
                                        int64_t timestamp)
{
    bmu_rint_result_t res = {};
    res.v_load_mv       = v_load;
    res.i_load_a        = i_load;
    res.v_ocv_fast_mv   = v_fast;
    res.v_ocv_stable_mv = v_stable;
    res.timestamp_ms    = timestamp;
    res.valid           = false;

    float i_abs = fabsf(i_load);

    /* Courant minimum */
    const float i_min_a = CONFIG_BMU_RINT_MIN_CURRENT_MA / 1000.0f;
    if (i_abs < i_min_a) {
        ESP_LOGD(TAG, "compute: courant insuffisant %.3f A (min=%.3f)", i_abs, i_min_a);
        return res;
    }

    /* dV doit être positif (tension rebondit après déconnexion) */
    float dv_fast   = v_fast   - v_load;
    float dv_stable = v_stable - v_load;

    if (dv_fast <= 0.0f) {
        ESP_LOGD(TAG, "compute: dV_fast négatif (%.1f mV)", dv_fast);
        return res;
    }
    if (dv_stable <= 0.0f) {
        ESP_LOGD(TAG, "compute: dV_stable négatif (%.1f mV)", dv_stable);
        return res;
    }

    /* R = dV [mV] / I [A] → résultat en mΩ */
    float r_ohmic = dv_fast   / i_abs;
    float r_total = dv_stable / i_abs;

    /* Validation plage */
    if (r_ohmic > (float)CONFIG_BMU_RINT_R_MAX_MOHM) {
        ESP_LOGW(TAG, "compute: R_ohmic %.1f mΩ > max %d mΩ",
                 r_ohmic, CONFIG_BMU_RINT_R_MAX_MOHM);
        return res;
    }

    /* R_total doit être >= R_ohmic */
    if (r_total < r_ohmic) {
        ESP_LOGD(TAG, "compute: R_total (%.1f) < R_ohmic (%.1f)", r_total, r_ohmic);
        return res;
    }

    res.r_ohmic_mohm = r_ohmic;
    res.r_total_mohm = r_total;
    res.valid        = true;

    ESP_LOGI(TAG, "R_ohmic=%.1f mΩ  R_total=%.1f mΩ  I=%.3f A",
             r_ohmic, r_total, i_load);
    return res;
}

/* ═══════════════════════════════════════════════════════════════════════
 * API publique
 * ═══════════════════════════════════════════════════════════════════════ */

esp_err_t bmu_rint_init(void)
{
    if (s_mutex != NULL) {
        return ESP_OK; /* Déjà initialisé */
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Échec création mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(s_cache, 0, sizeof(s_cache));
    s_measuring    = false;
    s_task_handle  = NULL;

    ESP_LOGI(TAG, "R_int init OK");
    return ESP_OK;
}

esp_err_t bmu_rint_measure(uint8_t battery_idx, bmu_rint_trigger_t trigger)
{
    /* Préconditions de sécurité */
    esp_err_t ret = guard_check(battery_idx);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t tca_idx = battery_idx / 4;
    uint8_t channel = battery_idx % 4;

    s_measuring = true;
    ESP_LOGI(TAG, "Mesure R_int batterie %d (trigger=%d)", battery_idx, (int)trigger);

    /* ── Étape 1 : lecture V1/I1 sous charge ─────────────────────────── */
    float v1 = 0.0f, i1 = 0.0f;
    ret = bmu_ina237_read_voltage_current(&s_prot->ina_devices[battery_idx], &v1, &i1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Bat %d : erreur lecture V1/I1 (%s)",
                 battery_idx, esp_err_to_name(ret));
        s_measuring = false;
        return ret;
    }
    int64_t ts = now_ms();

    /* ── Étape 2 : switch OFF ─────────────────────────────────────────── */
    ret = bmu_tca9535_switch_battery(&s_prot->tca_devices[tca_idx], channel, false);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Bat %d : erreur switch OFF (%s)",
                 battery_idx, esp_err_to_name(ret));
        s_measuring = false;
        return ret;
    }

    /* ── Attente PULSE_FAST_MS → lecture V2 (ohmique) ────────────────── */
    vTaskDelay(pdMS_TO_TICKS(CONFIG_BMU_RINT_PULSE_FAST_MS));

    /* Vérification d'erreur intercalée avant V2 */
    if (has_error_or_locked()) {
        ESP_LOGW(TAG, "Bat %d : erreur/lock détectée pendant pulse — abandon", battery_idx);
        bmu_tca9535_switch_battery(&s_prot->tca_devices[tca_idx], channel, true);
        s_measuring = false;
        return ESP_FAIL;
    }

    float v2 = 0.0f, dummy = 0.0f;
    ret = bmu_ina237_read_voltage_current(&s_prot->ina_devices[battery_idx], &v2, &dummy);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Bat %d : erreur lecture V2 (%s)",
                 battery_idx, esp_err_to_name(ret));
        bmu_tca9535_switch_battery(&s_prot->tca_devices[tca_idx], channel, true);
        s_measuring = false;
        return ret;
    }

    /* ── Attente complémentaire → lecture V3 (totale) ────────────────── */
    int rest_ms = CONFIG_BMU_RINT_PULSE_TOTAL_MS - CONFIG_BMU_RINT_PULSE_FAST_MS;
    if (rest_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(rest_ms));
    }

    /* Vérification d'erreur intercalée avant V3 */
    if (has_error_or_locked()) {
        ESP_LOGW(TAG, "Bat %d : erreur/lock avant V3 — abandon", battery_idx);
        bmu_tca9535_switch_battery(&s_prot->tca_devices[tca_idx], channel, true);
        s_measuring = false;
        return ESP_FAIL;
    }

    float v3 = 0.0f;
    ret = bmu_ina237_read_voltage_current(&s_prot->ina_devices[battery_idx], &v3, &dummy);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Bat %d : erreur lecture V3 (%s)",
                 battery_idx, esp_err_to_name(ret));
        bmu_tca9535_switch_battery(&s_prot->tca_devices[tca_idx], channel, true);
        s_measuring = false;
        return ret;
    }

    /* ── Étape 5 : switch ON ─────────────────────────────────────────── */
    bmu_tca9535_switch_battery(&s_prot->tca_devices[tca_idx], channel, true);
    s_measuring = false;

    /* ── Calcul et mise en cache ─────────────────────────────────────── */
    bmu_rint_result_t result = compute_result(v1, i1, v2, v3, ts);

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_cache[battery_idx] = result;
        xSemaphoreGive(s_mutex);
    }

    /* ── Routage sortie (MQTT, InfluxDB, Display) ────────────────────── */
    rint_output_route(battery_idx, trigger, &result);

    return result.valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t bmu_rint_measure_all(bmu_rint_trigger_t trigger)
{
    if (s_prot == NULL || s_prot->nb_ina == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Mesure R_int de toutes les batteries connectées");

    bool at_least_one_ok = false;

    for (int i = 0; i < s_prot->nb_ina; i++) {
        if (bmu_protection_get_state(s_prot, i) != BMU_STATE_CONNECTED) {
            continue;
        }

        esp_err_t ret = bmu_rint_measure((uint8_t)i, trigger);
        if (ret == ESP_OK) {
            at_least_one_ok = true;
        } else {
            ESP_LOGD(TAG, "Bat %d : mesure échouée (%s)", i, esp_err_to_name(ret));
        }

        /* Pause entre mesures pour laisser les batteries se stabiliser */
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    return at_least_one_ok ? ESP_OK : ESP_FAIL;
}

bmu_rint_result_t bmu_rint_get_cached(uint8_t battery_idx)
{
    bmu_rint_result_t result = {};

    if (battery_idx >= BMU_MAX_BATTERIES || s_mutex == NULL) {
        return result;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        result = s_cache[battery_idx];
        xSemaphoreGive(s_mutex);
    }

    return result;
}

void bmu_rint_on_disconnect(uint8_t battery_idx, float v_before_mv, float i_before_a)
{
    /* Mesure opportuniste : la batterie vient d'être déconnectée par la protection.
     * On ne la reconnecte pas — on lit simplement V2 et V3 aux délais attendus. */

    if (s_prot == NULL || battery_idx >= s_prot->nb_ina) {
        return;
    }

    if (s_measuring) {
        ESP_LOGD(TAG, "Bat %d opportuniste : mesure active deja en cours", battery_idx);
        return;
    }

    const float i_min_a = CONFIG_BMU_RINT_MIN_CURRENT_MA / 1000.0f;
    if (v_before_mv < BMU_MIN_VOLTAGE_MV || fabsf(i_before_a) < i_min_a) {
        ESP_LOGD(TAG, "Bat %d opportuniste : skip (V=%.0f mV I=%.3f A)",
                 battery_idx, v_before_mv, i_before_a);
        return;
    }

    s_measuring = true;
    ESP_LOGI(TAG, "Bat %d : mesure opportuniste (V_before=%.0f mV, I=%.3f A)",
             battery_idx, v_before_mv, i_before_a);

    int64_t ts = now_ms();

    /* Attente → V2 ohmique */
    vTaskDelay(pdMS_TO_TICKS(CONFIG_BMU_RINT_PULSE_FAST_MS));

    float v2 = 0.0f, dummy = 0.0f;
    esp_err_t ret = bmu_ina237_read_voltage_current(
        &s_prot->ina_devices[battery_idx], &v2, &dummy);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Bat %d opportuniste : erreur V2", battery_idx);
        s_measuring = false;
        return;
    }

    /* Attente complémentaire → V3 totale */
    int rest_ms = CONFIG_BMU_RINT_PULSE_TOTAL_MS - CONFIG_BMU_RINT_PULSE_FAST_MS;
    if (rest_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(rest_ms));
    }

    float v3 = 0.0f;
    ret = bmu_ina237_read_voltage_current(
        &s_prot->ina_devices[battery_idx], &v3, &dummy);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Bat %d opportuniste : erreur V3", battery_idx);
        s_measuring = false;
        return;
    }

    bmu_rint_result_t result = compute_result(v_before_mv, i_before_a, v2, v3, ts);

    if (s_mutex != NULL && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_cache[battery_idx] = result;
        xSemaphoreGive(s_mutex);
    }

    rint_output_route(battery_idx, BMU_RINT_TRIGGER_OPPORTUNISTIC, &result);
    s_measuring = false;
}

/* ── Tâche périodique ─────────────────────────────────────────────────── */
static void rint_periodic_task(void *pv)
{
    /* Attente initiale de 2 minutes au boot */
    vTaskDelay(pdMS_TO_TICKS(2UL * 60UL * 1000UL));

    for (;;) {
        ESP_LOGI(TAG, "Mesure périodique R_int");
        bmu_rint_measure_all(BMU_RINT_TRIGGER_PERIODIC);

        /* Pause jusqu'à la prochaine mesure */
        vTaskDelay(pdMS_TO_TICKS((uint32_t)CONFIG_BMU_RINT_PERIOD_MIN * 60UL * 1000UL));
    }
}

esp_err_t bmu_rint_start_periodic(void)
{
#ifndef CONFIG_BMU_RINT_PERIODIC_ENABLED
    ESP_LOGI(TAG, "Mesure périodique désactivée (Kconfig)");
    return ESP_OK;
#endif

    if (s_task_handle != NULL) {
        ESP_LOGW(TAG, "Tâche périodique déjà démarrée");
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreate(rint_periodic_task, "rint_periodic",
                                CONFIG_BMU_RINT_TASK_STACK, NULL,
                                CONFIG_BMU_RINT_TASK_PRIORITY, &s_task_handle);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Échec création tâche périodique");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Tâche périodique R_int démarrée (période %d min)",
             CONFIG_BMU_RINT_PERIOD_MIN);
    return ESP_OK;
}
