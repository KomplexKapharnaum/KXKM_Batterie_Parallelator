// firmware-idf-v2/components/bmu_ble_victron/include/bmu_ble_victron.h
//
// Phase 21 : emulation BLE Victron SmartShunt — GATT + Instant Readout.

#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

struct BmuCore;

// Register Victron GATT service 0x6597. Must be called before NimBLE starts.
int bmu_ble_victron_gatt_init(void);

// Update Instant Readout advertising data. Called at 1 Hz from task_ble.
void bmu_ble_victron_adv_tick(struct BmuCore *core);

#ifdef __cplusplus
}
#endif
