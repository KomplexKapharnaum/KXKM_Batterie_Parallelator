/**
 * bmu_balancer — Soft-balancing par duty-cycling.
 * Consomme des snapshots de la queue protection et poste des
 * CMD_BALANCE_REQUEST quand une batterie doit etre connectee/deconnectee.
 */

#include "bmu_balancer.h"
#include "bmu_types.h"
#include "esp_log.h"

static const char *TAG = "BALANCER";

#if !CONFIG_BMU_BALANCER_ENABLED

esp_err_t bmu_balancer_init(const bmu_balancer_config_t *) { return ESP_OK; }
esp_err_t bmu_balancer_start_task(UBaseType_t, uint32_t) { return ESP_OK; }
bool bmu_balancer_is_off(uint8_t) { return false; }
int bmu_balancer_get_duty_pct(uint8_t) { return 100; }

#else

static bmu_balancer_config_t s_cfg;

static struct {
    int  on_counter;
    int  off_counter;
    bool balancing;
    float v_before_mv;
    float i_before_a;
} s_bat[BMU_MAX_BATTERIES];

esp_err_t bmu_balancer_init(const bmu_balancer_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    s_cfg = *cfg;
    for (int i = 0; i < BMU_MAX_BATTERIES; i++) {
        s_bat[i].on_counter  = CONFIG_BMU_BALANCE_DUTY_ON;
        s_bat[i].off_counter = 0;
        s_bat[i].balancing   = false;
    }
    ESP_LOGI(TAG, "Init OK — seuil=%d mV, duty ON=%d OFF=%d, min_conn=%d",
             CONFIG_BMU_BALANCE_HIGH_MV, CONFIG_BMU_BALANCE_DUTY_ON,
             CONFIG_BMU_BALANCE_DUTY_OFF, CONFIG_BMU_BALANCE_MIN_CONNECTED);
    return ESP_OK;
}

static void balancer_task(void *arg)
{
    ESP_LOGI(TAG, "Balancer task started");

    while (true) {
        bmu_snapshot_t snap;
        if (xQueueReceive(s_cfg.q_snapshot, &snap, portMAX_DELAY) != pdTRUE)
            continue;

        int nb = snap.nb_batteries;
        if (nb < CONFIG_BMU_BALANCE_MIN_CONNECTED)
            continue;

        // Compute v_mean of connected batteries (excluding OFF phase)
        float sum_v = 0;
        int n_conn = 0;
        for (int i = 0; i < nb; i++) {
            if (snap.battery[i].state != BMU_STATE_CONNECTED) continue;
            if (s_bat[i].off_counter > 0) continue;
            if (snap.battery[i].voltage_mv > 1000.0f) {
                sum_v += snap.battery[i].voltage_mv;
                n_conn++;
            }
        }

        if (n_conn < CONFIG_BMU_BALANCE_MIN_CONNECTED) continue;
        float v_moy = sum_v / (float)n_conn;

        for (int i = 0; i < nb; i++) {
            bmu_battery_state_t state = snap.battery[i].state;

            if (state != BMU_STATE_CONNECTED && s_bat[i].off_counter == 0) {
                s_bat[i].balancing = false;
                continue;
            }

            // Battery in OFF phase (duty-cycled)
            if (s_bat[i].off_counter > 0) {
                s_bat[i].off_counter--;
                if (s_bat[i].off_counter == 0) {
                    s_bat[i].balancing = false;
                    s_bat[i].on_counter = CONFIG_BMU_BALANCE_DUTY_ON;
                    // Request reconnect via cmd queue
                    bmu_cmd_t cmd = {};
                    cmd.type = CMD_BALANCE_REQUEST;
                    cmd.payload.balance_req.battery_idx = (uint8_t)i;
                    cmd.payload.balance_req.on = true;
                    xQueueSend(s_cfg.q_cmd, &cmd, pdMS_TO_TICKS(50));
                    ESP_LOGD(TAG, "BAT[%d] balance ON (fin duty OFF)", i + 1);
                }
                continue;
            }

            // Battery in ON phase — check if duty-cycle needed
            float v = snap.battery[i].voltage_mv;
            float delta = v - v_moy;

            if (delta > (float)CONFIG_BMU_BALANCE_HIGH_MV) {
                s_bat[i].on_counter--;
                s_bat[i].balancing = true;

                if (s_bat[i].on_counter <= 0) {
                    s_bat[i].v_before_mv = v;
                    s_bat[i].i_before_a = snap.battery[i].current_a;
                    s_bat[i].off_counter = CONFIG_BMU_BALANCE_DUTY_OFF;

                    // Request disconnect via cmd queue
                    bmu_cmd_t cmd = {};
                    cmd.type = CMD_BALANCE_REQUEST;
                    cmd.payload.balance_req.battery_idx = (uint8_t)i;
                    cmd.payload.balance_req.on = false;
                    xQueueSend(s_cfg.q_cmd, &cmd, pdMS_TO_TICKS(50));

                    ESP_LOGI(TAG, "BAT[%d] balance OFF (V=%.0f > moy=%.0f +%d)",
                             i + 1, v, v_moy, CONFIG_BMU_BALANCE_HIGH_MV);
                }
            } else {
                s_bat[i].on_counter = CONFIG_BMU_BALANCE_DUTY_ON;
                s_bat[i].balancing = false;
            }
        }
    }
}

esp_err_t bmu_balancer_start_task(UBaseType_t priority, uint32_t stack_size)
{
    BaseType_t ret = xTaskCreate(balancer_task, "balancer",
                                  stack_size, NULL, priority, NULL);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

bool bmu_balancer_is_off(uint8_t idx)
{
    if (idx >= BMU_MAX_BATTERIES) return false;
    return s_bat[idx].off_counter > 0;
}

int bmu_balancer_get_duty_pct(uint8_t idx)
{
    if (idx >= BMU_MAX_BATTERIES || !s_bat[idx].balancing) return 100;
    int total = CONFIG_BMU_BALANCE_DUTY_ON + CONFIG_BMU_BALANCE_DUTY_OFF;
    return (CONFIG_BMU_BALANCE_DUTY_ON * 100) / total;
}

#endif /* CONFIG_BMU_BALANCER_ENABLED */
