/**
 * @file main.cpp
 * @brief KXKM BMU — ESP-IDF v5.4 (BOX-3)
 *
 * Init order: NVS → BSP/Display → WiFi → I2C (non-bloquant) → Protection → Cloud → Web
 * I2C hotplug: tache FreeRTOS bmu_i2c_hotplug re-scanne le bus periodiquement.
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
#include "bmu_influx_store.h"
#include "bmu_balancer.h"
#include "bmu_ble_victron_gatt.h"
#include "bmu_ble_victron_scan.h"
#include "bmu_sntp.h"
#include "bmu_display.h"
#include "bmu_ui.h"
#include "bmu_vedirect.h"
#include "bmu_climate.h"
#include "bmu_ota.h"
#include "bmu_vrm.h"
#include "bmu_i2c_bitbang.h"
#if CONFIG_BMU_SOH_ENABLED
#include "bmu_soh.h"
#endif
#include "bmu_rint.h"
#ifdef CONFIG_BMU_I2C_HOTPLUG_ENABLED
#include "bmu_i2c_hotplug.h"
#endif
#ifdef CONFIG_BMU_BLE_ENABLED
#include "bmu_ble.h"
#endif
#ifdef CONFIG_BMU_VICTRON_BLE_ENABLED
#include "bmu_ble_victron.h"
#endif
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>

static const char *TAG = "MAIN";

static bool bitbang_bus_conflicts_with_vedirect(void)
{
#if CONFIG_BMU_I2C_BB_ENABLED && defined(CONFIG_BMU_VEDIRECT_ENABLED) && CONFIG_BMU_VEDIRECT_ENABLED
    const int bb_sda = CONFIG_BMU_I2C_BB_SDA_GPIO;
    const int bb_scl = CONFIG_BMU_I2C_BB_SCL_GPIO;
    const int ve_rx = CONFIG_BMU_VEDIRECT_RX_GPIO;
    const int ve_tx = CONFIG_BMU_VEDIRECT_TX_GPIO;
    return ve_rx == bb_sda || ve_rx == bb_scl ||
           (ve_tx >= 0 && (ve_tx == bb_sda || ve_tx == bb_scl));
#else
    return false;
#endif
}

/* ── Cloud telemetry task ──────────────────────────────────────────── */
typedef struct {
    bmu_protection_ctx_t  *prot;
    bmu_battery_manager_t *mgr;
    uint8_t               *nb_ina;   /* pointer — follows hotplug changes */
} cloud_task_ctx_t;

static void cloud_telemetry_task(void *pv)
{
    cloud_task_ctx_t *ctx = (cloud_task_ctx_t *)pv;
    const TickType_t period = pdMS_TO_TICKS(10000);

    ESP_LOGI("CLOUD", "Telemetry task started — period 10s");

    for (;;) {
        vTaskDelay(period);

#if CONFIG_BMU_SOH_ENABLED
        bmu_soh_update_all(ctx->mgr, ctx->prot, *ctx->nb_ina);
#endif

        if (!bmu_wifi_is_connected()) continue;

        /* Rejouer les données offline si présentes */
        if (bmu_influx_store_has_pending()) {
            int replayed = bmu_influx_store_replay();
            if (replayed > 0) {
                ESP_LOGI("CLOUD", "Replay offline: %d lignes renvoyées", replayed);
            }
        }

        for (int i = 0; i < *ctx->nb_ina; i++) {
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

        /* ── Solar telemetry ── */
        if (bmu_vedirect_is_connected()) {
            const bmu_vedirect_data_t *sol = bmu_vedirect_get_data();
            if (sol && sol->valid) {
                /* MQTT */
                char solar_payload[192];
                snprintf(solar_payload, sizeof(solar_payload),
                    "{\"vpv\":%.1f,\"ppv\":%u,\"vbat\":%.2f,"
                    "\"ibat\":%.2f,\"cs\":\"%s\",\"yield\":%lu,\"err\":%u}",
                    sol->panel_voltage_v,
                    (unsigned)sol->panel_power_w,
                    sol->battery_voltage_v,
                    sol->battery_current_a,
                    bmu_vedirect_cs_name(sol->charge_state),
                    (unsigned long)sol->yield_today_wh,
                    (unsigned)sol->error_code);
                char solar_topic[64];
                snprintf(solar_topic, sizeof(solar_topic),
                    "bmu/%s/solar", bmu_config_get_device_name());
                bmu_mqtt_publish(solar_topic, solar_payload, 0, 0, false);

                /* InfluxDB */
                char solar_tags[48];
                snprintf(solar_tags, sizeof(solar_tags),
                    "device=%s", bmu_config_get_device_name());
                char solar_fields[128];
                snprintf(solar_fields, sizeof(solar_fields),
                    "vpv=%.1f,ppv=%ui,vbat=%.2f,ibat=%.2f,cs=%ui,yield=%lui",
                    sol->panel_voltage_v,
                    (unsigned)sol->panel_power_w,
                    sol->battery_voltage_v,
                    sol->battery_current_a,
                    (unsigned)sol->charge_state,
                    (unsigned long)sol->yield_today_wh);
                bmu_influx_write("solar", solar_tags, solar_fields, 0);
            }
        }
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
    bmu_usb_msc_init();   /* TinyUSB MSC (if enabled) */
    bmu_config_load_battery_labels();  /* /fatfs/batteries.cfg */
    /* SD externe montee a la demande pour garder un boot propre sans carte. */

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

    /* ── 8. Scan I2C + init sensors (si bus ok) ──────────────────── */
    static bmu_ina237_t ina[BMU_MAX_BATTERIES] = {};
    static uint8_t nb_ina = 0;
    static bmu_tca9535_handle_t tca[BMU_MAX_TCA] = {};
    static uint8_t nb_tca = 0;
    static bool topology_ok = false;

    if (i2c_ok) {
        /* Le scan diagnostic sert de "warm-up" du bus I2C — sans lui,
         * les esclaves derriere l'ISO1540 ne repondent pas.
         * IMPORTANT: ne pas supprimer ce scan. */
        /* Scan + init avec retry — les INA237 derriere l'ISO1540
         * peuvent mettre 1-2s a repondre apres power-on. */
        for (int attempt = 0; attempt < 3; attempt++) {
            if (attempt > 0) {
                ESP_LOGW(TAG, "I2C scan retry %d/3 — attente 500ms", attempt + 1);
                i2c_master_bus_reset(i2c_bus);
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            int dev_count = bmu_i2c_scan(i2c_bus);
            ESP_LOGI(TAG, "I2C scan: %d devices", dev_count);

            /* Bus reset apres scan pour nettoyer les handles internes */
            i2c_master_bus_reset(i2c_bus);
            vTaskDelay(pdMS_TO_TICKS(50));

            bmu_ina237_scan_init(i2c_bus, 2000, 10.0f, ina, &nb_ina);
            ESP_LOGI(TAG, "INA237: %d", nb_ina);

            vTaskDelay(pdMS_TO_TICKS(10));

            bmu_tca9535_scan_init(i2c_bus, tca, BMU_MAX_TCA, &nb_tca);
            ESP_LOGI(TAG, "TCA9535: %d", nb_tca);

            if (nb_ina > 0 && nb_tca > 0) break; /* Tous trouves */
        }
        bmu_ui_debug_set_device_count(nb_ina + nb_tca);
        bmu_ui_debug_log("I2C scan OK");

        topology_ok = (nb_ina > 0) && (nb_tca > 0) && (nb_tca * 4 == nb_ina);
        if (!topology_ok && (nb_ina > 0 || nb_tca > 0)) {
            ESP_LOGW(TAG, "TOPOLOGY: %d TCA * 4 != %d INA", nb_tca, nb_ina);
        }
        bmu_ui_debug_set_device_count(nb_ina + nb_tca);
        bmu_ui_debug_log("I2C scan OK");
    }

    /* ── 8a. Capteur climat AHT30 (apres scan BMU pour eviter la concurrence boot) ── */
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

    /* ── 8b. I2C Bus 2 — bit-bang (batteries 17-32) ──────────────────── */
#if CONFIG_BMU_I2C_BB_ENABLED
    static bmu_ina237_bb_t ina_bb[INA237_MAX_DEVICES] = {};
    uint8_t nb_ina_bb = 0;
    static bmu_tca9535_bb_handle_t tca_bb[TCA9535_MAX_DEVICES] = {};
    uint8_t nb_tca_bb = 0;

#if defined(CONFIG_BMU_VEDIRECT_ENABLED) && CONFIG_BMU_VEDIRECT_ENABLED
    if (bitbang_bus_conflicts_with_vedirect()) {
        ESP_LOGW(TAG, "I2C bus 2 skippe: conflit de pins avec VE.Direct "
                      "(BB SDA=%d SCL=%d, VE RX=%d TX=%d)",
                 CONFIG_BMU_I2C_BB_SDA_GPIO,
                 CONFIG_BMU_I2C_BB_SCL_GPIO,
                 CONFIG_BMU_VEDIRECT_RX_GPIO,
                 CONFIG_BMU_VEDIRECT_TX_GPIO);
    } else
#endif
    {
        bmu_i2c_bb_config_t bb_cfg = {
            .sda_gpio = CONFIG_BMU_I2C_BB_SDA_GPIO,
            .scl_gpio = CONFIG_BMU_I2C_BB_SCL_GPIO,
            .freq_hz = CONFIG_BMU_I2C_BB_FREQ_HZ,
        };
        bmu_i2c_bb_handle_t bb_bus = NULL;
        if (bmu_i2c_bb_init(&bb_cfg, &bb_bus) == ESP_OK) {
            ESP_LOGI(TAG, "I2C bus 2 (bit-bang) OK");

            bmu_ina237_bb_scan_init(bb_bus, 2000, 10.0f, ina_bb, &nb_ina_bb);
            ESP_LOGI(TAG, "Bus 2 INA237: %d", nb_ina_bb);

            vTaskDelay(pdMS_TO_TICKS(10));

            bmu_tca9535_bb_scan_init(bb_bus, tca_bb, TCA9535_MAX_DEVICES, &nb_tca_bb);
            ESP_LOGI(TAG, "Bus 2 TCA9535: %d", nb_tca_bb);
        } else {
            ESP_LOGW(TAG, "I2C bus 2 init failed — single bus mode");
        }
    }

    /* Update topology for dual bus */
    {
        bool bus1_ok = (nb_ina == 0 && nb_tca == 0) || (nb_tca * 4 == nb_ina);
        bool bus2_ok = (nb_ina_bb == 0 && nb_tca_bb == 0) || (nb_tca_bb * 4 == nb_ina_bb);
        topology_ok = bus1_ok && bus2_ok;
        if (!topology_ok) {
            ESP_LOGW(TAG, "TOPOLOGY: bus1(%d TCA * 4 != %d INA) bus2(%d TCA * 4 != %d INA)",
                     nb_tca, nb_ina, nb_tca_bb, nb_ina_bb);
        }
    }

    uint8_t total_ina = nb_ina + nb_ina_bb;
    ESP_LOGI(TAG, "Total batteries: %d (%d + %d)", total_ina, nb_ina, nb_ina_bb);
#else
    uint8_t nb_ina_bb = 0;
    uint8_t total_ina = nb_ina;
#endif

    /* ── 9. Protection + Battery Manager ───────────────────────────── */
    ESP_ERROR_CHECK(bmu_protection_init(&prot, ina, nb_ina, tca, nb_tca));
    bmu_battery_manager_init(&mgr, ina, nb_ina);
    bmu_ble_set_nb_ina(nb_ina); /* Update BLE after I2C scan */
    if (nb_ina > 0) {
        bmu_battery_manager_start(&mgr);
    }

#if CONFIG_BMU_SOH_ENABLED
    if (bmu_soh_init() == ESP_OK && nb_ina > 0) {
        ESP_LOGI(TAG, "SOH predictor ready — %d batteries", nb_ina);
    }
#endif

#if CONFIG_BMU_RINT_ENABLED
    bmu_rint_set_ctx(&prot);
    bmu_rint_init();
    if (nb_ina > 0) {
        bmu_rint_start_periodic();
    }
#endif

    /* Update display context avec nb_ina reel */
    disp_ctx.nb_ina = total_ina;
    bmu_display_request_update();

    /* ── 9c. I2C Hotplug (si bus ok ET devices trouves au boot) ─────── */
#ifdef CONFIG_BMU_I2C_HOTPLUG_ENABLED
    if (i2c_ok && (nb_ina > 0 || nb_tca > 0)) {
        bmu_hotplug_cfg_t hp_cfg = {
            .bus = i2c_bus,
            .ina_devices = ina,
            .tca_devices = tca,
            .nb_ina = &nb_ina,
            .nb_tca = &nb_tca,
            .topology_ok = &topology_ok,
            .prot = &prot,
            .mgr = &mgr,
        };
        if (bmu_hotplug_init(&hp_cfg) == ESP_OK) {
            bmu_hotplug_start();
            ESP_LOGI(TAG, "I2C hotplug active — scan every %ds",
                     CONFIG_BMU_I2C_HOTPLUG_INTERVAL_S);
        }
    }
#endif

    /* ── 9b. BLE Victron (si enabled) ──────────────────────────────── */
#ifdef CONFIG_BMU_VICTRON_BLE_ENABLED
    bmu_ble_victron_init(&prot, &mgr, nb_ina);
#endif

#ifdef CONFIG_BMU_VIC_SCAN_ENABLED
    bmu_vic_scan_init();
    bmu_vic_scan_start();
    ESP_LOGI(TAG, "Victron BLE scanner started");
#endif

    /* ── 10. Cloud (si WiFi) ───────────────────────────────────────── */
    bmu_influx_store_init(); /* Toujours init — persiste quand WiFi tombe */
    bmu_balancer_init(&prot);
    if (bmu_wifi_is_connected()) {
        bmu_mqtt_init();
        bmu_influx_init();

        /* Cloud telemetry task — push MQTT + InfluxDB toutes les 10s */
        static cloud_task_ctx_t cloud_ctx = {};
        cloud_ctx.prot = &prot;
        cloud_ctx.mgr = &mgr;
        cloud_ctx.nb_ina = &nb_ina;
        xTaskCreate(cloud_telemetry_task, "cloud", 4096, &cloud_ctx, 2, NULL);

        /* VRM — publish to Victron cloud */
        bmu_vrm_init(&prot, &mgr, nb_ina);
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
    bool topology_fail_safe_applied = false;
    while (true) {
        if (i2c_ok) {
            if (!topology_ok) {
                if (!topology_fail_safe_applied) {
                    ESP_LOGW(TAG, "Topology invalid — fail-safe all OFF applied once");
                    bmu_protection_all_off(&prot);
                    topology_fail_safe_applied = true;
                }
            } else {
                topology_fail_safe_applied = false;
                for (int i = 0; i < nb_ina; i++) {
                    /* Skip la protection si le balancer a mis cette batterie en OFF volontaire */
                    if (bmu_balancer_is_off((uint8_t)i)) continue;
                    bmu_protection_check_battery(&prot, i);
                }
                /* Soft-balancing : duty-cycling des batteries trop chargees */
                int balancing = bmu_balancer_tick();
                if (balancing > 0) {
                    ESP_LOGD("MAIN", "%d batterie(s) en equilibrage", balancing);
                }
                /* Update display context if hotplug changed nb_ina */
                if (disp_ctx.nb_ina != nb_ina) {
                    disp_ctx.nb_ina = nb_ina;
                    bmu_display_request_update();
                }
            }
        }
        /* SOH disabled */

        vTaskDelay(pdMS_TO_TICKS(BMU_LOOP_PERIOD_MS));
    }
}
