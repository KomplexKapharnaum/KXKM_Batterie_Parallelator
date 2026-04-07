/**
 * @file bmu_ui_detail.cpp
 * @brief Ecran detail batterie — graphique V/I + valeurs + boutons actions.
 *
 * Cree dynamiquement au tap sur une cellule, detruit au retour.
 * Utilise lv_chart pour l'historique temps reel.
 */

#include "bmu_ui.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_climate.h"
#include "esp_log.h"
#include <cstdio>

static const char *TAG = "UI_DETAIL";

/* ── State statique ───────────────────────────────────────────────── */

static lv_obj_t *s_panel = NULL;          // panneau overlay principal
static lv_obj_t *s_chart = NULL;          // lv_chart widget
static lv_chart_series_t *s_ser_v = NULL; // serie tension
static lv_chart_series_t *s_ser_i = NULL; // serie courant
static lv_obj_t *s_val_labels[8] = {};    // labels valeurs numeriques
static int s_battery_idx = -1;
static bmu_ui_ctx_t *s_ctx_ref = NULL;
static bmu_nav_state_t *s_nav_ref = NULL;
static bmu_protection_ctx_t *s_prot = NULL;
static bool s_switch_action = false;

static void load_runtime_voltage_window(int32_t *min_mv_out, int32_t *max_mv_out)
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

    if (min_mv_out != NULL) *min_mv_out = (int32_t)min_mv;
    if (max_mv_out != NULL) *max_mv_out = (int32_t)max_mv;
}

/* ── Noms d'etats ─────────────────────────────────────────────────── */

static const char *state_name(bmu_battery_state_t s)
{
    switch (s) {
        case BMU_STATE_CONNECTED:    return "CONNECTED";
        case BMU_STATE_DISCONNECTED: return "DISCONNECTED";
        case BMU_STATE_RECONNECTING: return "RECONNECTING";
        case BMU_STATE_ERROR:        return "ERROR";
        case BMU_STATE_LOCKED:       return "LOCKED";
        default:                     return "UNKNOWN";
    }
}

static lv_color_t state_color(bmu_battery_state_t s)
{
    switch (s) {
        case BMU_STATE_CONNECTED:    return UI_COLOR_OK;
        case BMU_STATE_DISCONNECTED: return UI_COLOR_ERR;
        case BMU_STATE_RECONNECTING: return UI_COLOR_WARN;
        case BMU_STATE_ERROR:        return UI_COLOR_ERR;
        case BMU_STATE_LOCKED:       return UI_COLOR_TEXT_DIM;
        default:                     return UI_COLOR_TEXT_DIM;
    }
}

/* ── Auto-scale graphique Y ───────────────────────────────────────── */

static void update_chart_range(bmu_chart_history_t *hist)
{
    if (hist->count == 0) return;
    float v_min = 35000, v_max = 15000;
    for (int i = 0; i < hist->count; i++) {
        int idx = (hist->head - hist->count + i + CONFIG_BMU_CHART_HISTORY_POINTS) % CONFIG_BMU_CHART_HISTORY_POINTS;
        float v = hist->voltage_mv[idx];
        if (v > 0 && v < v_min) v_min = v;
        if (v > v_max) v_max = v;
    }
    float margin = (v_max - v_min) * 0.1f;
    if (margin < 500) margin = 500;
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y,
                       (int)(v_min - margin), (int)(v_max + margin));
}

/* ── Dialog confirmation switch ──────────────────────────────────── */

static void switch_confirm_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *msgbox = lv_obj_get_parent(lv_obj_get_parent(btn)); /* btn -> footer -> msgbox */

    const char *txt = lv_label_get_text(lv_obj_get_child(btn, 0));
    if (txt && strcmp(txt, "Oui") == 0) {
        bmu_protection_web_switch(s_prot, s_battery_idx, s_switch_action);
        ESP_LOGI(TAG, "Switch bat[%d] -> %s (confirme)", s_battery_idx,
                 s_switch_action ? "ON" : "OFF");
    }
    lv_msgbox_close(msgbox);
}

static void show_switch_confirm(bool on)
{
    s_switch_action = on;
    s_prot = s_ctx_ref->prot;

    lv_obj_t *msgbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(msgbox, on ? "Switch ON" : "Switch OFF");

    char msg[64];
    snprintf(msg, sizeof(msg), "%s batterie %d ?",
             on ? "Connecter" : "Deconnecter", s_battery_idx + 1);
    lv_msgbox_add_text(msgbox, msg);
    lv_msgbox_add_close_button(msgbox);

    lv_obj_t *btn_yes = lv_msgbox_add_footer_button(msgbox, "Oui");
    lv_obj_t *btn_no  = lv_msgbox_add_footer_button(msgbox, "Non");
    lv_obj_add_event_cb(btn_yes, switch_confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_no,  switch_confirm_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_center(msgbox);
}

/* ── Callbacks boutons ────────────────────────────────────────────── */

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Retour a la grille depuis BAT %d", s_battery_idx + 1);
    bmu_ui_detail_destroy();
}

/* ── Swipe gauche/droite — differe via lv_async_call ──────────── */

static int s_swipe_next = -1;

static void swipe_async_cb(void *data)
{
    (void)data;
    if (s_ctx_ref == NULL || s_nav_ref == NULL || s_swipe_next < 0) return;
    int next = s_swipe_next;
    s_swipe_next = -1;

    lv_obj_t *parent = (s_panel != NULL) ? lv_obj_get_parent(s_panel) : NULL;
    bmu_ui_detail_destroy();
    if (parent != NULL) {
        s_nav_ref->detail_visible = true;
        s_nav_ref->detail_battery = next;
        bmu_ui_detail_create(parent, s_ctx_ref, next);
    }
}

static void swipe_cb(lv_event_t *e)
{
    (void)e;
    if (s_ctx_ref == NULL || s_nav_ref == NULL) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    int nb = s_ctx_ref->nb_ina;
    if (nb <= 1) return;

    if (dir == LV_DIR_LEFT) {
        s_swipe_next = (s_battery_idx + 1) % nb;
    } else if (dir == LV_DIR_RIGHT) {
        s_swipe_next = (s_battery_idx - 1 + nb) % nb;
    } else {
        return;
    }
    ESP_LOGI(TAG, "Swipe BAT %d -> BAT %d (async)", s_battery_idx + 1, s_swipe_next + 1);
    lv_async_call(swipe_async_cb, NULL);
}

static void switch_on_btn_cb(lv_event_t *e)  { (void)e; show_switch_confirm(true);  }
static void switch_off_btn_cb(lv_event_t *e) { (void)e; show_switch_confirm(false); }

static void reset_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_ctx_ref == NULL) return;
    ESP_LOGI(TAG, "Reset compteur BAT %d via display", s_battery_idx + 1);
    bmu_protection_reset_switch_count(s_ctx_ref->prot, s_battery_idx);
}

/* ── Set nav state (appele depuis bmu_display.cpp) ────────────────── */

void bmu_ui_detail_set_nav_state(bmu_nav_state_t *nav)
{
    s_nav_ref = nav;
}

/* ── Create ───────────────────────────────────────────────────────── */

void bmu_ui_detail_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx, int idx)
{
    if (idx < 0 || idx >= ctx->nb_ina) return;

    s_battery_idx = idx;
    s_ctx_ref = ctx;

    /* Panneau overlay couvrant tout le parent */
    s_panel = lv_obj_create(parent);
    lv_obj_set_size(s_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_panel, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 4, 0);
    lv_obj_set_style_radius(s_panel, 0, 0);
    lv_obj_move_foreground(s_panel);
    lv_obj_add_event_cb(s_panel, swipe_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_set_scroll_dir(s_panel, LV_DIR_NONE); /* empeche scroll, capture gesture */

    /* ── Ligne du haut : Back + titre ─────────────────────────────── */
    lv_obj_t *btn_back = lv_button_create(s_panel);
    lv_obj_set_size(btn_back, 56, 22);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_back, UI_COLOR_CARD, 0);
    lv_obj_t *btn_lbl = lv_label_create(btn_back);
    lv_label_set_text(btn_lbl, LV_SYMBOL_LEFT " Ret");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(btn_back, back_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *tlbl = lv_label_create(s_panel);
    lv_label_set_text(tlbl, bmu_config_get_battery_label(idx));
    lv_obj_set_style_text_color(tlbl, UI_COLOR_INFO, 0);
    lv_obj_set_style_text_font(tlbl, &lv_font_montserrat_14, 0);
    lv_obj_align(tlbl, LV_ALIGN_TOP_MID, 0, 2);

    /* ── Graphique V/I (moitie gauche) ────────────────────────────── */
    s_chart = lv_chart_create(s_panel);
    lv_obj_set_size(s_chart, 150, 90);
    lv_obj_align(s_chart, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, 60);  // affiche les 60 derniers points (30s)
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_color(s_chart, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_border_width(s_chart, 1, 0);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR); // pas de points

    lv_chart_set_div_line_count(s_chart, 3, 0);

    /* Serie tension (axe Y primaire) — bleu */
    s_ser_v = lv_chart_add_series(s_chart, UI_COLOR_INFO, LV_CHART_AXIS_PRIMARY_Y);
    int32_t min_mv = BMU_MIN_VOLTAGE_MV;
    int32_t max_mv = BMU_MAX_VOLTAGE_MV;
    load_runtime_voltage_window(&min_mv, &max_mv);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, min_mv - 1000, max_mv + 1000);

    /* Serie courant (axe Y secondaire) — vert */
    s_ser_i = lv_chart_add_series(s_chart, UI_COLOR_OK, LV_CHART_AXIS_SECONDARY_Y);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_SECONDARY_Y, -5000, 30000); // -5A..30A en mA

    /* ── Valeurs numeriques (moitie droite) ───────────────────────── */
    const char *labels[] = {
        "V:", "I:", "P:", "T:",
        "Ah+:", "Ah-:", "Coup:", "Etat:"
    };
    for (int i = 0; i < 8; i++) {
        lv_obj_t *name = lv_label_create(s_panel);
        lv_label_set_text(name, labels[i]);
        lv_obj_set_style_text_color(name, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 158, 26 + i * 16);

        s_val_labels[i] = lv_label_create(s_panel);
        lv_label_set_text(s_val_labels[i], "---");
        lv_obj_set_style_text_color(s_val_labels[i], UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(s_val_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_align(s_val_labels[i], LV_ALIGN_TOP_LEFT, 200, 26 + i * 16);
    }

    /* ── Boutons actions (bas de l'ecran) ─────────────────────────── */
    lv_obj_t *btn_on = lv_button_create(s_panel);
    lv_obj_set_size(btn_on, 80, 26);
    lv_obj_align(btn_on, LV_ALIGN_BOTTOM_LEFT, 4, -2);
    lv_obj_set_style_bg_color(btn_on, UI_COLOR_OK, 0);
    lv_obj_t *lbl_on = lv_label_create(btn_on);
    lv_label_set_text(lbl_on, "Switch ON");
    lv_obj_set_style_text_font(lbl_on, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_on, lv_color_black(), 0);
    lv_obj_center(lbl_on);
    lv_obj_add_event_cb(btn_on, switch_on_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_off = lv_button_create(s_panel);
    lv_obj_set_size(btn_off, 80, 26);
    lv_obj_align(btn_off, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_color(btn_off, UI_COLOR_ERR, 0);
    lv_obj_t *lbl_off = lv_label_create(btn_off);
    lv_label_set_text(lbl_off, "Switch OFF");
    lv_obj_set_style_text_font(lbl_off, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_off, UI_COLOR_TEXT, 0);
    lv_obj_center(lbl_off);
    lv_obj_add_event_cb(btn_off, switch_off_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_reset = lv_button_create(s_panel);
    lv_obj_set_size(btn_reset, 80, 26);
    lv_obj_align(btn_reset, LV_ALIGN_BOTTOM_RIGHT, -4, -2);
    lv_obj_set_style_bg_color(btn_reset, UI_COLOR_WARN, 0);
    lv_obj_t *lbl_rst = lv_label_create(btn_reset);
    lv_label_set_text(lbl_rst, "Reset");
    lv_obj_set_style_text_font(lbl_rst, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_rst, lv_color_black(), 0);
    lv_obj_center(lbl_rst);
    lv_obj_add_event_cb(btn_reset, reset_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Premiere mise a jour */
    bmu_ui_detail_update(ctx, idx);

    ESP_LOGI(TAG, "Detail cree pour BAT %d", idx + 1);
}

/* ── Update ───────────────────────────────────────────────────────── */

void bmu_ui_detail_update(bmu_ui_ctx_t *ctx, int idx)
{
    if (s_panel == NULL || idx < 0 || idx >= ctx->nb_ina) return;

    float v_mv = bmu_protection_get_voltage(ctx->prot, idx);
    float v = v_mv / 1000.0f;
    bmu_battery_state_t state = bmu_protection_get_state(ctx->prot, idx);
    float ah_d = bmu_battery_manager_get_ah_discharge(ctx->mgr, idx);
    float ah_c = bmu_battery_manager_get_ah_charge(ctx->mgr, idx);
    int nb_sw = 0;
    esp_err_t nb_sw_ret = bmu_protection_get_switch_count(ctx->prot, idx, &nb_sw);

    /* Calculer courant et puissance depuis l'historique */
    bmu_chart_history_t *h = (ctx->chart_hist != NULL) ? &ctx->chart_hist[idx] : NULL;
    float i_a = 0.0f;
    if (h != NULL && h->count > 0) {
        int last = (h->head - 1 + CONFIG_BMU_CHART_HISTORY_POINTS) % CONFIG_BMU_CHART_HISTORY_POINTS;
        i_a = h->current_a[last];
    }
    float p_w = v * i_a;

    /* Valeurs numeriques */
    char buf[20];
    snprintf(buf, sizeof(buf), "%.2fV", v);
    lv_label_set_text(s_val_labels[0], buf);

    snprintf(buf, sizeof(buf), "%.2f A", i_a);
    lv_label_set_text(s_val_labels[1], buf);

    snprintf(buf, sizeof(buf), "%.1f W", p_w);
    lv_label_set_text(s_val_labels[2], buf);

    /* Temperature depuis AHT30 */
    if (bmu_climate_is_available()) {
        snprintf(buf, sizeof(buf), "%.1f C", bmu_climate_get_temperature());
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(s_val_labels[3], buf);

    snprintf(buf, sizeof(buf), "%.3f", ah_c);
    lv_label_set_text(s_val_labels[4], buf);

    snprintf(buf, sizeof(buf), "%.3f", ah_d);
    lv_label_set_text(s_val_labels[5], buf);

    if (nb_sw_ret == ESP_OK) {
        snprintf(buf, sizeof(buf), "%d", nb_sw);
        lv_label_set_text(s_val_labels[6], buf);
    } else {
        lv_label_set_text(s_val_labels[6], "---");
    }

    lv_label_set_text(s_val_labels[7], state_name(state));
    lv_obj_set_style_text_color(s_val_labels[7], state_color(state), 0);

    /* Mettre a jour le graphique avec les donnees du ring buffer */
    if (s_chart != NULL && h != NULL && h->count > 0) {
        int points = h->count < 60 ? h->count : 60;
        int start = (h->head - points + CONFIG_BMU_CHART_HISTORY_POINTS) % CONFIG_BMU_CHART_HISTORY_POINTS;

        for (int i = 0; i < 60; i++) {
            if (i < points) {
                int ri = (start + i) % CONFIG_BMU_CHART_HISTORY_POINTS;
                lv_chart_set_value_by_id(s_chart, s_ser_v, i, (int32_t)h->voltage_mv[ri]);
                lv_chart_set_value_by_id(s_chart, s_ser_i, i, (int32_t)(h->current_a[ri] * 1000.0f));
            } else {
                lv_chart_set_value_by_id(s_chart, s_ser_v, i, LV_CHART_POINT_NONE);
                lv_chart_set_value_by_id(s_chart, s_ser_i, i, LV_CHART_POINT_NONE);
            }
        }

        /* Auto-scale axe Y tension */
        update_chart_range(h);

        lv_chart_refresh(s_chart);
    }
}

/* ── Destroy ──────────────────────────────────────────────────────── */

void bmu_ui_detail_destroy(void)
{
    if (s_panel != NULL) {
        lv_obj_delete(s_panel);
        s_panel = NULL;
    }
    s_chart = NULL;
    s_ser_v = NULL;
    s_ser_i = NULL;
    for (int i = 0; i < 8; i++) s_val_labels[i] = NULL;

    /* Mettre a jour l'etat de navigation */
    if (s_nav_ref != NULL) {
        s_nav_ref->detail_visible = false;
        s_nav_ref->detail_battery = -1;
        s_nav_ref->detail_panel = NULL;
    }
    s_battery_idx = -1;
    ESP_LOGI(TAG, "Detail detruit, retour grille");
}
