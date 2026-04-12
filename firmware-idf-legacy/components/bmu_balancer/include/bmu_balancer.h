#pragma once

#include "bmu_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    QueueHandle_t q_snapshot;  // input: snapshot from protection
    QueueHandle_t q_cmd;       // output: BALANCE_REQUEST to protection
} bmu_balancer_config_t;

esp_err_t bmu_balancer_init(const bmu_balancer_config_t *cfg);
esp_err_t bmu_balancer_start_task(UBaseType_t priority, uint32_t stack_size);
bool      bmu_balancer_is_off(uint8_t idx);
int       bmu_balancer_get_duty_pct(uint8_t idx);

#ifdef __cplusplus
}
#endif
