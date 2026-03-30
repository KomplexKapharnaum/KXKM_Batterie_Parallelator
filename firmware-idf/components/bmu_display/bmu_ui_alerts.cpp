#include "bmu_ui.h"
#include <cstdio>

#define MAX_ALERTS 20

typedef struct {
    char text[64];
    lv_color_t color;
} alert_entry_t;

static alert_entry_t alerts[MAX_ALERTS] = {};
static int alert_count = 0;
static lv_obj_t *alert_list = NULL;

void bmu_ui_alerts_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x1E1E1E), 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Alertes");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF1744), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    alert_list = lv_list_create(parent);
    lv_obj_set_size(alert_list, 310, 210);
    lv_obj_align(alert_list, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_color(alert_list, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(alert_list, 0, 0);

    if (alert_count == 0) {
        lv_obj_t *empty = lv_label_create(alert_list);
        lv_label_set_text(empty, "Aucune alerte");
        lv_obj_set_style_text_color(empty, lv_color_hex(0x9E9E9E), 0);
    }
}

void bmu_ui_alerts_add(const char *timestamp, const char *message, lv_color_t color)
{
    if (alert_count >= MAX_ALERTS) {
        // Shift old alerts
        for (int i = 0; i < MAX_ALERTS - 1; i++) {
            alerts[i] = alerts[i + 1];
        }
        alert_count = MAX_ALERTS - 1;
    }

    snprintf(alerts[alert_count].text, sizeof(alerts[alert_count].text),
             "%s %s", timestamp ? timestamp : "??:??:??", message);
    alerts[alert_count].color = color;
    alert_count++;

    // Refresh list if visible
    if (alert_list != NULL) {
        lv_obj_clean(alert_list);
        for (int i = alert_count - 1; i >= 0; i--) {
            lv_obj_t *item = lv_label_create(alert_list);
            lv_label_set_text(item, alerts[i].text);
            lv_obj_set_style_text_color(item, alerts[i].color, 0);
            lv_obj_set_style_text_font(item, &lv_font_montserrat_10, 0);
        }
    }
}
