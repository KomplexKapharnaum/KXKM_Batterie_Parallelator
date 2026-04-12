#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Retourne la definition du service GATT SmartShunt.
 *  A enregistrer dans le tableau GATT de bmu_ble.cpp. */
const struct ble_gatt_svc_def *bmu_ble_victron_gatt_svc_defs(void);

/** Demarre le timer de notification (1s). Appeler quand un client BLE se connecte. */
void bmu_ble_victron_gatt_notify_start(void);

/** Arrete le timer de notification. Appeler quand le dernier client BLE se deconnecte. */
void bmu_ble_victron_gatt_notify_stop(void);

#ifdef __cplusplus
}
#endif
