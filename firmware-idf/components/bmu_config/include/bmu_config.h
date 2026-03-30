#pragma once

#include "sdkconfig.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMU_MIN_VOLTAGE_MV      CONFIG_BMU_MIN_VOLTAGE_MV
#define BMU_MAX_VOLTAGE_MV      CONFIG_BMU_MAX_VOLTAGE_MV
#define BMU_MAX_CURRENT_MA      CONFIG_BMU_MAX_CURRENT_MA
#define BMU_VOLTAGE_DIFF_MV     CONFIG_BMU_VOLTAGE_DIFF_MV
#define BMU_RECONNECT_DELAY_MS  CONFIG_BMU_RECONNECT_DELAY_MS
#define BMU_NB_SWITCH_MAX       CONFIG_BMU_NB_SWITCH_MAX
#define BMU_OVERCURRENT_FACTOR  CONFIG_BMU_OVERCURRENT_FACTOR
#define BMU_LOOP_PERIOD_MS      CONFIG_BMU_LOOP_PERIOD_MS

#define BMU_MAX_BATTERIES       16
#define BMU_MAX_TCA             8

void bmu_config_log(void);

#ifdef __cplusplus
}
#endif
