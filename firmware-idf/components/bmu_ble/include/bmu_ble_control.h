// firmware-idf-v2/components/bmu_ble/include/bmu_ble_control.h
//
// Phase 19 : BLE Control Service — commande securisee via GATT write.

#pragma once
#include "host/ble_hs.h"

int bmu_ble_control_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg);

void bmu_ble_control_on_disconnect(uint16_t conn_handle);
