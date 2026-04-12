// firmware-idf-v2/components/bmu_mqtt/src/bmu_mqtt.c
//
// Phase 16 -- MQTT client telemetrie JSON + replay trigger.

#include "bmu_mqtt.h"
#include "bmu_core.h"
#include "bmu_sd_log.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt";

#define BROKER_URI "mqtt://kxkm-ai:1883"

// Not static -- shared with bmu_mqtt_replay.c (same component)
esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
char s_device_id[16] = {0};
static char s_topic_telemetry[64] = {0};

static void format_device_id(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_topic_telemetry, sizeof(s_topic_telemetry),
             "bmu/%s/telemetry", s_device_id);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    (void)arg;
    (void)base;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to %s", BROKER_URI);
        s_connected = true;
        bmu_mqtt_replay_start();
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        s_connected = false;
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error type=%d", event->error_handle->error_type);
        break;
    default:
        break;
    }
}

esp_err_t bmu_mqtt_init(void)
{
    format_device_id();
    ESP_LOGI(TAG, "device_id=%s topic=%s", s_device_id, s_topic_telemetry);

    const esp_mqtt_client_config_t cfg = {
        .broker = {
            .address = {
                .uri = BROKER_URI,
            },
        },
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    return ESP_OK;
}

esp_err_t bmu_mqtt_start(void)
{
    if (s_client == NULL) return ESP_ERR_INVALID_STATE;
    return esp_mqtt_client_start(s_client);
}

bool bmu_mqtt_is_connected(void)
{
    return s_connected;
}

esp_err_t bmu_mqtt_publish_telemetry(struct BmuCore *core)
{
    if (!s_connected || s_client == NULL || core == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    struct BmuSnapshotC snap;
    memset(&snap, 0, sizeof(snap));
    if (bmu_core_get_cached_snapshot(core, &snap) != 0) {
        return ESP_FAIL;
    }

    // Timestamp ms (uptime-based, no NTP dependency)
    int64_t ts_ms = esp_timer_get_time() / 1000;

    // Build JSON. Max ~100 bytes per battery + overhead.
    // 16 batteries * 100 + 200 header/system ~ 1800 bytes max.
    char buf[2048];
    int pos = 0;

    pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "{\"t\":%" PRId64 ",\"d\":\"%s\",\"nb\":%u,\"b\":[",
                    ts_ms, s_device_id, (unsigned)snap.n_bat);

    for (uint8_t i = 0; i < snap.n_bat && i < MAX_BATTERIES; i++) {
        const struct BmuBatteryC *b = &snap.batteries[i];
        if (i > 0 && pos < (int)sizeof(buf)) {
            buf[pos++] = ',';
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "{\"v\":%" PRId32 ",\"c\":%" PRId32 ",\"s\":%u,"
                        "\"sc\":%u,\"ah\":%" PRId32 ",\"soh\":%u,"
                        "\"r\":%" PRIu32 ",\"dut\":%u}",
                        b->voltage_mv, b->current_ma, (unsigned)b->state,
                        (unsigned)b->switch_count, b->ah_remaining_ma_h,
                        (unsigned)b->soh_pct, b->r_ohmic_m_ohms,
                        (unsigned)b->balancer_duty_pct);
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "],\"sy\":{\"tok\":%u,\"p50\":%" PRIu32 ",\"p99\":%" PRIu32 "}}",
                    (unsigned)snap.system.topology_ok,
                    snap.system.tick_us_p50,
                    snap.system.tick_us_p99);

    int msg_id = esp_mqtt_client_publish(s_client, s_topic_telemetry,
                                         buf, pos, 0, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "publish failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}
