/**
 * @file bmu_ui_solar.cpp
 * @brief Ecran Solar — affiche les donnees VE.Direct du chargeur Victron MPPT.
 *
 * Lit bmu_vedirect_get_data() et affiche PV tension, puissance, etat MPPT,
 * yield, et infos chargeur. Si VE.Direct non connecte : "Pas de chargeur detecte".
 */

#include "bmu_ui.h"
#include "bmu_vedirect.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "UI_SOLAR";

/* ── Couleurs ─────────────────────────────────────────────────────── */

#define COL_BG          lv_color_hex(0x1E1E1E)
#define COL_CARD        lv_color_hex(0x2D2D2D)
#define COL_BLUE        lv_color_hex(0x2979FF)
#define COL_GREEN       lv_color_hex(0x00C853)
#define COL_ORANGE      lv_color_hex(0xFF9100)
#define COL_YELLOW      lv_color_hex(0xFFD600)
#define COL_RED         lv_color_hex(0xFF1744)
#define COL_GREY        lv_color_hex(0x9E9E9E)
#define COL_WHITE       lv_color_white()

/* ── Labels statiques ─────────────────────────────────────────────── */

static lv_obj_t *s_no_charger_label = NULL;
static lv_obj_t *s_data_container = NULL;

// Donnees PV
static lv_obj_t *s_pv_voltage = NULL;
static lv_obj_t *s_pv_power = NULL;
static lv_obj_t *s_charge_current = NULL;
static lv_obj_t *s_batt_voltage = NULL;

// Etat MPPT
static lv_obj_t *s_charge_state = NULL;

// Yield
static lv_obj_t *s_yield_today = NULL;
static lv_obj_t *s_yield_total = NULL;
static lv_obj_t *s_max_power = NULL;

// Infos chargeur
static lv_obj_t *s_product_id = NULL;
static lv_obj_t *s_serial = NULL;
static lv_obj_t *s_firmware = NULL;

/* ── Helpers ──────────────────────────────────────────────────────── */

static lv_color_t cs_color(uint8_t cs)
{
    switch (cs) {
        case 0:  return COL_GREY;    // Off
        case 3:  return COL_ORANGE;  // Bulk
        case 4:  return COL_YELLOW;  // Absorption
        case 5:  return COL_GREEN;   // Float
        case 7:  return COL_BLUE;    // Equalize
        default: return COL_GREY;
    }
}

static lv_obj_t *make_row(lv_obj_t *parent, const char *label_text, int y, lv_obj_t **value_out)
{
    lv_obj_t *name = lv_label_create(parent);
    lv_label_set_text(name, label_text);
    lv_obj_set_style_text_color(name, COL_GREY, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 4, y);

    *value_out = lv_label_create(parent);
    lv_label_set_text(*value_out, "---");
    lv_obj_set_style_text_color(*value_out, COL_WHITE, 0);
    lv_obj_set_style_text_font(*value_out, &lv_font_montserrat_14, 0);
    lv_obj_align(*value_out, LV_ALIGN_TOP_LEFT, 130, y);

    return name;
}

/* ── Create ───────────────────────────────────────────────────────── */

void bmu_ui_solar_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, COL_BG, 0);

    /* Titre */
    lv_obj_t *header = lv_label_create(parent);
    lv_label_set_text(header, LV_SYMBOL_CHARGE " Solar VE.Direct");
    lv_obj_set_style_text_color(header, COL_BLUE, 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 4, 2);

    /* Message "pas de chargeur" (cache par defaut si connecte) */
    s_no_charger_label = lv_label_create(parent);
    lv_label_set_text(s_no_charger_label, "Pas de chargeur detecte");
    lv_obj_set_style_text_color(s_no_charger_label, COL_ORANGE, 0);
    lv_obj_set_style_text_font(s_no_charger_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_no_charger_label, LV_ALIGN_CENTER, 0, 0);

    /* Conteneur donnees (cache si pas de chargeur) */
    s_data_container = lv_obj_create(parent);
    lv_obj_set_size(s_data_container, LV_PCT(100), LV_PCT(90));
    lv_obj_align(s_data_container, LV_ALIGN_TOP_LEFT, 0, 20);
    lv_obj_set_style_bg_opa(s_data_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_data_container, 0, 0);
    lv_obj_set_style_pad_all(s_data_container, 0, 0);

    /* Colonne gauche : donnees PV */
    int y = 0;
    make_row(s_data_container, "PV tension:",  y,      &s_pv_voltage);     y += 18;
    make_row(s_data_container, "PV puiss.:",   y,      &s_pv_power);       y += 18;
    make_row(s_data_container, "I charge:",    y,      &s_charge_current); y += 18;
    make_row(s_data_container, "V batterie:",  y,      &s_batt_voltage);   y += 18;
    make_row(s_data_container, "Etat MPPT:",   y,      &s_charge_state);   y += 18;

    /* Separateur visuel */
    y += 4;

    /* Yield */
    make_row(s_data_container, "Yield jour:",  y,      &s_yield_today);    y += 18;
    make_row(s_data_container, "Yield total:", y,      &s_yield_total);    y += 18;
    make_row(s_data_container, "Pmax jour:",   y,      &s_max_power);      y += 18;

    /* Infos chargeur */
    y += 4;
    make_row(s_data_container, "Product ID:",  y,      &s_product_id);     y += 18;
    make_row(s_data_container, "Serial:",      y,      &s_serial);         y += 18;
    make_row(s_data_container, "Firmware:",    y,      &s_firmware);

    ESP_LOGI(TAG, "Ecran Solar cree");
}

/* ── Update ───────────────────────────────────────────────────────── */

void bmu_ui_solar_update(void)
{
    bool connected = bmu_vedirect_is_connected();

    if (!connected) {
        lv_obj_remove_flag(s_no_charger_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_data_container, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_add_flag(s_no_charger_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_data_container, LV_OBJ_FLAG_HIDDEN);

    const bmu_vedirect_data_t *d = bmu_vedirect_get_data();
    if (d == NULL || !d->valid) return;

    char buf[32];

    snprintf(buf, sizeof(buf), "%.1f V", d->panel_voltage_v);
    lv_label_set_text(s_pv_voltage, buf);

    snprintf(buf, sizeof(buf), "%d W", d->panel_power_w);
    lv_label_set_text(s_pv_power, buf);

    snprintf(buf, sizeof(buf), "%.1f A", d->battery_current_a);
    lv_label_set_text(s_charge_current, buf);

    snprintf(buf, sizeof(buf), "%.2f V", d->battery_voltage_v);
    lv_label_set_text(s_batt_voltage, buf);

    /* Etat charge avec couleur */
    const char *cs_name = bmu_vedirect_cs_name(d->charge_state);
    lv_label_set_text(s_charge_state, cs_name);
    lv_obj_set_style_text_color(s_charge_state, cs_color(d->charge_state), 0);

    /* Yield */
    snprintf(buf, sizeof(buf), "%lu Wh", (unsigned long)d->yield_today_wh);
    lv_label_set_text(s_yield_today, buf);

    snprintf(buf, sizeof(buf), "%.2f kWh", d->yield_total_wh / 1000.0f);
    lv_label_set_text(s_yield_total, buf);

    snprintf(buf, sizeof(buf), "%d W", d->max_power_today_w);
    lv_label_set_text(s_max_power, buf);

    /* Infos chargeur */
    lv_label_set_text(s_product_id, d->product_id);
    lv_label_set_text(s_serial, d->serial);
    lv_label_set_text(s_firmware, d->firmware);
}
