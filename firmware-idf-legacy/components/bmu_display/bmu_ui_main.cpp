#include "bmu_ui.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_wifi.h"
#include "bmu_mqtt.h"
#include "bmu_climate.h"
#include "bmu_ble.h"
#include "bmu_config.h"
#include "bmu_balancer.h"
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
/* CLIMAT supprimé de la stats bar — affiché sur la ligne PACK */

/* Pack info line */
static lv_obj_t *s_pack_vmin_label  = NULL;
static lv_obj_t *s_pack_vmax_label  = NULL;
static lv_obj_t *s_pack_vmoy_label  = NULL;
static lv_obj_t *s_pack_temp_label  = NULL;

/* Battery list */
static lv_obj_t *s_bat_rows[32]    = {};
static lv_obj_t *s_bat_bars[32]    = {};
static lv_obj_t *s_bat_vlabels[32] = {};
static lv_obj_t *s_bat_ilabels[32] = {};
static lv_obj_t *s_bat_borders[32] = {};
static lv_obj_t *s_bat_list = NULL;
static int s_bat_created = 0; /* number of rows already created */

static lv_obj_t *s_grid_parent = NULL;
static bmu_ui_ctx_t *s_ctx_ref = NULL;
static bmu_nav_state_t *s_nav  = NULL;

static void load_runtime_voltage_window(float *min_mv_out, float *max_mv_out)
{
    uint16_t min_mv = BMU_MIN_VOLTAGE_MV;
    uint16_t max_mv = BMU_MAX_VOLTAGE_MV;
    uint16_t max_ma = BMU_MAX_CURRENT_MA;
    uint16_t diff_mv = BMU_VOLTAGE_DIFF_MV;

    bmu_config_get_thresholds(&min_mv, &max_mv, &max_ma, &diff_mv);

    if (max_mv <= min_mv) {
        min_mv = BMU_MIN_VOLTAGE_MV;
        max_mv = BMU_MAX_VOLTAGE_MV;
    }

    if (min_mv_out != NULL) *min_mv_out = (float)min_mv;
    if (max_mv_out != NULL) *max_mv_out = (float)max_mv;
}

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

    /* ── Top bar ligne 2 : 4 tuiles stats en flex row ───────────────── */

    lv_obj_t *stats_row = lv_obj_create(parent);
    lv_obj_set_size(stats_row, 320, 18);
    lv_obj_align(stats_row, LV_ALIGN_TOP_LEFT, 0, 18);
    lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(stats_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stats_row, 0, 0);
    lv_obj_set_style_pad_all(stats_row, 0, 0);
    lv_obj_clear_flag(stats_row, LV_OBJ_FLAG_SCROLLABLE);

    /* 4 labels compacts : "V 27.15" "I 3.2A" "+12.3" "-8.1" */
    auto make_stat = [](lv_obj_t *row, const char *key, lv_obj_t **val_label) {
        lv_obj_t *klbl = lv_label_create(row);
        lv_label_set_text(klbl, key);
        lv_obj_set_style_text_color(klbl, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(klbl, &lv_font_montserrat_14, 0);

        *val_label = lv_label_create(row);
        lv_label_set_text(*val_label, "---");
        lv_obj_set_style_text_color(*val_label, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(*val_label, &lv_font_montserrat_14, 0);
    };

    make_stat(stats_row, "V",  &s_vmoy_label);
    make_stat(stats_row, "I",  &s_itot_label);
    make_stat(stats_row, "+",  &s_ahin_label);
    make_stat(stats_row, "-",  &s_ahout_label);

    /* ── Ligne PACK : Vmin / Vmoy / Vmax + T°C/H% ────────────────────── */

    lv_obj_t *pack_row = lv_obj_create(parent);
    lv_obj_set_size(pack_row, 320, 18);
    lv_obj_align(pack_row, LV_ALIGN_TOP_LEFT, 0, 38);
    lv_obj_set_flex_flow(pack_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pack_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(pack_row, UI_COLOR_CARD, 0);
    lv_obj_set_style_border_width(pack_row, 0, 0);
    lv_obj_set_style_pad_all(pack_row, 0, 0);
    lv_obj_set_style_radius(pack_row, 0, 0);
    lv_obj_clear_flag(pack_row, LV_OBJ_FLAG_SCROLLABLE);

    s_pack_vmin_label = lv_label_create(pack_row);
    lv_label_set_text(s_pack_vmin_label, "--.-");
    lv_obj_set_style_text_color(s_pack_vmin_label, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_font(s_pack_vmin_label, &lv_font_montserrat_14, 0);

    s_pack_vmoy_label = lv_label_create(pack_row);
    lv_label_set_text(s_pack_vmoy_label, "--.-V");
    lv_obj_set_style_text_color(s_pack_vmoy_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_pack_vmoy_label, &lv_font_montserrat_14, 0);

    s_pack_vmax_label = lv_label_create(pack_row);
    lv_label_set_text(s_pack_vmax_label, "--.-");
    lv_obj_set_style_text_color(s_pack_vmax_label, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_font(s_pack_vmax_label, &lv_font_montserrat_14, 0);

    s_pack_temp_label = lv_label_create(pack_row);
    lv_label_set_text(s_pack_temp_label, "---");
    lv_obj_set_style_text_color(s_pack_temp_label, UI_COLOR_INFO, 0);
    lv_obj_set_style_text_font(s_pack_temp_label, &lv_font_montserrat_14, 0);

    /* ── Liste batteries scrollable (décalée d'une ligne) ───────────── */

    s_bat_list = lv_obj_create(parent);
    lv_obj_set_size(s_bat_list, 320, 148);
    lv_obj_align(s_bat_list, LV_ALIGN_TOP_LEFT, 0, 58);
    lv_obj_set_flex_flow(s_bat_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(s_bat_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_bat_list, 0, 0);
    lv_obj_set_style_pad_row(s_bat_list, 2, 0);
    lv_obj_set_style_pad_all(s_bat_list, 0, 0);
    lv_obj_set_scroll_dir(s_bat_list, LV_DIR_VER); /* vertical seulement */

    s_bat_created = 0;
    /* Rows crees progressivement dans update() — 1 par appel timer max */
}

/* ── Create battery rows on demand ────────────────────────────────── */

static void ensure_battery_rows(int nb)
{
    if (nb <= s_bat_created || s_bat_list == NULL) return;
    if (nb > 32) nb = 32;

    /* Creer jusqu'a 4 rows par tick. Avec LVGL heap 64KB et esp_timer
     * task, 4 rows (~2-3ms chacune) = ~10ms par tick, sans impact watchdog. */
    int target = s_bat_created + 4;
    if (target > nb) target = nb;

    for (int i = s_bat_created; i < target; i++) {
        lv_obj_t *row = lv_obj_create(s_bat_list);
        lv_obj_set_size(row, 316, 18);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, UI_COLOR_BG, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 4, 0);

        lv_obj_t *border = lv_obj_create(row);
        lv_obj_set_size(border, 3, 16);
        lv_obj_set_style_bg_color(border, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_border_width(border, 0, 0);
        lv_obj_set_style_radius(border, 0, 0);
        s_bat_borders[i] = border;

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, bmu_config_get_battery_label(i));
        lv_obj_set_style_text_color(name, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_width(name, 48);

        lv_obj_t *bar = lv_bar_create(row);
        lv_obj_set_size(bar, 136, 12);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, UI_COLOR_CARD, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, UI_COLOR_OK, LV_PART_INDICATOR);
        s_bat_bars[i] = bar;

        lv_obj_t *vlbl = lv_label_create(row);
        lv_label_set_text(vlbl, "0.00V");
        lv_obj_set_style_text_color(vlbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(vlbl, &lv_font_montserrat_14, 0);
        lv_obj_set_width(vlbl, 48);
        s_bat_vlabels[i] = vlbl;

        lv_obj_t *ilbl = lv_label_create(row);
        lv_label_set_text(ilbl, "-.-A");
        lv_obj_set_style_text_color(ilbl, UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_text_font(ilbl, &lv_font_montserrat_14, 0);
        lv_obj_set_width(ilbl, 40);
        s_bat_ilabels[i] = ilbl;

        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, cell_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_bat_rows[i] = row;
    }
    s_bat_created = target;
}

void bmu_ui_main_update(bmu_ui_ctx_t *ctx)
{
    /* Si le detail est visible, mettre a jour le detail, pas la liste */
    if (s_nav != NULL && s_nav->detail_visible) {
        bmu_ui_detail_update(ctx, s_nav->detail_battery);
        return;
    }

    int nb = ctx->nb_ina > 32 ? 32 : ctx->nb_ina;
    float min_voltage_mv = (float)BMU_MIN_VOLTAGE_MV;
    float max_voltage_mv = (float)BMU_MAX_VOLTAGE_MV;
    load_runtime_voltage_window(&min_voltage_mv, &max_voltage_mv);
    const float voltage_span_mv = (max_voltage_mv > min_voltage_mv)
                                      ? (max_voltage_mv - min_voltage_mv)
                                      : 1.0f;

    /* Create battery rows lazily when nb_ina becomes known */
    ensure_battery_rows(nb);

    float sum_v = 0, sum_i = 0, sum_ah_c = 0, sum_ah_d = 0;
    float v_min = 999999.0f, v_max = 0.0f;
    int n_active = 0;

    for (int i = 0; i < nb; i++) {
        float v_mv = bmu_protection_get_voltage(ctx->prot, i);
        float v = v_mv / 1000.0f;
        bmu_battery_state_t state = bmu_protection_get_state(ctx->prot, i);

        /* Courant : cache temps-reel de la protection task */
        float i_a = bmu_protection_get_current(ctx->prot, i);

        /* Labels tension et courant — skip si row pas encore creee */
        if (s_bat_vlabels[i] == NULL) continue;

        char buf[16];
        snprintf(buf, sizeof(buf), "%.2fV", v);
        lv_label_set_text(s_bat_vlabels[i], buf);
        snprintf(buf, sizeof(buf), "%.1fA", i_a);
        lv_label_set_text(s_bat_ilabels[i], buf);

        /* Barre : map tension → 0-100% */
        int pct = (int)(((v_mv - min_voltage_mv) / voltage_span_mv) * 100.0f);
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;
        lv_bar_set_value(s_bat_bars[i], pct, LV_ANIM_OFF);

        /* Couleur barre + bord selon etat */
        lv_color_t col;
        if (state == BMU_STATE_CONNECTED) {
            col = UI_COLOR_OK;
        } else if (state == BMU_STATE_DISCONNECTED ||
                   state == BMU_STATE_ERROR ||
                   state == BMU_STATE_LOCKED) {
            col = UI_COLOR_ERR;
        } else {
            col = UI_COLOR_WARN;
        }
        /* Indicateur balancing : orange si duty-cycled OFF */
        if (bmu_balancer_is_off((uint8_t)i)) {
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
            if (v < v_min) v_min = v;
            if (v > v_max) v_max = v;
            n_active++;
        }
        sum_ah_c += bmu_battery_manager_get_ah_charge(ctx->mgr, i);
        sum_ah_d += bmu_battery_manager_get_ah_discharge(ctx->mgr, i);
    }

    /* Mise a jour barre stats */
    char buf[24];
    float avg_v = n_active > 0 ? sum_v / n_active : 0;
    snprintf(buf, sizeof(buf), "%.2fV", avg_v);
    lv_label_set_text(s_vmoy_label, buf);
    lv_obj_set_style_text_color(s_vmoy_label,
        (avg_v * 1000.0f >= min_voltage_mv && avg_v * 1000.0f <= max_voltage_mv)
            ? UI_COLOR_OK
            : UI_COLOR_WARN,
        0);

    snprintf(buf, sizeof(buf), "%.1fA", sum_i);
    lv_label_set_text(s_itot_label, buf);

    snprintf(buf, sizeof(buf), "%.1f", sum_ah_c);
    lv_label_set_text(s_ahin_label, buf);

    snprintf(buf, sizeof(buf), "%.1f", sum_ah_d);
    lv_label_set_text(s_ahout_label, buf);

    /* ── Ligne PACK : Vmin / Vmoy / Vmax + T°C H% ─────────────────── */
    if (n_active > 0) {
        snprintf(buf, sizeof(buf), "%.2f", v_min);
        lv_label_set_text(s_pack_vmin_label, buf);
        snprintf(buf, sizeof(buf), "%.2fV", avg_v);
        lv_label_set_text(s_pack_vmoy_label, buf);
        snprintf(buf, sizeof(buf), "%.2f", v_max);
        lv_label_set_text(s_pack_vmax_label, buf);

        /* Couleur déséquilibre */
        float delta = (v_max - v_min) * 1000.0f; /* mV */
        lv_color_t dcol = (delta > 1000.0f) ? UI_COLOR_ERR : (delta > 500.0f) ? UI_COLOR_WARN : UI_COLOR_OK;
        lv_obj_set_style_text_color(s_pack_vmin_label, dcol, 0);
        lv_obj_set_style_text_color(s_pack_vmax_label, dcol, 0);
    } else {
        lv_label_set_text(s_pack_vmin_label, "--.-");
        lv_label_set_text(s_pack_vmoy_label, "--.-V");
        lv_label_set_text(s_pack_vmax_label, "--.-");
    }

    if (bmu_climate_is_available()) {
        snprintf(buf, sizeof(buf), "%.1fC %.0f%%",
                 bmu_climate_get_temperature(), bmu_climate_get_humidity());
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(s_pack_temp_label, buf);

    /* Dots connexion */
    lv_obj_set_style_bg_color(s_ble_dot,  bmu_ble_is_connected()  ? UI_COLOR_INFO : UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_bg_color(s_wifi_dot, bmu_wifi_is_connected()  ? UI_COLOR_OK   : UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_bg_color(s_mqtt_dot, bmu_mqtt_is_connected()  ? UI_COLOR_OK   : UI_COLOR_WARN,     0);
}
