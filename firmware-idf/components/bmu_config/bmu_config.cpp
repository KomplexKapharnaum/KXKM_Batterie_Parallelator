#include "bmu_config.h"
#include "esp_log.h"

static const char *TAG = "CFG";

void bmu_config_log(void)
{
    ESP_LOGI(TAG, "Config: V_min=%dmV V_max=%dmV I_max=%dmA",
             BMU_MIN_VOLTAGE_MV, BMU_MAX_VOLTAGE_MV, BMU_MAX_CURRENT_MA);
    ESP_LOGI(TAG, "Config: V_diff=%dmV delay=%dms nb_switch_max=%d",
             BMU_VOLTAGE_DIFF_MV, BMU_RECONNECT_DELAY_MS, BMU_NB_SWITCH_MAX);
    ESP_LOGI(TAG, "Config: overcurrent_factor=%d/1000 loop=%dms",
             BMU_OVERCURRENT_FACTOR, BMU_LOOP_PERIOD_MS);
}
