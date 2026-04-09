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

// Extended battery telemetry with optional health metrics
typedef struct {
    int   battery_id;       /* 0-indexed */
    float voltage_mv;
    float current_a;
    float ah_discharge;
    float ah_charge;
    const char *state;
    /* Optional health metrics — set to NAN if unavailable */
    float r_ohmic_mohm;     /* NAN si non mesure */
    float r_total_mohm;     /* NAN si non mesure */
    float soh_percent;      /* NAN si non disponible */
    int   balancer_duty;    /* -1 si non actif, sinon 0-100 */
} bmu_influx_battery_full_t;

esp_err_t bmu_influx_write_battery_full(const bmu_influx_battery_full_t *data);

// Convenience: write AHT30 climate telemetry
esp_err_t bmu_influx_write_climate(float temperature_c, float humidity_pct);

// Write a raw pre-formatted line protocol string into the memory buffer.
// Used by replay to re-inject stored lines.
esp_err_t bmu_influx_write_raw(const char *line, size_t len);

// Flush buffered writes to InfluxDB
esp_err_t bmu_influx_flush(void);

#ifdef __cplusplus
}
#endif
