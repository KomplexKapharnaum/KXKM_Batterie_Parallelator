// firmware-idf-v2/components/bmu_ble/include/bmu_ble_audit.h
//
// Phase 19 : Append-only audit log with HMAC chain.

#pragma once
#include <stdint.h>
#include "host/ble_hs.h"

void bmu_ble_audit_init(void);

void bmu_ble_audit_log_pass(const ble_addr_t *peer, uint8_t cmd_id,
                              uint8_t bat_idx, uint32_t param, int result);

void bmu_ble_audit_log_reject(const ble_addr_t *peer, uint8_t cmd_id,
                                const char *reason);
