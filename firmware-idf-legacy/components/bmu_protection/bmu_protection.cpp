#include "bmu_protection.h"
#include "bmu_balancer.h"
#include "bmu_i2c.h"
#include "bmu_rint.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
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

    /* All batteries start DISCONNECTED, health at max */
    for (int i = 0; i < BMU_MAX_BATTERIES; i++) {
        ctx->battery_state[i] = BMU_STATE_DISCONNECTED;
        ctx->ina_health[i].score = BMU_HEALTH_SCORE_INIT;
        ctx->ina_health[i].consec_fails = 0;
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
        vTaskDelay(pdMS_TO_TICKS(10));  /* MOSFET dead-time (IRF4905 ~50ns, 10ms = marge 200000×) */
    } else {
        vTaskDelay(pdMS_TO_TICKS(5));   /* Attente switch off */
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

/* ── Compute fleet_max under mutex (call once per cycle) ──────────── */
float bmu_protection_compute_fleet_max(bmu_protection_ctx_t *ctx)
{
    float max_mv = 0;
    if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        for (int i = 0; i < ctx->nb_ina; i++) {
            bmu_battery_state_t st = ctx->battery_state[i];
            if (st != BMU_STATE_CONNECTED && st != BMU_STATE_RECONNECTING) continue;
            if (ctx->battery_voltages[i] > max_mv) {
                max_mv = ctx->battery_voltages[i];
            }
        }
        xSemaphoreGive(ctx->state_mutex);
    }
    return max_mv;
}

/* ── Wrapper — backward compat ───────────────────────────────────── */
esp_err_t bmu_protection_check_battery(bmu_protection_ctx_t *ctx, int idx)
{
    return bmu_protection_check_battery_ex(ctx, idx, find_fleet_max_mv(ctx));
}

/* ── Main state machine — called once per battery per loop ─────────── */
esp_err_t bmu_protection_check_battery_ex(bmu_protection_ctx_t *ctx, int idx, float fleet_max_mv)
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
        bmu_i2c_health_record_failure(&ctx->ina_health[idx]);

        // If critical: force battery OFF
        if (bmu_i2c_health_is_critical(&ctx->ina_health[idx])) {
            if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                if (ctx->battery_state[idx] == BMU_STATE_CONNECTED) {
                    ESP_LOGW(TAG, "BAT[%d] health critical (score=%d), forcing OFF",
                             idx + 1, ctx->ina_health[idx].score);
                    switch_battery(ctx, idx, false);
                    ctx->battery_state[idx] = BMU_STATE_DISCONNECTED;
                }
                ctx->battery_voltages[idx] = 0;
                xSemaphoreGive(ctx->state_mutex);
            }
        } else {
            ESP_LOGW(TAG, "BAT[%d] I2C read error (health=%d) — skip",
                     idx + 1, ctx->ina_health[idx].score);
            if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                ctx->battery_voltages[idx] = 0;
                xSemaphoreGive(ctx->state_mutex);
            }
        }
        return ret;
    }

    bmu_i2c_health_record_success(&ctx->ina_health[idx]);

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

    /* Cache voltage and current (mutex-protected) */
    if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        ctx->battery_voltages[idx] = v_mv;
        ctx->battery_currents[idx] = i_a;
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
    /* Protection checks: voltage, current, imbalance */
    else if (!is_voltage_in_range(v_mv) || !is_current_in_range(i_a)) {
        /* Voltage ou courant hors range — deconnexion immediate */
        state = BMU_STATE_DISCONNECTED;
        ctx->imbalance_count[idx] = 0;
        ESP_LOGW(TAG, "BAT[%d] PROT: V=%.0f I=%.3f — hors range", idx + 1, v_mv, i_a);
    }
    else if (!is_imbalance_ok(v_mv, fleet_max_mv)) {
        /* Imbalance — deconnexion seulement apres N cycles consecutifs */
        ctx->imbalance_count[idx]++;
        if (ctx->imbalance_count[idx] >= BMU_IMBALANCE_CONFIRM_CYCLES) {
            state = BMU_STATE_DISCONNECTED;
            ESP_LOGW(TAG, "BAT[%d] IMBALANCE confirme (%d cycles): V=%.0f fleet=%.0f diff=%.0f",
                     idx + 1, ctx->imbalance_count[idx], v_mv, fleet_max_mv, fleet_max_mv - v_mv);
            ctx->imbalance_count[idx] = 0;
        } else {
            ESP_LOGD(TAG, "BAT[%d] imbalance %d/%d: V=%.0f fleet=%.0f diff=%.0f",
                     idx + 1, ctx->imbalance_count[idx], BMU_IMBALANCE_CONFIRM_CYCLES,
                     v_mv, fleet_max_mv, fleet_max_mv - v_mv);
            state = local_prev_state;
        }
    }
    /* All protection checks pass — reset imbalance counter */
    else {
        ctx->imbalance_count[idx] = 0;

        /* Already connected — stay CONNECTED */
        if (local_prev_state == BMU_STATE_CONNECTED ||
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

float bmu_protection_get_current(bmu_protection_ctx_t *ctx, int idx)
{
    float i_a = 0;
    if (idx >= 0 && idx < BMU_MAX_BATTERIES) {
        if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            i_a = ctx->battery_currents[idx];
            xSemaphoreGive(ctx->state_mutex);
        }
    }
    return i_a;
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

/* ── Web-initiated switch — validates through protection ──────────────── */
esp_err_t bmu_protection_web_switch(bmu_protection_ctx_t *ctx, int idx, bool on)
{
    if (idx < 0 || idx >= ctx->nb_ina) return ESP_ERR_INVALID_ARG;

    bmu_battery_state_t state = bmu_protection_get_state(ctx, idx);

    /* Rejeter les etats dangereux */
    if (state == BMU_STATE_LOCKED) {
        ESP_LOGW(TAG, "BAT[%d] web switch rejected — LOCKED", idx + 1);
        return ESP_ERR_NOT_ALLOWED;
    }
    if (on && state == BMU_STATE_ERROR) {
        ESP_LOGW(TAG, "BAT[%d] web switch ON rejected — ERROR state", idx + 1);
        return ESP_ERR_NOT_ALLOWED;
    }

    /* Pour switch ON: valider tension dans la plage securisee */
    if (on) {
        float v_mv = bmu_protection_get_voltage(ctx, idx);
        if (v_mv < BMU_MIN_VOLTAGE_MV || v_mv > BMU_MAX_VOLTAGE_MV) {
            ESP_LOGW(TAG, "BAT[%d] web switch ON rejected — voltage %.0f mV out of range",
                     idx + 1, v_mv);
            return ESP_ERR_INVALID_STATE;
        }
    }

    int tca_idx = idx / 4;
    int channel = idx % 4;
    if (tca_idx >= ctx->nb_tca) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = bmu_tca9535_switch_battery(&ctx->tca_devices[tca_idx], channel, on);
    if (ret == ESP_OK) {
        bmu_tca9535_set_led(&ctx->tca_devices[tca_idx], channel, !on, on);

        /* Mettre a jour l'etat et le compteur de switch — meme logique
         * que la state machine automatique pour coherence */
        if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (on) {
                ctx->nb_switch[idx]++;
                ctx->reconnect_time_ms[idx] = esp_timer_get_time() / 1000;
                ctx->battery_state[idx] = BMU_STATE_RECONNECTING;
            } else {
                ctx->battery_state[idx] = BMU_STATE_DISCONNECTED;
            }
            xSemaphoreGive(ctx->state_mutex);
        }
        ESP_LOGI(TAG, "BAT[%d] web switch %s OK (sw=%d)",
                 idx + 1, on ? "ON" : "OFF", ctx->nb_switch[idx]);
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

// ── RTOS queue / task API (Phase 2) ─────────────────────────────────

esp_err_t bmu_protection_set_queues(bmu_protection_ctx_t *ctx,
                                     const bmu_protection_queues_t *queues) {
    if (!ctx || !queues) return ESP_ERR_INVALID_ARG;
    ctx->queues = *queues;
    ctx->cycle_count = 0;
    return ESP_OK;
}

void bmu_protection_publish_snapshot(bmu_protection_ctx_t *ctx) {
    bmu_snapshot_t snap = {};
    snap.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    snap.cycle_count = ctx->cycle_count++;
    snap.nb_batteries = ctx->nb_ina;
    snap.topology_ok = (ctx->nb_tca * 4 == ctx->nb_ina);

    float max_v = 0, sum_v = 0;
    int count_connected = 0;

    for (int i = 0; i < ctx->nb_ina; i++) {
        snap.battery[i].voltage_mv  = ctx->battery_voltages[i];
        snap.battery[i].state       = ctx->battery_state[i];
        snap.battery[i].nb_switches = (uint8_t)ctx->nb_switch[i];
        snap.battery[i].health_score = ctx->ina_health[i].score;
        snap.battery[i].balancer_active = false;
        snap.battery[i].current_a = ctx->battery_currents[i];

        if (ctx->battery_state[i] == BMU_STATE_CONNECTED ||
            ctx->battery_state[i] == BMU_STATE_RECONNECTING) {
            if (ctx->battery_voltages[i] > max_v)
                max_v = ctx->battery_voltages[i];
            sum_v += ctx->battery_voltages[i];
            count_connected++;
        }
    }

    snap.fleet_max_mv = max_v;
    snap.fleet_mean_mv = count_connected > 0 ? sum_v / count_connected : 0;

    if (ctx->queues.q_balancer)
        xQueueOverwrite(ctx->queues.q_balancer, &snap);
    if (ctx->queues.q_display)
        xQueueOverwrite(ctx->queues.q_display, &snap);
    if (ctx->queues.q_cloud)
        xQueueOverwrite(ctx->queues.q_cloud, &snap);
    if (ctx->queues.q_ble)
        xQueueOverwrite(ctx->queues.q_ble, &snap);
}

void bmu_protection_process_commands(bmu_protection_ctx_t *ctx) {
    if (!ctx->queues.q_cmd) return;
    bmu_cmd_t cmd;
    while (xQueueReceive(ctx->queues.q_cmd, &cmd, 0) == pdTRUE) {
        switch (cmd.type) {
        case CMD_WEB_SWITCH:
            ESP_LOGI(TAG, "CMD web_switch bat=%d on=%d",
                     cmd.payload.web_switch.battery_idx,
                     cmd.payload.web_switch.on);
            bmu_protection_web_switch(ctx,
                                      cmd.payload.web_switch.battery_idx,
                                      cmd.payload.web_switch.on);
            break;
        case CMD_TOPOLOGY_CHANGED:
            ESP_LOGI(TAG, "CMD topology nb_ina=%d nb_tca=%d",
                     cmd.payload.topology.nb_ina,
                     cmd.payload.topology.nb_tca);
            bmu_protection_update_topology(ctx,
                                           cmd.payload.topology.nb_ina,
                                           cmd.payload.topology.nb_tca);
            break;
        case CMD_BALANCE_REQUEST: {
            uint8_t idx = cmd.payload.balance_req.battery_idx;
            bool on = cmd.payload.balance_req.on;
            ESP_LOGD(TAG, "CMD balance bat=%d on=%d", idx, on);
            if (idx >= ctx->nb_ina) break;
            if (xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                if (ctx->battery_state[idx] == BMU_STATE_CONNECTED ||
                    (on && ctx->battery_state[idx] == BMU_STATE_DISCONNECTED)) {
                    switch_battery(ctx, idx, on);
                }
                xSemaphoreGive(ctx->state_mutex);
            }
            break;
        }
        case CMD_CONFIG_UPDATE:
            ESP_LOGI(TAG, "CMD config_update");
            break;
        case CMD_BUS_RECOVERY:
            ESP_LOGW(TAG, "CMD bus_recovery bus=%d",
                     cmd.payload.bus_recovery.bus_id);
            bmu_i2c_bus_recover();
            break;
        }
    }
}

static void protection_task(void *arg) {
    bmu_protection_ctx_t *ctx = (bmu_protection_ctx_t *)arg;
    const TickType_t period = pdMS_TO_TICKS(ctx->task_period_ms);

    ESP_LOGI(TAG, "Protection task started (period=%lums, prio=%d)",
             (unsigned long)ctx->task_period_ms, (int)uxTaskPriorityGet(NULL));

    /* S'enregistrer au WDT pour detecter les spins I2C */
    esp_err_t wdt_ret = esp_task_wdt_add(NULL);
    if (wdt_ret != ESP_OK && wdt_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "WDT register failed: %s", esp_err_to_name(wdt_ret));
    }

    // Warm-up: 5 cycles to stabilize INA237 voltage cache
    for (int w = 0; w < 5; w++) {
        for (int i = 0; i < ctx->nb_ina; i++) {
            float v, c;
            bmu_ina237_read_voltage_current(&ctx->ina_devices[i], &v, &c);
            if (v > 0) ctx->battery_voltages[i] = v;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Reset last_wake AFTER warm-up so vTaskDelayUntil doesn't try to catch up
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        esp_task_wdt_reset();  /* feed watchdog — debut de cycle */

        bmu_protection_process_commands(ctx);

        float fleet_max = bmu_protection_compute_fleet_max(ctx);

        for (int i = 0; i < ctx->nb_ina; i++) {
            /* Skip batteries volontairement OFF par le balancer (évite nb_switch sur duty-cycle) */
            if (bmu_balancer_is_off((uint8_t)i)) {
                vTaskDelay(1);
                continue;
            }
            bmu_protection_check_battery_ex(ctx, i, fleet_max);
            // Yield between batteries to avoid starving lower-prio tasks (display, IDLE)
            vTaskDelay(1);
        }

        esp_task_wdt_reset();  /* feed watchdog — apres boucle batteries */

        bmu_protection_publish_snapshot(ctx);

        vTaskDelayUntil(&last_wake, period);
    }
}

esp_err_t bmu_protection_start_task(bmu_protection_ctx_t *ctx,
                                     uint32_t period_ms,
                                     UBaseType_t priority,
                                     uint32_t stack_size) {
    ctx->task_period_ms = period_ms;
    BaseType_t ret = xTaskCreate(protection_task, "protection",
                                  stack_size, ctx, priority, &ctx->task_handle);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
