#pragma once

/**
 * @file bmu_climate.h
 * @brief Driver AHT30 — capteur temperature/humidite pour BMU.
 *
 * Adresse I2C : 0x38 sur le bus BMU (I2C_NUM_1, GPIO40/41).
 * Lecture automatique toutes les 5 secondes via esp_timer.
 */

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise le capteur AHT30 sur le bus I2C BMU.
 *        Lance un timer periodique de lecture toutes les 5s.
 * @param bus Handle du bus I2C maitre (I2C_NUM_1).
 * @return ESP_OK si le capteur repond, sinon erreur.
 */
esp_err_t bmu_climate_init(i2c_master_bus_handle_t bus);

/**
 * @brief Lecture immediate temperature + humidite (bloquant ~100ms).
 * @param[out] temperature_c  Temperature en degres Celsius.
 * @param[out] humidity_pct   Humidite relative en pourcentage.
 * @return ESP_OK si lecture reussie.
 */
esp_err_t bmu_climate_read(float *temperature_c, float *humidity_pct);

/**
 * @brief Retourne la derniere temperature lue (cache timer).
 * @return Temperature en degres Celsius, ou NAN si indisponible.
 */
float bmu_climate_get_temperature(void);

/**
 * @brief Retourne la derniere humidite lue (cache timer).
 * @return Humidite relative en %, ou NAN si indisponible.
 */
float bmu_climate_get_humidity(void);

/**
 * @brief Indique si le capteur AHT30 est present et fonctionnel.
 */
bool bmu_climate_is_available(void);

#ifdef __cplusplus
}
#endif
