// firmware-idf-v2/components/bmu_ble_victron/include/fleet_agg.h
//
// Phase 21 : aggregation flotte pour emulation Victron SmartShunt.

#pragma once
#include <stdint.h>

struct BmuSnapshotC;

typedef struct {
    uint16_t voltage_mv;      // mean of online batteries
    int32_t  current_ma;      // sum of online batteries
    uint8_t  soc_pct;         // average soh_pct of online batteries (as proxy for SoC)
    int32_t  power_cw;        // centi-watts: voltage_mv * current_ma / 100000
    int32_t  consumed_ah_mah; // sum of ah_remaining of online batteries
    int8_t   temp_max_c;      // max temp of online batteries (temp_c10 / 10)
    uint8_t  n_online;
} bmu_fleet_agg_t;

void fleet_agg_compute(const struct BmuSnapshotC *snap, bmu_fleet_agg_t *out);
