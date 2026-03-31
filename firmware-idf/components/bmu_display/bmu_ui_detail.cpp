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
#include "esp_log.h"
#include <cstdio>

static const char *TAG = "UI_DETAIL";

/* ── Couleurs ─────────────────────────────────────────────────────── */

#define COL_BG          lv_color_hex(0x1E1E1E)
#define COL_CARD        lv_color_hex(0x2D2D2D)
#define COL_BLUE        lv_color_hex(0x2979FF)
#define COL_GREEN       lv_color_hex(0x00C853)
#define COL_RED         lv_color_hex(0xFF1744)
#define COL_ORANGE      lv_color_hex(0xFF9100)
#define COL_GREY        lv_color_hex(0x9E9E9E)
#define COL_WHITE       lv_color_white()
#define COL_CHART_V     lv_color_hex(0x42A5F5)  // bleu voltage
#define COL_CHART_I     lv_color_hex(0x66BB6A)  // vert courant

/* ── State statique ───────────────────────────────────────────────── */

static lv_obj_t *s_panel = NULL;         // panneau overlay principal
static lv_obj_t *s_chart = NULL;         // lv_chart widget
static lv_chart_series_t *s_ser_v = NULL; // serie tension
static lv_chart_series_t *s_ser_i = NULL; // serie courant
static lv_obj_t *s_val_labels[8] = {};    // labels valeurs numeriques
static int s_battery_idx = -1;
static bmu_ui_ctx_t *s_ctx_ref = NULL;
static bmu_nav_state_t *s_nav_ref = NULL;

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
        case BMU_STATE_CONNECTED:    return COL_GREEN;
        case BMU_STATE_DISCONNECTED: return COL_RED;
        case BMU_STATE_RECONNECTING: return COL_ORANGE;
        case BMU_STATE_ERROR:        return COL_RED;
        case BMU_STATE_LOCKED:       return COL_GREY;
        default:                     return COL_GREY;
    }
}

/* ── Callbacks boutons ────────────────────────────────────────────── */

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Retour a la grille depuis BAT %d", s_battery_idx + 1);
    bmu_ui_detail_destroy();
}

static void confirm_switch_on_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    const char *btn_text = lv_msgbox_get_active_button_text(mbox);
    if (btn_text && strcmp(btn_text, "Oui") == 0) {
        ESP_LOGI(TAG, "Switch ON BAT %d via display", s_battery_idx + 1);
        bmu_protection_web_switch(s_ctx_ref->prot, s_battery_idx, true);
    }
    lv_msgbox_close(mbox);
}

static void confirm_switch_off_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    const char *btn_text = lv_msgbox_get_active_button_text(mbox);
    if (btn_text && strcmp(btn_text, "Oui") == 0) {
        ESP_LOGI(TAG, "Switch OFF BAT %d via display", s_battery_idx + 1);
        bmu_protection_web_switch(s_ctx_ref->prot, s_battery_idx, false);
    }
    lv_msgbox_close(mbox);
}

static void confirm_reset_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    const char *btn_text = lv_msgbox_get_active_button_text(mbox);
    if (btn_text && strcmp(btn_text, "Oui") == 0) {
        ESP_LOGI(TAG, "Reset compteur BAT %d via display", s_battery_idx + 1);
        bmu_protection_reset_switch_count(s_ctx_ref->prot, s_battery_idx);
    }
    lv_msgbox_close(mbox);
}

static void switch_on_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Confirmer");
    lv_msgbox_add_text(mbox, "Activer cette batterie ?");
    lv_msgbox_add_footer_button(mbox, "Oui");
    lv_msgbox_add_footer_button(mbox, "Non");
    lv_obj_set_style_bg_color(mbox, COL_CARD, 0);
    lv_obj_set_style_text_font(mbox, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(mbox, confirm_switch_on_cb, LV_EVENT_VALUE_CHANGED, mbox);
}

static void switch_off_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Confirmer");
    lv_msgbox_add_text(mbox, "Desactiver cette batterie ?");
    lv_msgbox_add_footer_button(mbox, "Oui");
    lv_msgbox_add_footer_button(mbox, "Non");
    lv_obj_set_style_bg_color(mbox, COL_CARD, 0);
    lv_obj_set_style_text_font(mbox, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(mbox, confirm_switch_off_cb, LV_EVENT_VALUE_CHANGED, mbox);
}

static void reset_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Confirmer");
    lv_msgbox_add_text(mbox, "Remettre le compteur a zero ?");
    lv_msgbox_add_footer_button(mbox, "Oui");
    lv_msgbox_add_footer_button(mbox, "Non");
    lv_obj_set_style_bg_color(mbox, COL_CARD, 0);
    lv_obj_set_style_text_font(mbox, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(mbox, confirm_reset_cb, LV_EVENT_VALUE_CHANGED, mbox);
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
    lv_obj_set_style_bg_color(s_panel, COL_BG, 0);
    lv_obj_set_style_border_width(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 4, 0);
    lv_obj_set_style_radius(s_panel, 0, 0);
    lv_obj_move_foreground(s_panel);

    /* ── Ligne du haut : Back + titre ─────────────────────────────── */
    lv_obj_t *btn_back = lv_button_create(s_panel);
    lv_obj_set_size(btn_back, 56, 22);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_back, COL_CARD, 0);
    lv_obj_t *btn_lbl = lv_label_create(btn_back);
    lv_label_set_text(btn_lbl, LV_SYMBOL_LEFT " Ret");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(btn_back, back_btn_cb, LV_EVENT_CLICKED, NULL);

    char title[16];
    snprintf(title, sizeof(title), "BAT %d", idx + 1);
    lv_obj_t *tlbl = lv_label_create(s_panel);
    lv_label_set_text(tlbl, title);
    lv_obj_set_style_text_color(tlbl, COL_BLUE, 0);
    lv_obj_set_style_text_font(tlbl, &lv_font_montserrat_14, 0);
    lv_obj_align(tlbl, LV_ALIGN_TOP_MID, 0, 2);

    /* ── Graphique V/I (moitie gauche) ────────────────────────────── */
    s_chart = lv_chart_create(s_panel);
    lv_obj_set_size(s_chart, 150, 90);
    lv_obj_align(s_chart, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, 60);  // affiche les 60 derniers points (30s)
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_color(s_chart, COL_GREY, 0);
    lv_obj_set_style_border_width(s_chart, 1, 0);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR); // pas de points

    lv_chart_set_div_line_count(s_chart, 3, 0);

    /* Serie tension (axe Y primaire) — bleu */
    s_ser_v = lv_chart_add_series(s_chart, COL_CHART_V, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 20000, 32000); // 20-32V en mV

    /* Serie courant (axe Y secondaire) — vert */
    s_ser_i = lv_chart_add_series(s_chart, COL_CHART_I, LV_CHART_AXIS_SECONDARY_Y);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_SECONDARY_Y, -5000, 30000); // -5A..30A en mA

    /* ── Valeurs numeriques (moitie droite) ───────────────────────── */
    const char *labels[] = {
        "V:", "I:", "P:", "T:",
        "Ah+:", "Ah-:", "Coup:", "Etat:"
    };
    for (int i = 0; i < 8; i++) {
        lv_obj_t *name = lv_label_create(s_panel);
        lv_label_set_text(name, labels[i]);
        lv_obj_set_style_text_color(name, COL_GREY, 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 158, 26 + i * 16);

        s_val_labels[i] = lv_label_create(s_panel);
        lv_label_set_text(s_val_labels[i], "---");
        lv_obj_set_style_text_color(s_val_labels[i], COL_WHITE, 0);
        lv_obj_set_style_text_font(s_val_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_align(s_val_labels[i], LV_ALIGN_TOP_LEFT, 200, 26 + i * 16);
    }

    /* ── Boutons actions (bas de l'ecran) ─────────────────────────── */
    lv_obj_t *btn_on = lv_button_create(s_panel);
    lv_obj_set_size(btn_on, 80, 26);
    lv_obj_align(btn_on, LV_ALIGN_BOTTOM_LEFT, 4, -2);
    lv_obj_set_style_bg_color(btn_on, COL_GREEN, 0);
    lv_obj_t *lbl_on = lv_label_create(btn_on);
    lv_label_set_text(lbl_on, "Switch ON");
    lv_obj_set_style_text_font(lbl_on, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_on, lv_color_black(), 0);
    lv_obj_center(lbl_on);
    lv_obj_add_event_cb(btn_on, switch_on_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_off = lv_button_create(s_panel);
    lv_obj_set_size(btn_off, 80, 26);
    lv_obj_align(btn_off, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_color(btn_off, COL_RED, 0);
    lv_obj_t *lbl_off = lv_label_create(btn_off);
    lv_label_set_text(lbl_off, "Switch OFF");
    lv_obj_set_style_text_font(lbl_off, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_off, COL_WHITE, 0);
    lv_obj_center(lbl_off);
    lv_obj_add_event_cb(btn_off, switch_off_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_reset = lv_button_create(s_panel);
    lv_obj_set_size(btn_reset, 80, 26);
    lv_obj_align(btn_reset, LV_ALIGN_BOTTOM_RIGHT, -4, -2);
    lv_obj_set_style_bg_color(btn_reset, COL_ORANGE, 0);
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
    int nb_sw = ctx->prot->nb_switch[idx];

    /* Calculer courant et puissance depuis la tension et l'historique */
    bmu_chart_history_t *h = &ctx->chart_hist[idx];
    float i_a = 0.0f;
    if (h->count > 0) {
        int last = (h->head - 1 + CONFIG_BMU_CHART_HISTORY_POINTS) % CONFIG_BMU_CHART_HISTORY_POINTS;
        i_a = h->current_a[last];
    }
    float p_w = v * i_a;

    /* Valeurs numeriques */
    char buf[20];
    snprintf(buf, sizeof(buf), "%.2f V", v);
    lv_label_set_text(s_val_labels[0], buf);

    snprintf(buf, sizeof(buf), "%.2f A", i_a);
    lv_label_set_text(s_val_labels[1], buf);

    snprintf(buf, sizeof(buf), "%.1f W", p_w);
    lv_label_set_text(s_val_labels[2], buf);

    lv_label_set_text(s_val_labels[3], "---");  // Temperature — pas encore disponible

    snprintf(buf, sizeof(buf), "%.3f", ah_c);
    lv_label_set_text(s_val_labels[4], buf);

    snprintf(buf, sizeof(buf), "%.3f", ah_d);
    lv_label_set_text(s_val_labels[5], buf);

    snprintf(buf, sizeof(buf), "%d", nb_sw);
    lv_label_set_text(s_val_labels[6], buf);

    lv_label_set_text(s_val_labels[7], state_name(state));
    lv_obj_set_style_text_color(s_val_labels[7], state_color(state), 0);

    /* Mettre a jour le graphique avec les donnees du ring buffer */
    if (s_chart != NULL && h->count > 0) {
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
