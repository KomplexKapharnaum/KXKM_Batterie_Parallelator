#pragma once
#include "esp_err.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bmu_ble_victron_init(bmu_protection_ctx_t *prot,
                               bmu_battery_manager_t *mgr,
                               uint8_t nb_ina);

#ifdef __cplusplus
}
#endif
