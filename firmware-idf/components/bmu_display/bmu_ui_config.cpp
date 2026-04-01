#include "bmu_ui.h"
#include "bmu_config.h"
#include "bmu_wifi.h"
#include "bmu_ble.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "UI_CFG";

static lv_obj_t *s_name_ta = NULL;
static lv_obj_t *s_ssid_ta = NULL;
static lv_obj_t *s_pass_ta = NULL;
static lv_obj_t *s_mqtt_ta = NULL;

/* Valeurs seuils — modifiées par les steppers */
static uint16_t s_v_min  = 24000;
static uint16_t s_v_max  = 30000;
static uint16_t s_i_max  = 10000;
static uint16_t s_v_diff = 1000;

static lv_obj_t *s_vmin_lbl  = NULL;
static lv_obj_t *s_vmax_lbl  = NULL;
static lv_obj_t *s_imax_lbl  = NULL;
static lv_obj_t *s_vdiff_lbl = NULL;

static lv_obj_t *s_ble_sw       = NULL;
static lv_obj_t *s_bright_slider = NULL;

/* ------------------------------------------------------------------ */
/* Stepper                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    uint16_t   *value;
    uint16_t    min_val;
    uint16_t    max_val;
    uint16_t    step;
    lv_obj_t  **label;
    const char *fmt; /* ex: "%u mV" */
} stepper_ctx_t;

static stepper_ctx_t s_steppers[4]; /* v_min, v_max, i_max, v_diff */

static void stepper_update_label(stepper_ctx_t *sc)
{
    char buf[24];
    snprintf(buf, sizeof(buf), sc->fmt, *sc->value);
    lv_label_set_text(*sc->label, buf);
}

static void stepper_minus_cb(lv_event_t *e)
{
    stepper_ctx_t *sc = (stepper_ctx_t *)lv_event_get_user_data(e);
    if (*sc->value > sc->min_val) *sc->value -= sc->step;
    stepper_update_label(sc);
}

static void stepper_plus_cb(lv_event_t *e)
{
    stepper_ctx_t *sc = (stepper_ctx_t *)lv_event_get_user_data(e);
    if (*sc->value < sc->max_val) *sc->value += sc->step;
    stepper_update_label(sc);
}

static void create_stepper(lv_obj_t *parent, const char *label_text,
                            stepper_ctx_t *sc)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 28);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 4, 0);

    /* Libellé gauche */
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_width(lbl, 55);

    /* Bouton moins */
    lv_obj_t *btn_minus = lv_button_create(row);
    lv_obj_set_size(btn_minus, 28, 22);
    lv_obj_set_style_bg_color(btn_minus, UI_COLOR_CARD_ALT, 0);
    lv_obj_t *ml = lv_label_create(btn_minus);
    lv_label_set_text(ml, LV_SYMBOL_MINUS);
    lv_obj_center(ml);
    lv_obj_add_event_cb(btn_minus, stepper_minus_cb, LV_EVENT_CLICKED, sc);

    /* Libellé valeur */
    *sc->label = lv_label_create(row);
    lv_obj_set_style_text_color(*sc->label, UI_COLOR_TEXT, 0);
    lv_obj_set_width(*sc->label, 80);
    lv_obj_set_style_text_align(*sc->label, LV_TEXT_ALIGN_CENTER, 0);
    stepper_update_label(sc);

    /* Bouton plus */
    lv_obj_t *btn_plus = lv_button_create(row);
    lv_obj_set_size(btn_plus, 28, 22);
    lv_obj_set_style_bg_color(btn_plus, UI_COLOR_CARD_ALT, 0);
    lv_obj_t *pl = lv_label_create(btn_plus);
    lv_label_set_text(pl, LV_SYMBOL_PLUS);
    lv_obj_center(pl);
    lv_obj_add_event_cb(btn_plus, stepper_plus_cb, LV_EVENT_CLICKED, sc);
}

/* ------------------------------------------------------------------ */
/* Helpers section / textarea                                          */
/* ------------------------------------------------------------------ */

static lv_obj_t *section_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_opa(lbl, LV_OPA_70, 0);
    lv_obj_set_width(lbl, lv_pct(100));
    return lbl;
}

static lv_obj_t *create_textarea(lv_obj_t *parent, const char *initial_text,
                                  bool password, int max_len)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_len);
    lv_textarea_set_text(ta, initial_text ? initial_text : "");
    if (password) lv_textarea_set_password_mode(ta, true);
    lv_obj_set_width(ta, lv_pct(100));
    lv_obj_set_height(ta, 30);
    lv_obj_set_style_bg_color(ta, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(ta, UI_COLOR_TEXT, 0);
    lv_obj_set_style_border_color(ta, UI_COLOR_BORDER, 0);
    return ta;
}

/* ------------------------------------------------------------------ */
/* API publique                                                        */
/* ------------------------------------------------------------------ */

void bmu_ui_config_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);

    /* Conteneur scrollable */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 312, 210);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 4, 0);
    lv_obj_set_style_pad_all(cont, 4, 0);
    lv_obj_set_style_bg_color(cont, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* --- Section : Nom appareil --- */
    section_label(cont, "NOM APPAREIL");
    s_name_ta = create_textarea(cont, bmu_config_get_device_name(), false, 31);

    /* --- Section : WiFi --- */
    section_label(cont, "WIFI");
    s_ssid_ta = create_textarea(cont, bmu_config_get_wifi_ssid(), false, 31);
    s_pass_ta = create_textarea(cont, bmu_config_get_wifi_password(), true, 63);

    /* --- Section : Seuils protection --- */
    section_label(cont, "SEUILS PROTECTION");
    bmu_config_get_thresholds(&s_v_min, &s_v_max, &s_i_max, &s_v_diff);

    s_steppers[0] = {&s_v_min,  20000, 30000,  500, &s_vmin_lbl,  "%u mV"};
    s_steppers[1] = {&s_v_max,  25000, 35000,  500, &s_vmax_lbl,  "%u mV"};
    s_steppers[2] = {&s_i_max,   1000, 50000, 1000, &s_imax_lbl,  "%u mA"};
    s_steppers[3] = {&s_v_diff,   100,  5000,  100, &s_vdiff_lbl, "%u mV"};

    create_stepper(cont, "V min",  &s_steppers[0]);
    create_stepper(cont, "V max",  &s_steppers[1]);
    create_stepper(cont, "I max",  &s_steppers[2]);
    create_stepper(cont, "V diff", &s_steppers[3]);

    /* --- Section : MQTT --- */
    section_label(cont, "MQTT");
    s_mqtt_ta = create_textarea(cont, bmu_config_get_mqtt_uri(), false, 63);

    /* --- Section : Options --- */
    section_label(cont, "OPTIONS");

    /* Ligne BLE */
    lv_obj_t *ble_row = lv_obj_create(cont);
    lv_obj_set_size(ble_row, lv_pct(100), 28);
    lv_obj_set_flex_flow(ble_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ble_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(ble_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ble_row, 0, 0);
    lv_obj_set_style_pad_all(ble_row, 0, 0);
    lv_obj_t *ble_lbl = lv_label_create(ble_row);
    lv_label_set_text(ble_lbl, "BLE");
    lv_obj_set_style_text_color(ble_lbl, UI_COLOR_TEXT_SEC, 0);
    s_ble_sw = lv_switch_create(ble_row);
    lv_obj_set_style_bg_color(s_ble_sw, UI_COLOR_OK, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_state(s_ble_sw, LV_STATE_CHECKED); /* BLE activé par défaut */

    /* Ligne luminosité */
    lv_obj_t *br_row = lv_obj_create(cont);
    lv_obj_set_size(br_row, lv_pct(100), 28);
    lv_obj_set_flex_flow(br_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(br_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(br_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(br_row, 0, 0);
    lv_obj_set_style_pad_all(br_row, 0, 0);
    lv_obj_set_style_pad_column(br_row, 8, 0);
    lv_obj_t *br_lbl = lv_label_create(br_row);
    lv_label_set_text(br_lbl, LV_SYMBOL_IMAGE " Lum");
    lv_obj_set_style_text_color(br_lbl, UI_COLOR_TEXT_SEC, 0);
    s_bright_slider = lv_slider_create(br_row);
    lv_slider_set_range(s_bright_slider, 10, 100);
    lv_slider_set_value(s_bright_slider, 100, LV_ANIM_OFF);
    lv_obj_set_width(s_bright_slider, 180);
    lv_obj_set_style_bg_color(s_bright_slider, UI_COLOR_SOLAR, LV_PART_INDICATOR);
    lv_obj_add_event_cb(s_bright_slider, [](lv_event_t *e) {
        lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
        int val = lv_slider_get_value(slider);
        bsp_display_brightness_set(val);
    }, LV_EVENT_VALUE_CHANGED, NULL);

    /* --- Bouton Sauvegarder --- */
    lv_obj_t *save_btn = lv_button_create(cont);
    lv_obj_set_size(save_btn, lv_pct(100), 36);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x00CC44), 0);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, LV_SYMBOL_SAVE " SAUVEGARDER");
    lv_obj_set_style_text_color(save_lbl, lv_color_hex(0x000000), 0);
    lv_obj_center(save_lbl);
    lv_obj_add_event_cb(save_btn, [](lv_event_t *e) {
        (void)e;
        bmu_config_set_device_name(lv_textarea_get_text(s_name_ta));
        bmu_config_set_wifi(lv_textarea_get_text(s_ssid_ta),
                            lv_textarea_get_text(s_pass_ta));
        bmu_config_set_thresholds(s_v_min, s_v_max, s_i_max, s_v_diff);
        bmu_config_set_mqtt_uri(lv_textarea_get_text(s_mqtt_ta));
        ESP_LOGI("UI_CFG", "Config sauvegardee dans NVS");
    }, LV_EVENT_CLICKED, NULL);

    ESP_LOGI(TAG, "Config screen cree");
}

void bmu_ui_config_update(void)
{
    /* Ecran majoritairement statique — rien à rafraîchir */
}
