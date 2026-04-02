/**
 * @file bmu_ui_soh.cpp
 * @brief SOH screen — high contrast, per-battery color-coded health bars.
 */

#include "bmu_ui.h"
#include "bmu_soh.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include <cstdio>

static const char *TAG = "UI_SOH";

static lv_obj_t *s_soh_mean_label   = NULL;
static lv_obj_t *s_soh_mean_bar     = NULL;
static lv_obj_t *s_soh_bars[16]     = {};
static lv_obj_t *s_soh_pct_labels[16] = {};
static lv_obj_t *s_soh_warn_labels[16] = {};
static lv_obj_t *s_timestamp_label  = NULL;
static lv_obj_t *s_soh_list = NULL;
static int s_nb = 0;
static int s_soh_created = 0;

/* ── Couleur selon seuil ──────────────────────────────────────────── */

static lv_color_t soh_color(float soh_pct)
{
    if (soh_pct >= 70.0f) return UI_COLOR_OK;
    if (soh_pct >= 40.0f) return UI_COLOR_WARN;
    return lv_color_hex(0xFF3333);
}

/* ── Create ───────────────────────────────────────────────────────── */

void bmu_ui_soh_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx)
{
    s_nb = 0; /* Rows created lazily in update() */
    s_soh_created = 0;

    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);

    /* ── Ligne header : titre gauche + timestamp droite ────────────── */

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "State of Health");
    lv_obj_set_style_text_color(title, UI_COLOR_SOH, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 6, 4);

    s_timestamp_label = lv_label_create(parent);
    lv_label_set_text(s_timestamp_label, "t=0s");
    lv_obj_set_style_text_color(s_timestamp_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_timestamp_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_timestamp_label, LV_ALIGN_TOP_RIGHT, -6, 4);

    /* ── Carte SOH moyen ────────────────────────────────────────────── */

    lv_obj_t *mean_card = lv_obj_create(parent);
    lv_obj_set_size(mean_card, 110, 48);
    lv_obj_align(mean_card, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_style_bg_color(mean_card, UI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(mean_card, UI_COLOR_SOH, 0);
    lv_obj_set_style_border_width(mean_card, 1, 0);
    lv_obj_set_style_pad_all(mean_card, 4, 0);
    lv_obj_clear_flag(mean_card, LV_OBJ_FLAG_SCROLLABLE);

    s_soh_mean_label = lv_label_create(mean_card);
    lv_label_set_text(s_soh_mean_label, "---%");
    lv_obj_set_style_text_color(s_soh_mean_label, UI_COLOR_SOH, 0);
    lv_obj_set_style_text_font(s_soh_mean_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_soh_mean_label, LV_ALIGN_TOP_MID, 0, 0);

    s_soh_mean_bar = lv_bar_create(mean_card);
    lv_obj_set_size(s_soh_mean_bar, 90, 10);
    lv_bar_set_range(s_soh_mean_bar, 0, 100);
    lv_bar_set_value(s_soh_mean_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_soh_mean_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_soh_mean_bar, UI_COLOR_SOH, LV_PART_INDICATOR);
    lv_obj_align(s_soh_mean_bar, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* ── Liste scrollable des batteries ────────────────────────────── */

    s_soh_list = lv_obj_create(parent);
    lv_obj_set_size(s_soh_list, 300, 140);
    lv_obj_align(s_soh_list, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_flex_flow(s_soh_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_soh_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(s_soh_list, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(s_soh_list, 0, 0);
    lv_obj_set_style_pad_all(s_soh_list, 2, 0);
    lv_obj_set_style_pad_row(s_soh_list, 2, 0);
    lv_obj_set_scrollbar_mode(s_soh_list, LV_SCROLLBAR_MODE_AUTO);

    /* SOH rows created lazily in update() */

    /* ── Légende ────────────────────────────────────────────────────── */

    lv_obj_t *legend = lv_obj_create(parent);
    lv_obj_set_size(legend, 300, 20);
    lv_obj_align(legend, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(legend, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(legend, 0, 0);
    lv_obj_set_style_pad_all(legend, 0, 0);
    lv_obj_set_style_pad_column(legend, 8, 0);
    lv_obj_clear_flag(legend, LV_OBJ_FLAG_SCROLLABLE);

    /* Point + texte pour chaque niveau */
    struct { lv_color_t col; const char *txt; } entries[] = {
        { UI_COLOR_OK,              "OK"        },
        { UI_COLOR_WARN,            "USURE"     },
        { lv_color_hex(0xFF3333),   "REMPLACER" },
    };

    for (int k = 0; k < 3; k++) {
        lv_obj_t *dot = lv_obj_create(legend);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, entries[k].col, 0);
        lv_obj_set_style_border_width(dot, 0, 0);

        lv_obj_t *ltxt = lv_label_create(legend);
        lv_label_set_text(ltxt, entries[k].txt);
        lv_obj_set_style_text_color(ltxt, entries[k].col, 0);
        lv_obj_set_style_text_font(ltxt, &lv_font_montserrat_14, 0);
    }

    ESP_LOGI(TAG, "SOH screen créé — %d batteries", s_nb);
}

/* ── Lazy row creation ────────────────────────────────────────────── */

static void ensure_soh_rows(int nb)
{
    if (nb <= s_soh_created || s_soh_list == NULL) return;
    if (nb > 16) nb = 16;

    for (int i = s_soh_created; i < nb; i++) {
        lv_obj_t *row = lv_obj_create(s_soh_list);
        lv_obj_set_size(row, 288, 20);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *blbl = lv_label_create(row);
        lv_label_set_text(blbl, bmu_config_get_battery_label(i));
        lv_obj_set_width(blbl, 48);
        lv_obj_set_style_text_color(blbl, UI_COLOR_TEXT_SEC, 0);

        s_soh_bars[i] = lv_bar_create(row);
        lv_obj_set_size(s_soh_bars[i], 160, 10);
        lv_bar_set_range(s_soh_bars[i], 0, 100);
        lv_bar_set_value(s_soh_bars[i], 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_soh_bars[i], lv_color_hex(0x222222), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_soh_bars[i], UI_COLOR_OK, LV_PART_INDICATOR);

        s_soh_pct_labels[i] = lv_label_create(row);
        lv_label_set_text(s_soh_pct_labels[i], "---%");
        lv_obj_set_width(s_soh_pct_labels[i], 38);
        lv_obj_set_style_text_color(s_soh_pct_labels[i], UI_COLOR_TEXT, 0);

        s_soh_warn_labels[i] = lv_label_create(row);
        lv_label_set_text(s_soh_warn_labels[i], "REMPLACER");
        lv_obj_set_style_text_color(s_soh_warn_labels[i], lv_color_hex(0xFF3333), 0);
        lv_obj_add_flag(s_soh_warn_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_soh_created = nb;
    s_nb = nb;
}

/* ── Update ───────────────────────────────────────────────────────── */

void bmu_ui_soh_update(bmu_ui_ctx_t *ctx)
{
    if (!ctx) return;

    int nb = ctx->nb_ina > 16 ? 16 : ctx->nb_ina;
    if (nb == 0) return;

    ensure_soh_rows(nb);

    float sum   = 0.0f;
    int   valid = 0;

    for (int i = 0; i < nb; i++) {
        float soh = bmu_soh_get_cached(i);

        if (soh < 0.0f) {
            /* Pas encore de donnée */
            if (s_soh_pct_labels[i])  lv_label_set_text(s_soh_pct_labels[i], "---%");
            if (s_soh_warn_labels[i]) lv_obj_add_flag(s_soh_warn_labels[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        float pct_f = soh * 100.0f;
        int   pct   = (int)(pct_f + 0.5f);
        if (pct > 100) pct = 100;
        if (pct < 0)   pct = 0;

        /* Barre */
        if (s_soh_bars[i]) {
            lv_bar_set_value(s_soh_bars[i], pct, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(s_soh_bars[i], soh_color(pct_f), LV_PART_INDICATOR);
        }

        /* Pourcentage */
        if (s_soh_pct_labels[i]) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d%%", pct);
            lv_label_set_text(s_soh_pct_labels[i], buf);
            lv_obj_set_style_text_color(s_soh_pct_labels[i], soh_color(pct_f), 0);
        }

        /* Avertissement REMPLACER */
        if (s_soh_warn_labels[i]) {
            if (pct_f < 40.0f) {
                lv_obj_clear_flag(s_soh_warn_labels[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_soh_warn_labels[i], LV_OBJ_FLAG_HIDDEN);
            }
        }

        sum += soh;
        valid++;
    }

    /* SOH moyen */
    if (s_soh_mean_label && s_soh_mean_bar) {
        if (valid > 0) {
            float mean_pct = (sum / (float)valid) * 100.0f;
            int   mean_i   = (int)(mean_pct + 0.5f);
            if (mean_i > 100) mean_i = 100;

            char buf[16];
            snprintf(buf, sizeof(buf), "%d%%", mean_i);
            lv_label_set_text(s_soh_mean_label, buf);
            lv_bar_set_value(s_soh_mean_bar, mean_i, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(s_soh_mean_bar, soh_color(mean_pct), LV_PART_INDICATOR);
        } else {
            lv_label_set_text(s_soh_mean_label, "---%");
            lv_bar_set_value(s_soh_mean_bar, 0, LV_ANIM_OFF);
        }
    }

    /* Timestamp uptime */
    if (s_timestamp_label) {
        uint32_t secs = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        char tsbuf[16];
        snprintf(tsbuf, sizeof(tsbuf), "%luh%02lum",
                 (unsigned long)(secs / 3600),
                 (unsigned long)((secs % 3600) / 60));
        lv_label_set_text(s_timestamp_label, tsbuf);
    }
}
