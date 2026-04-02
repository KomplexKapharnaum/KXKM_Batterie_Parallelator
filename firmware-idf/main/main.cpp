/**
 * @file main.cpp
 * @brief KXKM BMU — ESP-IDF v5.4 (BOX-3)
 *
 * Init order: NVS → BSP/Display → WiFi → I2C (non-bloquant) → Protection → Cloud → Web
 * I2C scan periodique dans la boucle pour hotplug/unplug.
 * L'absence de sensors I2C ne bloque JAMAIS le boot.
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
#include "bmu_ui.h"
#include "bmu_vedirect.h"
#include "bmu_climate.h"
#include "bmu_ota.h"
// #include "bmu_soh.h"  // Disabled — TFLite build issues
#ifdef CONFIG_BMU_BLE_ENABLED
#include "bmu_ble.h"
#endif
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>

static const char *TAG = "MAIN";

/* ── Cloud telemetry task ──────────────────────────────────────────── */
typedef struct {
    bmu_protection_ctx_t  *prot;
    bmu_battery_manager_t *mgr;
    uint8_t                nb_ina;
} cloud_task_ctx_t;

static void cloud_telemetry_task(void *pv)
{
    cloud_task_ctx_t *ctx = (cloud_task_ctx_t *)pv;
    const TickType_t period = pdMS_TO_TICKS(10000);

    ESP_LOGI("CLOUD", "Telemetry task started — period 10s");

    for (;;) {
        vTaskDelay(period);
        if (!bmu_wifi_is_connected()) continue;

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

            float i_a = 0;
            bmu_ina237_read_current(&ctx->mgr->ina_devices[i], &i_a);
            bmu_influx_write_battery(i, v_mv, i_a, ah_d, ah_c, state_str);

            char payload[192];
            snprintf(payload, sizeof(payload),
                     "{\"bat\":%d,\"v_mv\":%.0f,\"i_a\":%.3f,"
                     "\"ah_d\":%.3f,\"ah_c\":%.3f,\"state\":\"%s\"}",
                     i + 1, v_mv, i_a, ah_d, ah_c, state_str);
            char topic[64];
            snprintf(topic, sizeof(topic), "bmu/%s/battery/%d",
                     bmu_config_get_device_name(), i + 1);
            bmu_mqtt_publish(topic, payload, 0, 0, false);
        }

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
    ESP_LOGI(TAG, "  KXKM BMU — ESP-IDF v5.4 (BOX-3)");
    ESP_LOGI(TAG, "  Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "══════════════════════════════════════════════");

    /* ── 1. NVS + Config runtime ──────────────────────────────────── */
    bmu_nvs_init();
    bmu_config_load();
    bmu_config_log();

    /* ── 2. SPIFFS (web assets) ────────────────────────────────────── */
    init_spiffs();

    /* ── 3. Display EN PREMIER (BSP init I2C_NUM_0 + I2C_NUM_1 dock) */
    ESP_LOGI(TAG, "Init display...");
    static bmu_protection_ctx_t prot = {};
    static bmu_battery_manager_t mgr = {};
    static bmu_display_ctx_t disp_ctx = { .prot = &prot, .mgr = &mgr, .nb_ina = 0 };
    esp_err_t disp_ret = bmu_display_init(&disp_ctx);
    if (disp_ret == ESP_OK) {
        ESP_LOGI(TAG, "Display OK");
    } else {
        ESP_LOGW(TAG, "Display init failed: %s — continuing", esp_err_to_name(disp_ret));
    }

    /* ── 4. WiFi+BLE coex (ordre critique) ───────────────────────────── */
    /* Phase 1: WiFi alloc buffers + config STA (PAS de radio)
     * Phase 2: BLE nimble_port_init (controller BT)
     * Phase 3: WiFi start radio — coex BLE/WiFi gère le partage */
    bmu_wifi_init();   /* buffers + config, pas de radio */

#ifdef CONFIG_BMU_BLE_ENABLED
    {
        esp_err_t ble_ret = bmu_ble_init(&prot, &mgr, 0);
        if (ble_ret == ESP_OK) {
            ESP_LOGI(TAG, "BLE active — '%s'", bmu_config_get_device_name());
        } else {
            ESP_LOGW(TAG, "BLE init failed: %s", esp_err_to_name(ble_ret));
        }
    }
#endif

    bmu_wifi_start();  /* lance la radio — BLE déjà init */

    /* ── 5. Storage ──────────────────────────────────────────────────── */
    bmu_fat_init();     /* internal FAT partition (config files, USB-editable) */
    bmu_config_load_battery_labels();  /* /fatfs/batteries.cfg */
    bmu_sd_init();      /* external SD card (CSV logging) */

    /* ── 6. SNTP ───────────────────────────────────────────────────── */
    if (bmu_wifi_is_connected()) {
        bmu_sntp_init();
    }

    /* ── 7. I2C BMU — NON BLOQUANT ─────────────────────────────────── */
    /* Le BSP a deja cree les deux bus I2C dans bsp_display_start().
     * bmu_i2c_init() recupere le handle du bus DOCK (I2C_NUM_1, GPIO40/41).
     * Si ca echoue (pas de pull-ups, pas de dock), on continue sans I2C. */
    i2c_master_bus_handle_t i2c_bus = NULL;
    bool i2c_ok = false;
    esp_err_t i2c_ret = bmu_i2c_init(&i2c_bus);
    if (i2c_ret == ESP_OK) {
        ESP_LOGI(TAG, "I2C BMU bus OK");
        i2c_ok = true;
    } else {
        ESP_LOGW(TAG, "I2C BMU init failed: %s — running sans sensors", esp_err_to_name(i2c_ret));
    }

    /* ── 7b. Capteur climat AHT30 (si bus ok) ────────────────────── */
    if (i2c_ok) {
        esp_err_t clim_ret = bmu_climate_init(i2c_bus);
        if (clim_ret == ESP_OK) {
            ESP_LOGI(TAG, "AHT30 climat OK — T=%.1f°C  H=%.1f%%",
                     bmu_climate_get_temperature(), bmu_climate_get_humidity());
        } else {
            ESP_LOGW(TAG, "AHT30 init failed: %s — pas de capteur climat",
                     esp_err_to_name(clim_ret));
        }
    }

    /* ── 8. Scan I2C + init sensors (si bus ok) ──────────────────── */
    static bmu_ina237_t ina[BMU_MAX_BATTERIES] = {};
    uint8_t nb_ina = 0;
    static bmu_tca9535_handle_t tca[BMU_MAX_TCA] = {};
    uint8_t nb_tca = 0;
    bool topology_ok = false;

    if (i2c_ok) {
        int dev_count = bmu_i2c_scan(i2c_bus);
        ESP_LOGI(TAG, "I2C scan: %d devices", dev_count);
        bmu_ui_debug_set_device_count(dev_count);
        bmu_ui_debug_log("I2C scan OK");

        bmu_ina237_scan_init(i2c_bus, 2000, 10.0f, ina, &nb_ina);
        ESP_LOGI(TAG, "INA237: %d", nb_ina);

        /* Yield pour eviter watchdog pendant l'init longue */
        vTaskDelay(pdMS_TO_TICKS(10));

        bmu_tca9535_scan_init(i2c_bus, tca, BMU_MAX_TCA, &nb_tca);
        ESP_LOGI(TAG, "TCA9535: %d", nb_tca);

        topology_ok = (nb_ina > 0) && (nb_tca > 0) && (nb_tca * 4 == nb_ina);
        if (!topology_ok && (nb_ina > 0 || nb_tca > 0)) {
            ESP_LOGW(TAG, "TOPOLOGY: %d TCA * 4 != %d INA", nb_tca, nb_ina);
        }
    }

    /* ── 9. Protection + Battery Manager ───────────────────────────── */
    ESP_ERROR_CHECK(bmu_protection_init(&prot, ina, nb_ina, tca, nb_tca));
    bmu_battery_manager_init(&mgr, ina, nb_ina);
    if (nb_ina > 0) {
        bmu_battery_manager_start(&mgr);
    }

    /* SOH predictor disabled — TFLite build issues */

    /* Update display context avec nb_ina reel */
    disp_ctx.nb_ina = nb_ina;

    /* ── 10. Cloud (si WiFi) ───────────────────────────────────────── */
    if (bmu_wifi_is_connected()) {
        bmu_mqtt_init();
        bmu_influx_init();

        /* Cloud telemetry task — push MQTT + InfluxDB toutes les 10s */
        static cloud_task_ctx_t cloud_ctx = {};
        cloud_ctx.prot = &prot;
        cloud_ctx.mgr = &mgr;
        cloud_ctx.nb_ina = nb_ina;
        xTaskCreate(cloud_telemetry_task, "cloud", 4096, &cloud_ctx, 2, NULL);
    }

    /* ── 11. Web server ────────────────────────────────────────────── */
    if (bmu_wifi_is_connected()) {
        static bmu_web_ctx_t web_ctx = { .prot = &prot, .mgr = &mgr };
        bmu_web_start(&web_ctx);
        char ip[16] = {};
        bmu_wifi_get_ip(ip, sizeof(ip));
        ESP_LOGI(TAG, "Web: http://%s/", ip);
    }

    /* ── 12. VE.Direct ─────────────────────────────────────────────── */
    bmu_vedirect_init();

    /* ── 13. OTA validation ────────────────────────────────────────── */
    bmu_ota_mark_valid();

    ESP_LOGI(TAG, "Init complete — heap: %lu — loop %dms",
             (unsigned long)esp_get_free_heap_size(), BMU_LOOP_PERIOD_MS);

    /* ── Main loop ─────────────────────────────────────────────────── */
    while (true) {
        if (i2c_ok) {
            if (!topology_ok) {
                bmu_protection_all_off(&prot);
            } else {
                for (int i = 0; i < nb_ina; i++) {
                    bmu_protection_check_battery(&prot, i);
                }
            }
        }
        /* SOH disabled */

        vTaskDelay(pdMS_TO_TICKS(BMU_LOOP_PERIOD_MS));
    }
}
