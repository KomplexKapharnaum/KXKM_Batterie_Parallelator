#include "sdkconfig.h"

#if CONFIG_BMU_VRM_ENABLED

#include "bmu_vrm.h"
#include "bmu_vedirect.h"
#include "bmu_config.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"

#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstring>

static const char *TAG = "VRM";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static bmu_protection_ctx_t *s_prot = NULL;
static bmu_battery_manager_t *s_mgr = NULL;
static uint8_t s_nb_ina = 0;

/* ── Helpers ── */

static void vrm_publish(const char *path, const char *value_json)
{
    if (!s_connected || s_client == NULL) return;
    char topic[128];
    snprintf(topic, sizeof(topic), "N/%s/%s",
             CONFIG_BMU_VRM_PORTAL_ID, path);
    esp_mqtt_client_publish(s_client, topic, value_json, 0, 0, 0);
}

static void vrm_pub_float(const char *path, float val)
{
    char json[32];
    snprintf(json, sizeof(json), "{\"value\":%.2f}", val);
    vrm_publish(path, json);
}

static void vrm_pub_int(const char *path, int val)
{
    char json[32];
    snprintf(json, sizeof(json), "{\"value\":%d}", val);
    vrm_publish(path, json);
}

static void vrm_pub_str(const char *path, const char *val)
{
    char json[64];
    snprintf(json, sizeof(json), "{\"value\":\"%s\"}", val);
    vrm_publish(path, json);
}

/* ── SOC estimation ── */

static float estimate_soc(float avg_v)
{
    float v_min = (float)CONFIG_BMU_VRM_SOC_V_MIN / 1000.0f;
    float v_max = (float)CONFIG_BMU_VRM_SOC_V_MAX / 1000.0f;
    float soc = (avg_v - v_min) / (v_max - v_min) * 100.0f;
    if (soc < 0.0f) soc = 0.0f;
    if (soc > 100.0f) soc = 100.0f;
    return soc;
}

/* ── Publish solar charger ── */

static void publish_solar(void)
{
    if (!bmu_vedirect_is_connected()) return;
    const bmu_vedirect_data_t *d = bmu_vedirect_get_data();
    if (!d || !d->valid) return;

    vrm_pub_float("solarcharger/0/Pv/V", d->panel_voltage_v);
    vrm_pub_int("solarcharger/0/Pv/P", (int)d->panel_power_w);
    vrm_pub_float("solarcharger/0/Dc/0/Voltage", d->battery_voltage_v);
    vrm_pub_float("solarcharger/0/Dc/0/Current", d->battery_current_a);
    vrm_pub_int("solarcharger/0/State", (int)d->charge_state);
    vrm_pub_float("solarcharger/0/Yield/User", (float)d->yield_today_wh / 1000.0f);
    vrm_pub_int("solarcharger/0/ErrorCode", (int)d->error_code);
    vrm_pub_str("solarcharger/0/ProductId", d->product_id);
    vrm_pub_str("solarcharger/0/Serial", d->serial);
}

/* ── Publish battery monitor ── */

static void publish_battery(void)
{
    if (s_nb_ina == 0 || s_mgr == NULL) return;

    /* Utilise l'API bmu_battery_manager — pas d'acces direct aux ina_devices */
    float avg_mv = bmu_battery_manager_get_avg_voltage_mv(s_mgr);
    float avg_v  = avg_mv / 1000.0f;
    float total_i = bmu_battery_manager_get_total_current_a(s_mgr);
    float soc = estimate_soc(avg_v);

    /* Cumul Ah decharge sur toutes les batteries */
    float sum_ah_d = 0.0f;
    int nb = s_nb_ina > BMU_MAX_BATTERIES ? BMU_MAX_BATTERIES : (int)s_nb_ina;
    for (int i = 0; i < nb; i++) {
        sum_ah_d += bmu_battery_manager_get_ah_discharge(s_mgr, i);
    }

    vrm_pub_float("battery/0/Dc/0/Voltage", avg_v);
    vrm_pub_float("battery/0/Dc/0/Current", total_i);
    vrm_pub_float("battery/0/Soc", soc);
    vrm_pub_float("battery/0/ConsumedAmphours", sum_ah_d);
}

/* ── Keepalive ── */

static void publish_keepalive(void)
{
    if (!s_connected || s_client == NULL) return;
    char topic[64];
    snprintf(topic, sizeof(topic), "R/%s/keepalive", CONFIG_BMU_VRM_PORTAL_ID);
    esp_mqtt_client_publish(s_client, topic, "", 0, 0, 0);
}

/* ── MQTT event handler ── */

static void vrm_event_handler(void *arg, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    (void)arg; (void)base;
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)event_data;
    switch (ev->event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "VRM MQTT connecte");
            vrm_pub_str("system/0/Serial", CONFIG_BMU_VRM_PORTAL_ID);
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "VRM MQTT deconnecte");
            break;
        default:
            break;
    }
}

/* ── Task ── */

static void vrm_task(void *pv)
{
    (void)pv;
    const TickType_t period = pdMS_TO_TICKS(CONFIG_BMU_VRM_PUBLISH_INTERVAL_S * 1000);
    ESP_LOGI(TAG, "VRM task demarree — intervalle %ds, portal %s",
             CONFIG_BMU_VRM_PUBLISH_INTERVAL_S, CONFIG_BMU_VRM_PORTAL_ID);

    for (;;) {
        vTaskDelay(period);
        if (!s_connected) continue;
        publish_keepalive();
        publish_solar();
        publish_battery();
    }
}

/* ── Public API ── */

esp_err_t bmu_vrm_init(bmu_protection_ctx_t *prot,
                       bmu_battery_manager_t *mgr,
                       uint8_t nb_ina)
{
    s_prot   = prot;
    s_mgr    = mgr;
    s_nb_ina = nb_ina;

    const char *broker = CONFIG_BMU_VRM_USE_TLS
        ? "mqtts://mqtt.victronenergy.com:8883"
        : "mqtt://mqtt.victronenergy.com:1883";

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri             = broker;
    cfg.credentials.client_id          = CONFIG_BMU_VRM_PORTAL_ID;
    cfg.session.keepalive              = CONFIG_BMU_VRM_PUBLISH_INTERVAL_S;
    cfg.network.reconnect_timeout_ms   = 10000;
    cfg.buffer.size                    = 1024;

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "MQTT client init echoue");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, vrm_event_handler, NULL);
    esp_err_t ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT start echoue : %s", esp_err_to_name(ret));
        return ret;
    }

    xTaskCreate(vrm_task, "vrm", 4096, NULL, 2, NULL);
    ESP_LOGI(TAG, "VRM init OK — %s", broker);
    return ESP_OK;
}

bool bmu_vrm_is_connected(void) { return s_connected; }

#else  /* CONFIG_BMU_VRM_ENABLED */

#include "bmu_vrm.h"
esp_err_t bmu_vrm_init(bmu_protection_ctx_t *p, bmu_battery_manager_t *m, uint8_t n)
{ (void)p; (void)m; (void)n; return ESP_OK; }
bool bmu_vrm_is_connected(void) { return false; }

#endif /* CONFIG_BMU_VRM_ENABLED */
