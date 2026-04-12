#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct BmuCore;

/**
 * Initialise le stack NimBLE, enregistre les services GATT et demarre
 * l'advertising BLE. Doit etre appele une seule fois au boot.
 */
esp_err_t bmu_ble_init(void);

/**
 * Envoie les notifications BLE pour toutes les batteries et le systeme
 * a tous les peers connectes. Appele par task_ble a 2 Hz.
 */
void bmu_ble_notify_all(struct BmuCore *core);

#ifdef __cplusplus
}
#endif
