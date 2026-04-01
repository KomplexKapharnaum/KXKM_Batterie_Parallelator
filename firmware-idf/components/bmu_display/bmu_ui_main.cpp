#include "bmu_ui.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_wifi.h"
#include "bmu_mqtt.h"
#include "bmu_climate.h"
#include "bmu_ble.h"
#include "bmu_config.h"
#include "esp_log.h"
#include <cstdio>

static const char *TAG = "UI_MAIN";

/* Top bar */
static lv_obj_t *s_device_name_label = NULL;
static lv_obj_t *s_ble_dot  = NULL;
static lv_obj_t *s_wifi_dot = NULL;
static lv_obj_t *s_mqtt_dot = NULL;
static lv_obj_t *s_vmoy_label    = NULL;
static lv_obj_t *s_itot_label    = NULL;
static lv_obj_t *s_ahin_label    = NULL;
static lv_obj_t *s_ahout_label   = NULL;
static lv_obj_t *s_climate_label = NULL;

/* Battery list */
static lv_obj_t *s_bat_rows[16]    = {};
static lv_obj_t *s_bat_bars[16]    = {};
static lv_obj_t *s_bat_vlabels[16] = {};
static lv_obj_t *s_bat_ilabels[16] = {};
static lv_obj_t *s_bat_borders[16] = {};

static lv_obj_t *s_grid_parent = NULL;
static bmu_ui_ctx_t *s_ctx_ref = NULL;
static bmu_nav_state_t *s_nav  = NULL;

/* ── Callback tap sur cellule ─────────────────────────────────────── */

static void cell_clicked_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_nav == NULL || s_ctx_ref == NULL || s_grid_parent == NULL) return;
    if (s_nav->detail_visible) return;
    ESP_LOGI(TAG, "Tap sur BAT %d → detail", idx + 1);
    s_nav->detail_visible = true;
    s_nav->detail_battery = idx;
    bmu_ui_detail_create(s_grid_parent, s_ctx_ref, idx);
}

void bmu_ui_main_set_nav_state(bmu_nav_state_t *nav)
{
    s_nav = nav;
}

void bmu_ui_main_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx)
{
    s_grid_parent = parent;
    s_ctx_ref = ctx;

    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);

    /* ── Top bar ligne 1 : nom appareil + dots connexion ───────────── */

    s_device_name_label = lv_label_create(parent);
    lv_label_set_text_fmt(s_device_name_label, LV_SYMBOL_CHARGE " %s", bmu_config_get_device_name());
    lv_obj_set_style_text_color(s_device_name_label, UI_COLOR_INFO, 0);
    lv_obj_set_style_text_font(s_device_name_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_device_name_label, LV_ALIGN_TOP_LEFT, 4, 2);

    /* Dot BLE */
    s_ble_dot = lv_obj_create(parent);
    lv_obj_set_size(s_ble_dot, 8, 8);
    lv_obj_set_style_radius(s_ble_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ble_dot, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_border_width(s_ble_dot, 0, 0);
    lv_obj_align(s_ble_dot, LV_ALIGN_TOP_RIGHT, -40, 4);

    /* Dot WiFi */
    s_wifi_dot = lv_obj_create(parent);
    lv_obj_set_size(s_wifi_dot, 8, 8);
    lv_obj_set_style_radius(s_wifi_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_wifi_dot, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_border_width(s_wifi_dot, 0, 0);
    lv_obj_align(s_wifi_dot, LV_ALIGN_TOP_RIGHT, -24, 4);

    /* Dot MQTT */
    s_mqtt_dot = lv_obj_create(parent);
    lv_obj_set_size(s_mqtt_dot, 8, 8);
    lv_obj_set_style_radius(s_mqtt_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_mqtt_dot, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_border_width(s_mqtt_dot, 0, 0);
    lv_obj_align(s_mqtt_dot, LV_ALIGN_TOP_RIGHT, -8, 4);

    /* ── Top bar ligne 2 : 5 tuiles stats en flex row ───────────────── */

    lv_obj_t *stats_row = lv_obj_create(parent);
    lv_obj_set_size(stats_row, 312, 28);
    lv_obj_align(stats_row, LV_ALIGN_TOP_LEFT, 0, 16);
    lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(stats_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stats_row, 0, 0);
    lv_obj_set_style_pad_all(stats_row, 0, 0);

    /* Tuile V MOY */
    lv_obj_t *vmoy_tile = lv_obj_create(stats_row);
    lv_obj_set_style_bg_color(vmoy_tile, UI_COLOR_CARD, 0);
    lv_obj_set_style_border_width(vmoy_tile, 0, 0);
    lv_obj_set_style_pad_all(vmoy_tile, 2, 0);
    lv_obj_set_style_radius(vmoy_tile, 2, 0);
    lv_obj_set_size(vmoy_tile, 56, 24);

    lv_obj_t *vmoy_key = lv_label_create(vmoy_tile);
    lv_label_set_text(vmoy_key, "V MOY");
    lv_obj_set_style_text_color(vmoy_key, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(vmoy_key, &lv_font_montserrat_14, 0);
    lv_obj_align(vmoy_key, LV_ALIGN_TOP_LEFT, 0, 0);

    s_vmoy_label = lv_label_create(vmoy_tile);
    lv_label_set_text(s_vmoy_label, "--.-V");
    lv_obj_set_style_text_color(s_vmoy_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_vmoy_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_vmoy_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Tuile I TOTAL */
    lv_obj_t *itot_tile = lv_obj_create(stats_row);
    lv_obj_set_style_bg_color(itot_tile, UI_COLOR_CARD, 0);
    lv_obj_set_style_border_width(itot_tile, 0, 0);
    lv_obj_set_style_pad_all(itot_tile, 2, 0);
    lv_obj_set_style_radius(itot_tile, 2, 0);
    lv_obj_set_size(itot_tile, 56, 24);

    lv_obj_t *itot_key = lv_label_create(itot_tile);
    lv_label_set_text(itot_key, "I TOT");
    lv_obj_set_style_text_color(itot_key, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(itot_key, &lv_font_montserrat_14, 0);
    lv_obj_align(itot_key, LV_ALIGN_TOP_LEFT, 0, 0);

    s_itot_label = lv_label_create(itot_tile);
    lv_label_set_text(s_itot_label, "--.-A");
    lv_obj_set_style_text_color(s_itot_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_itot_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_itot_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Tuile Ah IN */
    lv_obj_t *ahin_tile = lv_obj_create(stats_row);
    lv_obj_set_style_bg_color(ahin_tile, UI_COLOR_CARD, 0);
    lv_obj_set_style_border_width(ahin_tile, 0, 0);
    lv_obj_set_style_pad_all(ahin_tile, 2, 0);
    lv_obj_set_style_radius(ahin_tile, 2, 0);
    lv_obj_set_size(ahin_tile, 56, 24);

    lv_obj_t *ahin_key = lv_label_create(ahin_tile);
    lv_label_set_text(ahin_key, "Ah IN");
    lv_obj_set_style_text_color(ahin_key, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(ahin_key, &lv_font_montserrat_14, 0);
    lv_obj_align(ahin_key, LV_ALIGN_TOP_LEFT, 0, 0);

    s_ahin_label = lv_label_create(ahin_tile);
    lv_label_set_text(s_ahin_label, "---");
    lv_obj_set_style_text_color(s_ahin_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_ahin_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_ahin_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Tuile Ah OUT */
    lv_obj_t *ahout_tile = lv_obj_create(stats_row);
    lv_obj_set_style_bg_color(ahout_tile, UI_COLOR_CARD, 0);
    lv_obj_set_style_border_width(ahout_tile, 0, 0);
    lv_obj_set_style_pad_all(ahout_tile, 2, 0);
    lv_obj_set_style_radius(ahout_tile, 2, 0);
    lv_obj_set_size(ahout_tile, 56, 24);

    lv_obj_t *ahout_key = lv_label_create(ahout_tile);
    lv_label_set_text(ahout_key, "Ah OUT");
    lv_obj_set_style_text_color(ahout_key, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(ahout_key, &lv_font_montserrat_14, 0);
    lv_obj_align(ahout_key, LV_ALIGN_TOP_LEFT, 0, 0);

    s_ahout_label = lv_label_create(ahout_tile);
    lv_label_set_text(s_ahout_label, "---");
    lv_obj_set_style_text_color(s_ahout_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_ahout_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_ahout_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Tuile CLIMAT */
    lv_obj_t *clim_tile = lv_obj_create(stats_row);
    lv_obj_set_style_bg_color(clim_tile, UI_COLOR_CARD, 0);
    lv_obj_set_style_border_width(clim_tile, 0, 0);
    lv_obj_set_style_pad_all(clim_tile, 2, 0);
    lv_obj_set_style_radius(clim_tile, 2, 0);
    lv_obj_set_size(clim_tile, 60, 24);

    lv_obj_t *clim_key = lv_label_create(clim_tile);
    lv_label_set_text(clim_key, "CLIMAT");
    lv_obj_set_style_text_color(clim_key, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(clim_key, &lv_font_montserrat_14, 0);
    lv_obj_align(clim_key, LV_ALIGN_TOP_LEFT, 0, 0);

    s_climate_label = lv_label_create(clim_tile);
    lv_label_set_text(s_climate_label, "---");
    lv_obj_set_style_text_color(s_climate_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_climate_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_climate_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* ── Liste batteries scrollable ─────────────────────────────────── */

    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_set_size(list, 312, 160);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_row(list, 2, 0);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    int nb = ctx->nb_ina > 16 ? 16 : ctx->nb_ina;
    for (int i = 0; i < nb; i++) {
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_set_size(row, 300, 18);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, UI_COLOR_BG, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 4, 0);

        /* Bord gauche : indicateur couleur 3px */
        lv_obj_t *border = lv_obj_create(row);
        lv_obj_set_size(border, 3, 16);
        lv_obj_set_style_bg_color(border, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_border_width(border, 0, 0);
        lv_obj_set_style_radius(border, 0, 0);
        s_bat_borders[i] = border;

        /* Nom batterie */
        lv_obj_t *name = lv_label_create(row);
        char buf[8];
        snprintf(buf, sizeof(buf), "B%d", i + 1);
        lv_label_set_text(name, buf);
        lv_obj_set_style_text_color(name, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_width(name, 24);

        /* Barre de progression tension */
        lv_obj_t *bar = lv_bar_create(row);
        lv_obj_set_size(bar, 160, 12);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, UI_COLOR_CARD, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, UI_COLOR_OK, LV_PART_INDICATOR);
        s_bat_bars[i] = bar;

        /* Label tension */
        lv_obj_t *vlbl = lv_label_create(row);
        lv_label_set_text(vlbl, "--.-V");
        lv_obj_set_style_text_color(vlbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(vlbl, &lv_font_montserrat_14, 0);
        lv_obj_set_width(vlbl, 48);
        s_bat_vlabels[i] = vlbl;

        /* Label courant */
        lv_obj_t *ilbl = lv_label_create(row);
        lv_label_set_text(ilbl, "-.-A");
        lv_obj_set_style_text_color(ilbl, UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_text_font(ilbl, &lv_font_montserrat_14, 0);
        lv_obj_set_width(ilbl, 40);
        s_bat_ilabels[i] = ilbl;

        /* Ligne cliquable */
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, cell_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_bat_rows[i] = row;
    }
}

void bmu_ui_main_update(bmu_ui_ctx_t *ctx)
{
    /* Si le detail est visible, mettre a jour le detail, pas la liste */
    if (s_nav != NULL && s_nav->detail_visible) {
        bmu_ui_detail_update(ctx, s_nav->detail_battery);
        return;
    }

    int nb = ctx->nb_ina > 16 ? 16 : ctx->nb_ina;
    float sum_v = 0, sum_i = 0, sum_ah_c = 0, sum_ah_d = 0;
    int n_active = 0;

    for (int i = 0; i < nb; i++) {
        float v_mv = bmu_protection_get_voltage(ctx->prot, i);
        float v = v_mv / 1000.0f;
        bmu_battery_state_t state = bmu_protection_get_state(ctx->prot, i);

        /* Courant : dernier point de l'historique graphique */
        bmu_chart_history_t *h = &ctx->chart_hist[i];
        float i_a = 0.0f;
        if (h->count > 0) {
            int last = (h->head - 1 + CONFIG_BMU_CHART_HISTORY_POINTS) % CONFIG_BMU_CHART_HISTORY_POINTS;
            i_a = h->current_a[last];
        }

        /* Labels tension et courant */
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1fV", v);
        lv_label_set_text(s_bat_vlabels[i], buf);
        snprintf(buf, sizeof(buf), "%.1fA", i_a);
        lv_label_set_text(s_bat_ilabels[i], buf);

        /* Barre : map tension → 0-100% */
        int pct = (int)((v_mv - 24000.0f) / 6000.0f * 100.0f);
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;
        lv_bar_set_value(s_bat_bars[i], pct, LV_ANIM_OFF);

        /* Couleur barre + bord selon etat */
        lv_color_t col;
        if (state == BMU_STATE_CONNECTED) {
            col = UI_COLOR_OK;
        } else if (state == BMU_STATE_DISCONNECTED || state == BMU_STATE_LOCKED) {
            col = UI_COLOR_ERR;
        } else {
            col = UI_COLOR_WARN;
        }
        lv_obj_set_style_bg_color(s_bat_bars[i], col, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_bat_borders[i], col, 0);

        /* Fond ligne : rouge sombre pour batteries OFF */
        if (state == BMU_STATE_DISCONNECTED || state == BMU_STATE_ERROR || state == BMU_STATE_LOCKED) {
            lv_obj_set_style_bg_color(s_bat_rows[i], UI_COLOR_BG_ERR, 0);
            lv_obj_set_style_bg_opa(s_bat_rows[i], LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_opa(s_bat_rows[i], LV_OPA_TRANSP, 0);
        }

        /* Accumulation stats */
        sum_i += i_a;
        if (state == BMU_STATE_CONNECTED && v > 1.0f) {
            sum_v += v;
            n_active++;
        }
        sum_ah_c += bmu_battery_manager_get_ah_charge(ctx->mgr, i);
        sum_ah_d += bmu_battery_manager_get_ah_discharge(ctx->mgr, i);
    }

    /* Mise a jour barre stats */
    char buf[24];
    float avg_v = n_active > 0 ? sum_v / n_active : 0;
    snprintf(buf, sizeof(buf), "%.1fV", avg_v);
    lv_label_set_text(s_vmoy_label, buf);
    lv_obj_set_style_text_color(s_vmoy_label,
        (avg_v >= 24.0f && avg_v <= 30.0f) ? UI_COLOR_OK : UI_COLOR_WARN, 0);

    snprintf(buf, sizeof(buf), "%.1fA", sum_i);
    lv_label_set_text(s_itot_label, buf);

    snprintf(buf, sizeof(buf), "%.1f", sum_ah_c);
    lv_label_set_text(s_ahin_label, buf);

    snprintf(buf, sizeof(buf), "%.1f", sum_ah_d);
    lv_label_set_text(s_ahout_label, buf);

    /* Climat */
    if (bmu_climate_is_available()) {
        snprintf(buf, sizeof(buf), "%.0fC %.0f%%",
                 bmu_climate_get_temperature(), bmu_climate_get_humidity());
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(s_climate_label, buf);

    /* Dots connexion */
    lv_obj_set_style_bg_color(s_ble_dot,  bmu_ble_is_connected()  ? UI_COLOR_INFO : UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_bg_color(s_wifi_dot, bmu_wifi_is_connected()  ? UI_COLOR_OK   : UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_bg_color(s_mqtt_dot, bmu_mqtt_is_connected()  ? UI_COLOR_OK   : UI_COLOR_WARN,     0);
}
