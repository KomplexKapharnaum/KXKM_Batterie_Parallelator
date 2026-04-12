// components/bmu_types/include/bmu_ops.h
#pragma once

#include "bmu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── I2C operations (real or mock) ──
typedef struct {
    esp_err_t (*read_register)(bmu_device_t *dev, uint8_t reg, uint8_t *buf, size_t len);
    esp_err_t (*write_register)(bmu_device_t *dev, uint8_t reg, const uint8_t *buf, size_t len);
    esp_err_t (*probe)(bmu_i2c_bus_t *bus, uint8_t addr);
} bmu_i2c_ops_t;

// ── Sensor operations (INA237 real or mock) ──
typedef struct {
    esp_err_t (*read_voltage_current)(bmu_device_t *dev, float *voltage_mv, float *current_a);
    void      (*record_health)(bmu_device_t *dev, bool success);
} bmu_sensor_ops_t;

// ── Switch operations (TCA9535 real or mock) ──
typedef struct {
    esp_err_t (*switch_battery)(bmu_device_t *dev, uint8_t channel, bool on);
    esp_err_t (*set_led)(bmu_device_t *dev, uint8_t channel, bool red, bool green);
    esp_err_t (*all_off)(bmu_device_t *dev);
} bmu_switch_ops_t;

#ifdef __cplusplus
}
#endif
