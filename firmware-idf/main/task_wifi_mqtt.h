// firmware-idf-v2/main/task_wifi_mqtt.h
//
// Phase 16 -- Wi-Fi STA + MQTT telemetry task.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct BmuCore;
void task_wifi_mqtt_start(struct BmuCore *core);

#ifdef __cplusplus
}
#endif
