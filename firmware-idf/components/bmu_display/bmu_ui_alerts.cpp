#include "bmu_ui.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "UI_ALERT";

#define MAX_ALERTS 30

typedef struct {
    char text[80];
    char detail[80];
    char timestamp[12];
    alert_type_t type;
} alert_entry_t;

static alert_entry_t s_alerts[MAX_ALERTS] = {};
static int s_alert_count = 0;
static lv_obj_t *s_alert_list = NULL;
static lv_obj_t *s_empty_label = NULL;

static lv_color_t alert_border_color(alert_type_t t)
{
    switch (t) {
        case ALERT_ERROR:   return UI_COLOR_ERR;
        case ALERT_WARNING: return UI_COLOR_WARN;
        case ALERT_INFO:    return UI_COLOR_OK;
        case ALERT_SYSTEM:  return UI_COLOR_INFO;
    }
    return UI_COLOR_TEXT_DIM;
}

static lv_color_t alert_bg_color(alert_type_t t)
{
    switch (t) {
        case ALERT_ERROR:   return UI_COLOR_BG_ERR;
        case ALERT_WARNING: return UI_COLOR_BG_WARN;
        default:            return UI_COLOR_BG;
    }
}

void bmu_ui_alerts_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);

    /* Titre */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, LV_SYMBOL_WARNING " Alertes");
    lv_obj_set_style_text_color(title, UI_COLOR_ERR, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 4);

    /* Bouton CLEAR */
    lv_obj_t *btn_clear = lv_button_create(parent);
    lv_obj_set_size(btn_clear, 60, 22);
    lv_obj_align(btn_clear, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_color(btn_clear, UI_COLOR_CARD_ALT, 0);
    lv_obj_t *btn_lbl = lv_label_create(btn_clear);
    lv_label_set_text(btn_lbl, "CLEAR");
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(btn_clear, [](lv_event_t *e) {
        (void)e;
        s_alert_count = 0;
        memset(s_alerts, 0, sizeof(s_alerts));
        if (s_alert_list) lv_obj_clean(s_alert_list);
        if (s_empty_label) lv_obj_remove_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Alertes effacees");
    }, LV_EVENT_CLICKED, NULL);

    /* Conteneur liste */
    s_alert_list = lv_obj_create(parent);
    lv_obj_set_size(s_alert_list, 312, 190);
    lv_obj_align(s_alert_list, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_flex_flow(s_alert_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_alert_list, 2, 0);
    lv_obj_set_style_bg_color(s_alert_list, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(s_alert_list, 0, 0);
    lv_obj_add_flag(s_alert_list, LV_OBJ_FLAG_SCROLLABLE);

    /* Message vide */
    s_empty_label = lv_label_create(s_alert_list);
    lv_label_set_text(s_empty_label, "Aucune alerte");
    lv_obj_set_style_text_color(s_empty_label, UI_COLOR_TEXT_DIM, 0);
    if (s_alert_count > 0) lv_obj_add_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);
}

/* API legacy — délègue vers le nouveau format */
void bmu_ui_alerts_add(const char *timestamp, const char *message, lv_color_t color)
{
    (void)color;
    bmu_ui_alerts_add_ex(timestamp, message, "", ALERT_INFO);
}

void bmu_ui_alerts_add_ex(const char *timestamp, const char *title,
                          const char *detail, alert_type_t type)
{
    if (s_alert_count >= MAX_ALERTS) {
        /* Décaler les anciennes alertes */
        for (int i = 0; i < MAX_ALERTS - 1; i++)
            s_alerts[i] = s_alerts[i + 1];
        s_alert_count = MAX_ALERTS - 1;
    }

    alert_entry_t *a = &s_alerts[s_alert_count];
    snprintf(a->timestamp, sizeof(a->timestamp), "%s", timestamp ? timestamp : "??:??:??");
    snprintf(a->text, sizeof(a->text), "%s", title ? title : "");
    snprintf(a->detail, sizeof(a->detail), "%s", detail ? detail : "");
    a->type = type;
    s_alert_count++;

    /* Reconstruire la liste UI */
    if (s_alert_list == NULL) return;
    lv_obj_clean(s_alert_list);

    /* Recréer le label vide (caché) */
    s_empty_label = lv_label_create(s_alert_list);
    lv_label_set_text(s_empty_label, "Aucune alerte");
    lv_obj_set_style_text_color(s_empty_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_add_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);

    /* Afficher les plus récentes en premier */
    for (int i = s_alert_count - 1; i >= 0; i--) {
        lv_obj_t *row = lv_obj_create(s_alert_list);
        lv_obj_set_size(row, 296, 32);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_all(row, 2, 0);
        lv_obj_set_style_bg_color(row, alert_bg_color(s_alerts[i].type), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(row, 3, 0);
        lv_obj_set_style_border_color(row, alert_border_color(s_alerts[i].type), 0);

        /* Horodatage */
        lv_obj_t *ts = lv_label_create(row);
        lv_label_set_text(ts, s_alerts[i].timestamp);
        lv_obj_set_style_text_color(ts, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_width(ts, 60);

        /* Colonne titre + détail */
        lv_obj_t *text_col = lv_obj_create(row);
        lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_grow(text_col, 1);
        lv_obj_set_style_bg_opa(text_col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(text_col, 0, 0);
        lv_obj_set_style_pad_all(text_col, 0, 0);

        lv_obj_t *ttl = lv_label_create(text_col);
        lv_label_set_text(ttl, s_alerts[i].text);
        lv_obj_set_style_text_color(ttl, UI_COLOR_TEXT, 0);

        if (s_alerts[i].detail[0] != '\0') {
            lv_obj_t *dtl = lv_label_create(text_col);
            lv_label_set_text(dtl, s_alerts[i].detail);
            lv_obj_set_style_text_color(dtl, UI_COLOR_TEXT_SEC, 0);
        }
    }
}

int bmu_ui_alerts_get_count(void)
{
    return s_alert_count;
}
