#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float    battery_voltage_v;    // V label (mV → V)
    float    battery_current_a;    // I label (mA → A)
    float    panel_voltage_v;      // VPV (mV → V)
    uint16_t panel_power_w;        // PPV
    uint8_t  charge_state;         // CS (0=Off, 3=Bulk, 4=Absorption, 5=Float)
    uint8_t  mppt_state;           // MPPT (0=Off, 1=Limited, 2=Active)
    uint8_t  error_code;           // ERR
    uint32_t yield_total_wh;       // H19 (0.01kWh → Wh)
    uint32_t yield_today_wh;       // H20 (0.01kWh → Wh)
    uint16_t max_power_today_w;    // H21
    char     product_id[8];        // PID
    char     serial[20];           // SER#
    char     firmware[8];          // FW
    bool     load_on;              // LOAD
    bool     valid;                // Checksum verified
    int64_t  last_update_ms;       // Timestamp of last valid frame
} bmu_vedirect_data_t;

// Charge state names
const char *bmu_vedirect_cs_name(uint8_t cs);

esp_err_t bmu_vedirect_init(void);
const bmu_vedirect_data_t *bmu_vedirect_get_data(void);
bool bmu_vedirect_is_connected(void);

#ifdef __cplusplus
}
#endif
