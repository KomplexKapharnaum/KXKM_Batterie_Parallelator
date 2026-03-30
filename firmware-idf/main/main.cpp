/**
 * @file main.cpp
 * @brief KXKM BMU — ESP-IDF v5.3 (Phase 6+7: Full firmware + Display + VE.Direct)
 *
 * Init: NVS → WiFi → SPIFFS → SD → SNTP → I2C → Protection → MQTT → InfluxDB → Display → VE.Direct → Web
 * Tasks: protection (main), Ah tracking (per battery), cloud telemetry, LVGL render, VE.Direct RX
 */
#include "bmu_i2c.h"
#include "bmu_ina237.h"
#include "bmu_tca9535.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_config.h"
#include "bmu_wifi.h"
#include "bmu_web.h"
#include "bmu_storage.h"
#include "bmu_mqtt.h"
#include "bmu_influx.h"
#include "bmu_sntp.h"
#include "bmu_display.h"
#include "bsp/esp-bsp.h"
#include "bmu_vedirect.h"
#include "bmu_ota.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>

static const char *TAG = "MAIN";

/* ── Cloud telemetry task context ──────────────────────────────────── */
typedef struct {
    bmu_protection_ctx_t    *prot;
    bmu_battery_manager_t   *mgr;
    uint8_t                  nb_ina;
} cloud_task_ctx_t;

static void cloud_telemetry_task(void *pv)
{
    cloud_task_ctx_t *ctx = (cloud_task_ctx_t *)pv;
    const TickType_t period = pdMS_TO_TICKS(10000); /* Push every 10s */

    ESP_LOGI("CLOUD", "Telemetry task started — period 10s");

    for (;;) {
        vTaskDelay(period);

        if (!bmu_wifi_is_connected()) continue;

        /* Publish battery data via MQTT + InfluxDB */
        for (int i = 0; i < ctx->nb_ina; i++) {
            float v_mv = bmu_protection_get_voltage(ctx->prot, i);
            float ah_d = bmu_battery_manager_get_ah_discharge(ctx->mgr, i);
            float ah_c = bmu_battery_manager_get_ah_charge(ctx->mgr, i);
            bmu_battery_state_t state = bmu_protection_get_state(ctx->prot, i);

            const char *state_str = "unknown";
            switch (state) {
                case BMU_STATE_CONNECTED:    state_str = "connected"; break;
                case BMU_STATE_DISCONNECTED: state_str = "disconnected"; break;
                case BMU_STATE_RECONNECTING: state_str = "reconnecting"; break;
                case BMU_STATE_ERROR:        state_str = "error"; break;
                case BMU_STATE_LOCKED:       state_str = "locked"; break;
            }

            /* InfluxDB line protocol */
            float i_a = 0;
            bmu_ina237_read_current(&ctx->mgr->ina_devices[i], &i_a);
            bmu_influx_write_battery(i, v_mv, i_a, ah_d, ah_c, state_str);

            /* MQTT JSON */
            char payload[192];
            snprintf(payload, sizeof(payload),
                     "{\"bat\":%d,\"v_mv\":%.0f,\"i_a\":%.3f,\"ah_d\":%.3f,\"ah_c\":%.3f,\"state\":\"%s\"}",
                     i + 1, v_mv, i_a, ah_d, ah_c, state_str);
            char topic[48];
            snprintf(topic, sizeof(topic), "bmu/battery/%d", i + 1);
            bmu_mqtt_publish(topic, payload, 0, 0, false);
        }

        /* Flush InfluxDB buffer */
        bmu_influx_flush();
    }
}

/* ── SPIFFS init ───────────────────────────────────────────────────── */
static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK) {
        size_t total = 0, used = 0;
        esp_spiffs_info("storage", &total, &used);
        ESP_LOGI(TAG, "SPIFFS: total=%zu used=%zu", total, used);
    } else {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* ── app_main ──────────────────────────────────────────────────────── */
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "══════════════════════════════════════════════");
    ESP_LOGI(TAG, "  KXKM BMU — ESP-IDF v5.3");
    ESP_LOGI(TAG, "  Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "══════════════════════════════════════════════");
    bmu_config_log();

    /* ── 1. NVS + WiFi ─────────────────────────────────────────────── */
    bmu_nvs_init();
    bmu_wifi_init();

    /* ── 2. SPIFFS (web assets) ────────────────────────────────────── */
    init_spiffs();

    /* ── 3. SD card ────────────────────────────────────────────────── */
    bmu_sd_init();

    /* ── 4. SNTP time sync ─────────────────────────────────────────── */
    if (bmu_wifi_is_connected()) {
        bmu_sntp_init();
    }

    /* ── 5. I2C + sensors ──────────────────────────────────────────── */
    /* Le BSP BOX-3 cree les deux bus I2C (I2C_NUM_0 interne + I2C_NUM_1 DOCK/PMOD)
     * → doit imperativement etre appele AVANT bmu_i2c_init qui recupere le bus DOCK */
    ESP_ERROR_CHECK(bsp_i2c_init());

    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(bmu_i2c_init(&i2c_bus));
    bmu_i2c_scan(i2c_bus);

    static bmu_ina237_t ina[BMU_MAX_BATTERIES] = {};
    uint8_t nb_ina = 0;
    bmu_ina237_scan_init(i2c_bus, 2000, 10.0f, ina, &nb_ina);
    for (int i = 0; i < nb_ina; i++) {
        bmu_ina237_set_bus_voltage_alerts(&ina[i], BMU_MAX_VOLTAGE_MV, BMU_MIN_VOLTAGE_MV);
    }

    static bmu_tca9535_handle_t tca[BMU_MAX_TCA] = {};
    uint8_t nb_tca = 0;
    bmu_tca9535_scan_init(i2c_bus, tca, BMU_MAX_TCA, &nb_tca);

    bool topology_ok = (nb_ina > 0) && (nb_tca > 0) && (nb_tca * 4 == nb_ina);
    if (!topology_ok) {
        ESP_LOGE(TAG, "TOPOLOGY MISMATCH %d TCA * 4 != %d INA — FAIL-SAFE", nb_tca, nb_ina);
    }

    /* ── 6. Protection + Battery Manager ───────────────────────────── */
    static bmu_protection_ctx_t prot = {};
    ESP_ERROR_CHECK(bmu_protection_init(&prot, ina, nb_ina, tca, nb_tca));

    static bmu_battery_manager_t mgr = {};
    bmu_battery_manager_init(&mgr, ina, nb_ina);
    for (int i = 0; i < nb_ina; i++) {
        bmu_battery_manager_start_ah_task(&mgr, i);
    }

    /* ── 7. MQTT + InfluxDB ────────────────────────────────────────── */
    if (bmu_wifi_is_connected()) {
        bmu_mqtt_init();
        bmu_influx_init();

        /* Cloud telemetry task — pushes data every 10s */
        static cloud_task_ctx_t cloud_ctx = { .prot = &prot, .mgr = &mgr, .nb_ina = nb_ina };
        xTaskCreate(cloud_telemetry_task, "cloud", 4096, &cloud_ctx, 1, NULL);
    }

    /* ── 8. Web server ─────────────────────────────────────────────── */
    if (bmu_wifi_is_connected()) {
        static bmu_web_ctx_t web_ctx = { .prot = &prot, .mgr = &mgr };
        bmu_web_start(&web_ctx);
        char ip[16] = {};
        bmu_wifi_get_ip(ip, sizeof(ip));
        ESP_LOGI(TAG, "Web server at http://%s/", ip);
    }

    /* ── 9. Display Dashboard (BOX-3 ILI9342C + LVGL) ─────────────── */
    static bmu_display_ctx_t disp_ctx = { .prot = &prot, .mgr = &mgr, .nb_ina = nb_ina };
    esp_err_t disp_ret = bmu_display_init(&disp_ctx);
    if (disp_ret == ESP_OK) {
        ESP_LOGI(TAG, "Display dashboard initialized");
    } else {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(disp_ret));
    }

    /* ── 10. VE.Direct (Victron MPPT solar charger) ────────────────── */
    bmu_vedirect_init();
    if (bmu_vedirect_is_connected()) {
        ESP_LOGI(TAG, "VE.Direct charger detected");
    } else {
        ESP_LOGI(TAG, "VE.Direct: no charger (will auto-detect)");
    }

    /* ── 11. OTA validation ─────────────────────────────────────────── */
    esp_err_t ota_ret = bmu_ota_mark_valid();
    if (ota_ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA: firmware marque comme valide");
    } else {
        ESP_LOGW(TAG, "OTA mark_valid: %s (ignore si pas d'OTA)", esp_err_to_name(ota_ret));
    }

    ESP_LOGI(TAG, "Init complete — protection loop (%d ms)", BMU_LOOP_PERIOD_MS);

    /* ── Main protection loop ──────────────────────────────────────── */
    while (true) {
        if (!topology_ok) {
            bmu_protection_all_off(&prot);
        } else {
            for (int i = 0; i < nb_ina; i++) {
                bmu_protection_check_battery(&prot, i);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BMU_LOOP_PERIOD_MS));
    }
}
