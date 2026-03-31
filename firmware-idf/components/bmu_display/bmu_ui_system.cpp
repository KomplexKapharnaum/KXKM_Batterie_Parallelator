#include "bmu_ui.h"
#include "bmu_wifi.h"
#include "bmu_mqtt.h"
#include "bmu_storage.h"
#include "bmu_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include <cstdio>

static lv_obj_t *sys_labels[10] = {};

void bmu_ui_system_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x1E1E1E), 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Systeme");
    lv_obj_set_style_text_color(title, lv_color_hex(0x2979FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    const char *rows[] = {
        "Firmware:", "Heap libre:", "Uptime:", "WiFi:", "MQTT:",
        "InfluxDB:", "SD Card:", "NTP:", "INA237:", "TCA9535:"
    };
    for (int i = 0; i < 10; i++) {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, rows[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x9E9E9E), 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, 28 + i * 20);

        sys_labels[i] = lv_label_create(parent);
        lv_label_set_text(sys_labels[i], "---");
        lv_obj_set_style_text_color(sys_labels[i], lv_color_white(), 0);
        lv_obj_align(sys_labels[i], LV_ALIGN_TOP_LEFT, 120, 28 + i * 20);
    }
}

void bmu_ui_system_update(bmu_ui_ctx_t *ctx)
{
    char buf[48];
    const esp_app_desc_t *desc = esp_app_get_description();
    snprintf(buf, sizeof(buf), "%.20s (%.20s)", desc->version, desc->date);
    lv_label_set_text(sys_labels[0], buf);

    snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(esp_get_free_heap_size() / 1024));
    lv_label_set_text(sys_labels[1], buf);

    uint32_t secs = (uint32_t)(esp_timer_get_time() / 1000000);
    snprintf(buf, sizeof(buf), "%luh %02lum", (unsigned long)(secs/3600), (unsigned long)((secs%3600)/60));
    lv_label_set_text(sys_labels[2], buf);

    char ip[16] = "N/A";
    if (bmu_wifi_is_connected()) bmu_wifi_get_ip(ip, sizeof(ip));
    lv_label_set_text(sys_labels[3], ip);

    lv_label_set_text(sys_labels[4], bmu_mqtt_is_connected() ? "Connected" : "Disconnected");
    lv_label_set_text(sys_labels[5], "---");
    lv_label_set_text(sys_labels[6], bmu_sd_is_mounted() ? "Mounted" : "Not mounted");
    lv_label_set_text(sys_labels[7], bmu_sntp_is_synced() ? "Synced" : "Not synced");
    snprintf(buf, sizeof(buf), "%d", ctx->nb_ina);
    lv_label_set_text(sys_labels[8], buf);
    lv_label_set_text(sys_labels[9], "---");
}
