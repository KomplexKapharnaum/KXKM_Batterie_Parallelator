// firmware-idf-v2/components/bmu_ble/include/bmu_ble_wifi_prov.h
//
// Phase 20 : Wi-Fi provisioning via BLE command 0x08 (v2 extended frame).

#pragma once
#include "host/ble_hs.h"

int bmu_ble_wifi_prov_handle(uint16_t conn_handle, struct ble_gatt_access_ctxt *ctxt);
