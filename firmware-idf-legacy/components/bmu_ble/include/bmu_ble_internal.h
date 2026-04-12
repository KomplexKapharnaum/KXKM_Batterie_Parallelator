#pragma once

/**
 * @file bmu_ble_internal.h
 * @brief Declarations internes partagees entre les fichiers .cpp du composant bmu_ble.
 *        Ne pas inclure depuis l'exterieur du composant.
 */

#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "host/ble_gatt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Accesseurs contexte (definis dans bmu_ble.cpp) ──────────────── */
bmu_protection_ctx_t    *bmu_ble_get_prot(void);
bmu_battery_manager_t   *bmu_ble_get_mgr(void);
uint8_t                  bmu_ble_get_nb_ina(void);

/* ── UUID base personnalise KXKM-BMU ─────────────────────────────── */
/* 4b584b4d-xxxx-4b4d-424d-55424c450000
 * Octets en little-endian pour NimBLE ble_uuid128_t                  */
#define BMU_BLE_UUID128_BASE \
    0x00, 0x00, 0x45, 0x4c, 0x42, 0x55, 0x4d, 0x42, \
    0x4d, 0x4b, 0x00, 0x00, 0x4d, 0x4b, 0x58, 0x4b

/* Macro pour construire un UUID 128-bit avec un suffix 16-bit */
#define BMU_BLE_UUID128_DECLARE(suffix_lo, suffix_hi) \
    BLE_UUID128_INIT( \
        0x00, 0x00, 0x45, 0x4c, 0x42, 0x55, 0x4d, 0x42, \
        0x4d, 0x4b, suffix_lo, suffix_hi, 0x4d, 0x4b, 0x58, 0x4b \
    )

/* ── Services GATT (definis dans chaque *_svc.cpp) ───────────────── */
const struct ble_gatt_svc_def *bmu_ble_battery_svc_defs(void);
const struct ble_gatt_svc_def *bmu_ble_system_svc_defs(void);
const struct ble_gatt_svc_def *bmu_ble_control_svc_defs(void);

/* ── Timers de notification (demarrage/arret) ────────────────────── */
void bmu_ble_battery_notify_start(void);
void bmu_ble_battery_notify_stop(void);
void bmu_ble_system_notify_start(void);
void bmu_ble_system_notify_stop(void);
void bmu_ble_wifi_notify_start(void);
void bmu_ble_wifi_notify_stop(void);

#ifdef __cplusplus
}
#endif
