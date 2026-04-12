// firmware-idf-v2/components/bmu_mqtt/include/bmu_mqtt.h
//
// Phase 16 -- MQTT client. Publie telemetrie JSON sur bmu/<id>/telemetry,
// replay SD NOSYNC sur bmu/<id>/replay au connect.

#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct BmuCore;

esp_err_t bmu_mqtt_init(void);
esp_err_t bmu_mqtt_start(void);
bool bmu_mqtt_is_connected(void);

// Build JSON telemetry from cached snapshot and publish to bmu/<id>/telemetry
esp_err_t bmu_mqtt_publish_telemetry(struct BmuCore *core);

// Spawn oneshot replay task on MQTT_CONNECTED
void bmu_mqtt_replay_start(void);

#ifdef __cplusplus
}
#endif
