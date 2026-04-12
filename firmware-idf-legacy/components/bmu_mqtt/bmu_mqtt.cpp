/**
 * @file bmu_mqtt.cpp
 * @brief Client MQTT ESP-IDF pour BMU — remplace PubSubClient Arduino.
 *
 * Utilise esp_mqtt_client (event-driven, reconnexion automatique).
 * Pas de loop() manuelle nécessaire contrairement à PubSubClient.
 */

#include "bmu_mqtt.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include <cstring>

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_client = nullptr;
static volatile bool s_connected = false;

/* -------------------------------------------------------------------------- */
/*  Gestionnaire d'événements MQTT                                            */
/* -------------------------------------------------------------------------- */

static void mqtt_event_handler(void * /*handler_args*/,
                               esp_event_base_t /*base*/,
                               int32_t event_id,
                               void *event_data)
{
    auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "Connecté au broker MQTT");
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Déconnecté du broker MQTT — reconnexion automatique");
        break;

    case MQTT_EVENT_ERROR:
        if (event->error_handle) {
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Erreur transport TCP — code esp-tls: 0x%x, errno: %d",
                         (int)event->error_handle->esp_tls_last_esp_err,
                         event->error_handle->esp_transport_sock_errno);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(TAG, "Connexion refusée par le broker — code: 0x%x",
                         (int)event->error_handle->connect_return_code);
            }
        }
        break;

    default:
        break;
    }
}

/* -------------------------------------------------------------------------- */
/*  API publique                                                              */
/* -------------------------------------------------------------------------- */

esp_err_t bmu_mqtt_init(void)
{
    if (s_client != nullptr) {
        ESP_LOGW(TAG, "Client MQTT déjà initialisé");
        return ESP_OK;
    }

    /* Avertissement sécurité si connexion non chiffrée (audit HIGH-4) */
    const char *uri = CONFIG_BMU_MQTT_BROKER_URI;
    if (strncmp(uri, "mqtts://", 8) != 0) {
        ESP_LOGW(TAG, "⚠ Connexion MQTT non chiffrée (%s) — "
                 "utiliser mqtts:// en production (cf. audit HIGH-4)", uri);
    }

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri         = CONFIG_BMU_MQTT_BROKER_URI;
    cfg.credentials.client_id      = CONFIG_BMU_MQTT_CLIENT_ID;
    cfg.network.reconnect_timeout_ms = CONFIG_BMU_MQTT_RECONNECT_MS;

    /* Credentials optionnels */
    if (strlen(CONFIG_BMU_MQTT_USERNAME) > 0) {
        cfg.credentials.username = CONFIG_BMU_MQTT_USERNAME;
    }
    if (strlen(CONFIG_BMU_MQTT_PASSWORD) > 0) {
        cfg.credentials.authentication.password = CONFIG_BMU_MQTT_PASSWORD;
    }

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == nullptr) {
        ESP_LOGE(TAG, "Échec esp_mqtt_client_init");
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(
        s_client, MQTT_EVENT_ANY, mqtt_event_handler, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Échec enregistrement event handler: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = nullptr;
        return err;
    }

    err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Échec esp_mqtt_client_start: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = nullptr;
        return err;
    }

    ESP_LOGI(TAG, "Client MQTT démarré — broker: %s, client_id: %s",
             CONFIG_BMU_MQTT_BROKER_URI, CONFIG_BMU_MQTT_CLIENT_ID);
    return ESP_OK;
}

bool bmu_mqtt_is_connected(void)
{
    return s_connected;
}

esp_err_t bmu_mqtt_publish(const char *topic, const char *payload, int len, int qos, bool retain)
{
    if (s_client == nullptr) {
        ESP_LOGE(TAG, "Client MQTT non initialisé — appeler bmu_mqtt_init() d'abord");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_connected) {
        ESP_LOGW(TAG, "Publish ignoré (non connecté) — topic: %s", topic);
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, len, qos, retain ? 1 : 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Échec publication — topic: %s", topic);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Publié msg_id=%d topic=%s len=%d qos=%d retain=%d",
             msg_id, topic, len, qos, (int)retain);
    return ESP_OK;
}
