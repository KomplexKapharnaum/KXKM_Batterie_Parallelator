#include "bmu_protection.h"
#include "bmu_rint.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>
#include <cstring>

static const char *TAG = "BATT";

/* Milliseconds since boot */
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

esp_err_t bmu_protection_init(bmu_protection_ctx_t *ctx,
                               bmu_ina237_t *ina, uint8_t nb_ina,
                               bmu_tca9535_handle_t *tca, uint8_t nb_tca)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->ina_devices = ina;
    ctx->tca_devices = tca;
    ctx->nb_ina = nb_ina;
    ctx->nb_tca = nb_tca;
    ctx->state_mutex = xSemaphoreCreateMutex();
    configASSERT(ctx->state_mutex != NULL);

    /* All batteries start DISCONNECTED */
    for (int i = 0; i < BMU_MAX_BATTERIES; i++) {
        ctx->battery_state[i] = BMU_STATE_DISCONNECTED;
    }

    ESP_LOGI(TAG, "Protection init: %d INA, %d TCA", nb_ina, nb_tca);
    return ESP_OK;
}

/* ── Helper: switch battery ON/OFF via TCA ─────────────────────────── */
static esp_err_t switch_battery(bmu_protection_ctx_t *ctx, int idx, bool on)
{
    if (idx < 0 || idx >= ctx->nb_ina) return ESP_ERR_INVALID_ARG;
    int tca_idx = idx / 4;
    int channel = idx % 4;
    if (tca_idx >= ctx->nb_tca) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = bmu_tca9535_switch_battery(&ctx->tca_devices[tca_idx], channel, on);
    if (ret != ESP_OK) return ret;

    /* LED: green=ON/connected, red=OFF/fault */
    ret = bmu_tca9535_set_led(&ctx->tca_devices[tca_idx], channel, !on, on);

    if (on) {
        vTaskDelay(pdMS_TO_TICKS(100)); /* MOSFET dead-time protection */
    } else {
        vTaskDelay(pdMS_TO_TICKS(50));  /* Wait for switch off */
    }

    ESP_LOGI(TAG, "Battery %d %s (TCA%d CH%d)", idx + 1, on ? "ON" : "OFF", tca_idx, channel);
    return ret;
}

/* ── Helper: is voltage within [min, max] range (mV) ──────────────── */
static bool is_voltage_in_range(float voltage_mv)
{
    if (voltage_mv < BMU_MIN_VOLTAGE_MV || voltage_mv > BMU_MAX_VOLTAGE_MV) {
        ESP_LOGD(TAG, "V out of range: %.0f mV (min=%d max=%d)",
                 voltage_mv, BMU_MIN_VOLTAGE_MV, BMU_MAX_VOLTAGE_MV);
        return false;
    }
    return true;
}

/* ── Helper: is |current| within max (mA→A conversion) ────────────── */
static bool is_current_in_range(float current_a)
{
    const float max_a = BMU_MAX_CURRENT_MA / 1000.0f;
    if (fabs(current_a) > max_a) {
        ESP_LOGD(TAG, "I out of range: %.3f A (max=%.1f)", current_a, max_a);
        return false;
    }
    return true;
}

/* ── Helper: voltage imbalance check against fleet max ─────────────── */
static bool is_imbalance_ok(float voltage_mv, float fleet_max_mv)
{
    const float diff_mv = fleet_max_mv - voltage_mv;
    if (diff_mv > BMU_VOLTAGE_DIFF_MV) {
        ESP_LOGD(TAG, "Imbalance: V=%.0f fleet_max=%.0f diff=%.0f > %d mV",
                 voltage_mv, fleet_max_mv, diff_mv, BMU_VOLTAGE_DIFF_MV);
        return false;
    }
    return true;
}

/* ── Helper: find max voltage across CONNECTED batteries only ─────── */
static float find_fleet_max_mv(bmu_protection_ctx_t *ctx)
{
    float max_mv = 0;
    for (int i = 0; i < ctx->nb_ina; i++) {
        /* Ignorer les batteries OFF/ERROR/LOCKED — leur tension cachee
         * est stale et provoquerait une cascade d'imbalance */
        bmu_battery_state_t st = ctx->battery_state[i];
        if (st != BMU_STATE_CONNECTED && st != BMU_STATE_RECONNECTING) continue;
        if (ctx->battery_voltages[i] > max_mv) {
            max_mv = ctx->battery_voltages[i];
        }
    }
    return max_mv;
}

/* ── Main state machine — called once per battery per loop ─────────── */
esp_err_t bmu_protection_check_battery(bmu_protection_ctx_t *ctx, int idx)
{
    if (idx < 0 || idx >= ctx->nb_ina) return ESP_ERR_INVALID_ARG;

    /* Skip si deja LOCKED — ne pas re-log ni re-switch inutilement */
    if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        bmu_battery_state_t cur = ctx->battery_state[idx];
        xSemaphoreGive(ctx->state_mutex);
        if (cur == BMU_STATE_LOCKED) return ESP_OK;
    }

    /* Read voltage (mV) and current (A) from INA237 */
    float v_mv = 0, i_a = 0;
    esp_err_t ret = bmu_ina237_read_voltage_current(&ctx->ina_devices[idx], &v_mv, &i_a);
    if (ret != ESP_OK || std::isnan(v_mv) || std::isnan(i_a)) {
        ESP_LOGW(TAG, "BAT[%d] I2C read error — skip protection", idx + 1);
        if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            ctx->battery_voltages[idx] = 0;
            xSemaphoreGive(ctx->state_mutex);
        }
        return ret;
    }

    /* Rejet des lectures aberrantes — donnee corrompue par glitch I2C.
     * Symptomes observes : V/2 (14.4V au lieu de 28.8V), V*1.4 (40V).
     * Filtre : si la batterie est CONNECTED/RECONNECTING et que la tension
     * s'ecarte de >30% de la derniere valeur connue, c'est un artefact. */
    if (v_mv > 35000 || fabs(i_a) > 50.0f) {
        ESP_LOGW(TAG, "BAT[%d] lecture aberrante V=%.0f I=%.1f — skip",
                 idx + 1, v_mv, i_a);
        return ESP_ERR_INVALID_RESPONSE;
    }
    {
        float prev_v = 0;
        if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            prev_v = ctx->battery_voltages[idx];
            xSemaphoreGive(ctx->state_mutex);
        }
        if (prev_v > 10000 && v_mv > 1000) {
            float ratio = v_mv / prev_v;
            if (ratio < 0.7f || ratio > 1.3f) {
                ESP_LOGW(TAG, "BAT[%d] saut V: %.0f→%.0f (%.0f%%) — skip",
                         idx + 1, prev_v, v_mv, (ratio - 1.0f) * 100.0f);
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
    }

    /* Cache voltage (mutex-protected) */
    if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        ctx->battery_voltages[idx] = v_mv;
        xSemaphoreGive(ctx->state_mutex);
    }

    /* Read shared state under mutex */
    int local_nb_switch = 0;
    int64_t local_reconnect_time = 0;
    bmu_battery_state_t local_prev_state = BMU_STATE_DISCONNECTED;
    if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        local_nb_switch = ctx->nb_switch[idx];
        local_reconnect_time = ctx->reconnect_time_ms[idx];
        local_prev_state = ctx->battery_state[idx];
        xSemaphoreGive(ctx->state_mutex);
    } else {
        ESP_LOGW(TAG, "BAT[%d] mutex timeout — skip", idx + 1);
        return ESP_ERR_TIMEOUT;
    }

    /* ── Determine state ──────────────────────────────────────────── */
    bmu_battery_state_t state;

    /* Overcurrent ERROR: |I| > factor * max_current */
    const float overcurrent_a = (BMU_OVERCURRENT_FACTOR / 1000.0f) * (BMU_MAX_CURRENT_MA / 1000.0f);

    if (v_mv < 0 || fabs(i_a) > overcurrent_a) {
        state = BMU_STATE_ERROR;
    }
    /* No voltage detected */
    else if (v_mv < 1000) { /* < 1V in mV = 1000 */
        state = BMU_STATE_DISCONNECTED;
    }
    /* Permanent lock — too many reconnections (F08) */
    else if (local_nb_switch > BMU_NB_SWITCH_MAX) {
        ESP_LOGW(TAG, "BAT[%d] LOCKED (nb_switch=%d > max=%d)",
                 idx + 1, local_nb_switch, BMU_NB_SWITCH_MAX);
        switch_battery(ctx, idx, false);
        /* Update state under mutex */
        if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            ctx->battery_state[idx] = BMU_STATE_LOCKED;
            xSemaphoreGive(ctx->state_mutex);
        }
        return ESP_OK; /* Locked — no further processing until reboot */
    }
    /* Protection checks failed */
    else if (!is_voltage_in_range(v_mv) ||
             !is_current_in_range(i_a) ||
             !is_imbalance_ok(v_mv, find_fleet_max_mv(ctx))) {
        state = BMU_STATE_DISCONNECTED;
        /* DEBUG: identifier la cause de deconnexion */
        float fm = find_fleet_max_mv(ctx);
        ESP_LOGW(TAG, "BAT[%d] PROT FAIL: V=%.0f I=%.3f Vok=%d Iok=%d Imb=%d (fleet=%.0f diff=%.0f)",
                 idx + 1, v_mv, i_a,
                 is_voltage_in_range(v_mv),
                 is_current_in_range(i_a),
                 is_imbalance_ok(v_mv, fm),
                 fm, fm - v_mv);
    }
    /* Already connected and all checks pass — stay CONNECTED */
    else if (local_prev_state == BMU_STATE_CONNECTED ||
             local_prev_state == BMU_STATE_RECONNECTING) {
        state = BMU_STATE_CONNECTED;
    }
    /* First connection (never switched) */
    else if (local_nb_switch == 0) {
        state = BMU_STATE_RECONNECTING;
    }
    /* Reconnect eligible after delay */
    else if (local_nb_switch <= BMU_NB_SWITCH_MAX &&
             (now_ms() - local_reconnect_time > BMU_RECONNECT_DELAY_MS)) {
        state = BMU_STATE_RECONNECTING;
    }
    /* Waiting for reconnect delay — stay disconnected */
    else {
        state = BMU_STATE_DISCONNECTED;
    }

    /* ── Act on state ─────────────────────────────────────────────── */
    switch (state) {
    case BMU_STATE_CONNECTED:
        ESP_LOGD(TAG, "BAT[%d] connected V=%.0fmV I=%.3fA", idx + 1, v_mv, i_a);
        break;

    case BMU_STATE_RECONNECTING:
        if (is_voltage_in_range(v_mv) && is_current_in_range(i_a)) {
            ESP_LOGI(TAG, "BAT[%d] reconnecting", idx + 1);
            switch_battery(ctx, idx, true);
            if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                ctx->nb_switch[idx]++;
                ctx->reconnect_time_ms[idx] = now_ms();
                xSemaphoreGive(ctx->state_mutex);
            }
        } else {
            ESP_LOGD(TAG, "BAT[%d] reconnect criteria not met", idx + 1);
        }
        break;

    case BMU_STATE_DISCONNECTED:
        if (local_prev_state != BMU_STATE_DISCONNECTED) {
            ESP_LOGI(TAG, "BAT[%d] disconnected V=%.0fmV I=%.3fA", idx + 1, v_mv, i_a);
            switch_battery(ctx, idx, false);
        }
        /* Ne pas re-appeler switch_battery si deja OFF — economise I2C + 50ms */
#if CONFIG_BMU_RINT_ENABLED
        if (local_prev_state != BMU_STATE_DISCONNECTED) {
            bmu_rint_on_disconnect(idx, v_mv, i_a);
        }
#endif
        break;

    case BMU_STATE_ERROR:
        ESP_LOGE(TAG, "BAT[%d] ERROR V=%.0fmV I=%.3fA — immediate OFF",
                 idx + 1, v_mv, i_a);
        switch_battery(ctx, idx, false);
        /* Double-off for safety */
        {
            int tca_idx = idx / 4;
            int ch = idx % 4;
            if (tca_idx < ctx->nb_tca) {
                bmu_tca9535_switch_battery(&ctx->tca_devices[tca_idx], ch, false);
                /* Clignotement LED rouge (~1 Hz, toggle à chaque passage 500ms) */
                static bool s_blink_phase[BMU_MAX_BATTERIES] = {};
                s_blink_phase[idx] = !s_blink_phase[idx];
                bmu_tca9535_set_led(&ctx->tca_devices[tca_idx], ch,
                                    s_blink_phase[idx], false);
            }
        }
        break;

    case BMU_STATE_LOCKED:
        break; /* Already handled above */
    }

    /* Update state under mutex */
    if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        ctx->battery_state[idx] = state;
        xSemaphoreGive(ctx->state_mutex);
    }

    return ESP_OK;
}

esp_err_t bmu_protection_all_off(bmu_protection_ctx_t *ctx)
{
    for (int t = 0; t < ctx->nb_tca; t++) {
        bmu_tca9535_all_off(&ctx->tca_devices[t]);
    }
    return ESP_OK;
}

esp_err_t bmu_protection_reset_switch_count(bmu_protection_ctx_t *ctx, int idx)
{
    if (idx < 0 || idx >= BMU_MAX_BATTERIES) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        ctx->nb_switch[idx] = 0;
        ctx->reconnect_time_ms[idx] = 0;
        ctx->battery_state[idx] = BMU_STATE_DISCONNECTED;
        xSemaphoreGive(ctx->state_mutex);
    }
    return ESP_OK;
}

bmu_battery_state_t bmu_protection_get_state(bmu_protection_ctx_t *ctx, int idx)
{
    bmu_battery_state_t s = BMU_STATE_DISCONNECTED;
    if (idx >= 0 && idx < BMU_MAX_BATTERIES) {
        if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            s = ctx->battery_state[idx];
            xSemaphoreGive(ctx->state_mutex);
        }
    }
    return s;
}

float bmu_protection_get_voltage(bmu_protection_ctx_t *ctx, int idx)
{
    float v = 0;
    if (idx >= 0 && idx < BMU_MAX_BATTERIES) {
        if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            v = ctx->battery_voltages[idx];
            xSemaphoreGive(ctx->state_mutex);
        }
    }
    return v;
}

esp_err_t bmu_protection_get_switch_count(bmu_protection_ctx_t *ctx, int idx,
                                          int *switch_count_out)
{
    if (ctx == NULL || switch_count_out == NULL ||
        idx < 0 || idx >= BMU_MAX_BATTERIES) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *switch_count_out = ctx->nb_switch[idx];
    xSemaphoreGive(ctx->state_mutex);
    return ESP_OK;
}

/* ── Web-initiated switch — validates through protection (audit H-06) ─── */
esp_err_t bmu_protection_web_switch(bmu_protection_ctx_t *ctx, int idx, bool on)
{
    if (idx < 0 || idx >= ctx->nb_ina) return ESP_ERR_INVALID_ARG;

    /* Check if battery is locked */
    bmu_battery_state_t state = bmu_protection_get_state(ctx, idx);
    if (state == BMU_STATE_LOCKED) {
        ESP_LOGW(TAG, "BAT[%d] web switch rejected — LOCKED", idx + 1);
        return ESP_ERR_NOT_ALLOWED;
    }

    /* For switch ON: validate voltage is in safe range */
    if (on) {
        float v_mv = bmu_protection_get_voltage(ctx, idx);
        if (v_mv < BMU_MIN_VOLTAGE_MV || v_mv > BMU_MAX_VOLTAGE_MV) {
            ESP_LOGW(TAG, "BAT[%d] web switch ON rejected — voltage %.0f mV out of range",
                     idx + 1, v_mv);
            return ESP_ERR_INVALID_STATE;
        }
    }

    /* Execute switch via TCA */
    int tca_idx = idx / 4;
    int channel = idx % 4;
    if (tca_idx >= ctx->nb_tca) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = bmu_tca9535_switch_battery(&ctx->tca_devices[tca_idx], channel, on);
    if (ret == ESP_OK) {
        bmu_tca9535_set_led(&ctx->tca_devices[tca_idx], channel, !on, on);
        ESP_LOGI(TAG, "BAT[%d] web switch %s OK", idx + 1, on ? "ON" : "OFF");
    }
    return ret;
}

esp_err_t bmu_protection_update_topology(bmu_protection_ctx_t *ctx,
                                          uint8_t new_nb_ina,
                                          uint8_t new_nb_tca)
{
    if (new_nb_ina > BMU_MAX_BATTERIES || new_nb_tca > BMU_MAX_TCA) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t old_nb_ina = ctx->nb_ina;

    /* Init state for newly added battery slots */
    for (int i = old_nb_ina; i < new_nb_ina; i++) {
        ctx->battery_state[i] = BMU_STATE_DISCONNECTED;
        ctx->battery_voltages[i] = 0;
        ctx->nb_switch[i] = 0;
        ctx->reconnect_time_ms[i] = 0;
    }

    /* Clear state for removed battery slots */
    for (int i = new_nb_ina; i < old_nb_ina; i++) {
        ctx->battery_state[i] = BMU_STATE_DISCONNECTED;
        ctx->battery_voltages[i] = 0;
        ctx->nb_switch[i] = 0;
        ctx->reconnect_time_ms[i] = 0;
    }

    ctx->nb_ina = new_nb_ina;
    ctx->nb_tca = new_nb_tca;

    xSemaphoreGive(ctx->state_mutex);

    if (new_nb_ina != old_nb_ina) {
        ESP_LOGI(TAG, "Topology updated: %d→%d INA, %d TCA",
                 old_nb_ina, new_nb_ina, new_nb_tca);
    }

    return ESP_OK;
}
