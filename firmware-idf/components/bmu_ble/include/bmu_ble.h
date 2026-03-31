#pragma once

/**
 * @file bmu_ble.h
 * @brief NimBLE BLE — 3 services GATT (Battery, System, Control) pour BMU.
 *
 * Phase 9 : monitoring temps reel + controle securise via BLE.
 * Pairing + bonding requis pour les ecritures de controle.
 */

#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise NimBLE, enregistre les 3 services GATT, demarre l'advertising.
 *        Doit etre appele apres bmu_protection_init et bmu_battery_manager_init.
 *
 * @param prot   Contexte protection (pour switch/reset/state)
 * @param mgr    Battery manager (pour Ah, courants)
 * @param nb_ina Nombre de batteries detectees
 * @return ESP_OK ou code erreur
 */
esp_err_t bmu_ble_init(bmu_protection_ctx_t *prot,
                        bmu_battery_manager_t *mgr,
                        uint8_t nb_ina);

/** @brief Retourne true si au moins un client BLE est connecte. */
bool bmu_ble_is_connected(void);

/** @brief Nombre de clients BLE actuellement connectes. */
int bmu_ble_connected_count(void);

#ifdef __cplusplus
}
#endif
