// firmware-idf-v2/components/bmu_ble/include/bmu_ble_hmac.h
//
// Phase 19 : HKDF key derivation + HMAC verify + nonce persistence.

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "host/ble_hs.h"

void bmu_ble_hmac_derive_for_conn(uint16_t conn_handle);

bool bmu_ble_hmac_verify(uint16_t conn_handle, const void *msg, size_t msg_len,
                          const uint8_t *tag, size_t tag_len);

void bmu_ble_hmac_persist_nonce(const ble_addr_t *peer, uint64_t nonce);

uint64_t bmu_ble_hmac_load_nonce(const ble_addr_t *peer);

void bmu_ble_hmac_invalidate_conn(uint16_t conn_handle);
