/**
 * @file bmu_ui_soh.cpp
 * @brief SOH prediction tab — per-battery health bars from TFLite Micro FPNN model.
 */

#include "bmu_ui.h"
#include "bmu_soh.h"
#include "esp_log.h"
#include "lvgl.h"

#include <cstdio>

static const char *TAG = "UI_SOH";

static lv_obj_t *s_bars[BMU_MAX_BATTERIES];
static lv_obj_t *s_labels[BMU_MAX_BATTERIES];
static lv_obj_t *s_pct_labels[BMU_MAX_BATTERIES];
static lv_obj_t *s_summary_label = NULL;
static bmu_ui_ctx_t *s_ctx = NULL;

void bmu_ui_soh_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx)
{
    s_ctx = ctx;
    int nb = ctx->nb_ina > BMU_MAX_BATTERIES ? BMU_MAX_BATTERIES : ctx->nb_ina;
    if (nb == 0) nb = 1;

    /* Title */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "SOH — Battery Health");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00BFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    /* Container for bars */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 300, 180);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont, 2, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

    for (int i = 0; i < nb; i++) {
        /* Row container */
        lv_obj_t *row = lv_obj_create(cont);
        lv_obj_set_size(row, 280, 18);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);

        /* Battery label */
        s_labels[i] = lv_label_create(row);
        char buf[8];
        snprintf(buf, sizeof(buf), "B%d", i + 1);
        lv_label_set_text(s_labels[i], buf);
        lv_obj_set_width(s_labels[i], 28);

        /* Health bar */
        s_bars[i] = lv_bar_create(row);
        lv_obj_set_size(s_bars[i], 200, 12);
        lv_bar_set_range(s_bars[i], 0, 100);
        lv_bar_set_value(s_bars[i], 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_bars[i], lv_color_hex(0x333333), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_bars[i], lv_color_hex(0x00CC66), LV_PART_INDICATOR);

        /* Percentage label */
        s_pct_labels[i] = lv_label_create(row);
        lv_label_set_text(s_pct_labels[i], "---%");
        lv_obj_set_width(s_pct_labels[i], 40);
    }

    /* Summary */
    s_summary_label = lv_label_create(parent);
    lv_label_set_text(s_summary_label, "Avg SOH: ---");
    lv_obj_align(s_summary_label, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_text_color(s_summary_label, lv_color_hex(0xAAAAAA), 0);
}

void bmu_ui_soh_update(bmu_ui_ctx_t *ctx)
{
    if (!ctx) return;
    int nb = ctx->nb_ina > BMU_MAX_BATTERIES ? BMU_MAX_BATTERIES : ctx->nb_ina;

    float sum = 0.0f;
    int valid = 0;

    for (int i = 0; i < nb; i++) {
        float soh = bmu_soh_get_cached(i);
        if (soh < 0.0f) continue;

        int pct = (int)(soh * 100.0f + 0.5f);
        if (pct > 100) pct = 100;
        if (pct < 0) pct = 0;

        lv_bar_set_value(s_bars[i], pct, LV_ANIM_OFF);

        /* Color: green >70%, yellow 40-70%, red <40% */
        lv_color_t col;
        if (pct >= 70) {
            col = lv_color_hex(0x00CC66);
        } else if (pct >= 40) {
            col = lv_color_hex(0xFFAA00);
        } else {
            col = lv_color_hex(0xFF3333);
        }
        lv_obj_set_style_bg_color(s_bars[i], col, LV_PART_INDICATOR);

        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(s_pct_labels[i], buf);

        sum += soh;
        valid++;
    }

    if (s_summary_label && valid > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Avg SOH: %d%%", (int)((sum / valid) * 100.0f + 0.5f));
        lv_label_set_text(s_summary_label, buf);
    }
}
