#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bmu_influx_init(void);

// Write a single line protocol entry. Buffered internally, flushed when buffer full or on explicit flush.
esp_err_t bmu_influx_write(const char *measurement, const char *tags, const char *fields, int64_t timestamp_ns);

// Convenience: write battery telemetry
esp_err_t bmu_influx_write_battery(int battery_id, float voltage_mv, float current_a,
                                    float ah_discharge, float ah_charge, const char *state);

// Flush buffered writes to InfluxDB
esp_err_t bmu_influx_flush(void);

#ifdef __cplusplus
}
#endif
