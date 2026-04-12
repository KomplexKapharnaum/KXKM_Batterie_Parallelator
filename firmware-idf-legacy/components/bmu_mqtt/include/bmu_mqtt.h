#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise le client MQTT et démarre la connexion au broker.
 *
 * Configuration lue depuis Kconfig (BMU_MQTT_BROKER_URI, etc.).
 * Le client gère la reconnexion automatiquement via esp_mqtt.
 *
 * @return ESP_OK en cas de succès, code d'erreur sinon.
 */
esp_err_t bmu_mqtt_init(void);

/**
 * @brief Vérifie si le client MQTT est connecté au broker.
 */
bool bmu_mqtt_is_connected(void);

/**
 * @brief Publie un message MQTT.
 *
 * @param topic   Topic MQTT (null-terminated).
 * @param payload Données à publier.
 * @param len     Longueur du payload (0 = strlen automatique).
 * @param qos     Niveau QoS (0, 1 ou 2).
 * @param retain  Flag retain MQTT.
 * @return ESP_OK si le message est enfilé, ESP_FAIL sinon.
 */
esp_err_t bmu_mqtt_publish(const char *topic, const char *payload, int len, int qos, bool retain);

#ifdef __cplusplus
}
#endif
