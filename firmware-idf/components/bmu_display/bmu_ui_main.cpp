#include "bmu_ui.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "esp_log.h"
#include <cstdio>

static const char *TAG = "UI_MAIN";

// Colors
#define COL_GREEN   lv_color_hex(0x00C853)
#define COL_RED     lv_color_hex(0xFF1744)
#define COL_ORANGE  lv_color_hex(0xFF9100)
#define COL_GREY    lv_color_hex(0x9E9E9E)
#define COL_BLUE    lv_color_hex(0x2979FF)
#define COL_BG      lv_color_hex(0x1E1E1E)
#define COL_CARD    lv_color_hex(0x2D2D2D)

static lv_obj_t *battery_cells[16] = {};
static lv_obj_t *voltage_labels[16] = {};
static lv_obj_t *current_labels[16] = {};
static lv_obj_t *status_indicators[16] = {};
static lv_obj_t *summary_label = NULL;
static lv_obj_t *s_grid_parent = NULL;
static bmu_ui_ctx_t *s_ctx_ref = NULL;
static bmu_nav_state_t *s_nav = NULL;

static lv_color_t state_color(bmu_battery_state_t state) {
    switch (state) {
        case BMU_STATE_CONNECTED:    return COL_GREEN;
        case BMU_STATE_DISCONNECTED: return COL_RED;
        case BMU_STATE_RECONNECTING: return COL_ORANGE;
        case BMU_STATE_ERROR:        return COL_RED;
        case BMU_STATE_LOCKED:       return COL_GREY;
        default:                     return COL_GREY;
    }
}

/* ── Callback tap sur cellule ─────────────────────────────────────── */

static void cell_clicked_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_nav == NULL || s_ctx_ref == NULL || s_grid_parent == NULL) return;
    if (s_nav->detail_visible) return; // deja en detail

    ESP_LOGI(TAG, "Tap sur BAT %d → detail", idx + 1);

    s_nav->detail_visible = true;
    s_nav->detail_battery = idx;

    /* Creer le detail en overlay sur le parent du tab */
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

    // Set dark background
    lv_obj_set_style_bg_color(parent, COL_BG, 0);

    // Header
    lv_obj_t *header = lv_label_create(parent);
    lv_label_set_text(header, "KXKM BMU");
    lv_obj_set_style_text_color(header, COL_BLUE, 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 4, 2);

    // Grid container — 4 columns
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_set_size(grid, 312, 200);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 2, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    int nb = ctx->nb_ina > 16 ? 16 : ctx->nb_ina;
    for (int i = 0; i < nb; i++) {
        // Cell card
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_set_size(cell, 74, 46);
        lv_obj_set_style_bg_color(cell, COL_CARD, 0);
        lv_obj_set_style_radius(cell, 4, 0);
        lv_obj_set_style_pad_all(cell, 2, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        battery_cells[i] = cell;

        /* Rendre la cellule clickable */
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cell, cell_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        // Battery number
        lv_obj_t *num = lv_label_create(cell);
        char buf[8];
        snprintf(buf, sizeof(buf), "B%d", i + 1);
        lv_label_set_text(num, buf);
        lv_obj_set_style_text_color(num, lv_color_white(), 0);
        lv_obj_set_style_text_font(num, &lv_font_montserrat_14, 0);
        lv_obj_align(num, LV_ALIGN_TOP_LEFT, 0, 0);

        // Status dot
        lv_obj_t *dot = lv_obj_create(cell);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, COL_GREY, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, 0, 0);
        status_indicators[i] = dot;

        // Voltage
        lv_obj_t *vlbl = lv_label_create(cell);
        lv_label_set_text(vlbl, "--.-V");
        lv_obj_set_style_text_color(vlbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(vlbl, &lv_font_montserrat_14, 0);
        lv_obj_align(vlbl, LV_ALIGN_BOTTOM_LEFT, 0, -10);
        voltage_labels[i] = vlbl;

        // Current
        lv_obj_t *ilbl = lv_label_create(cell);
        lv_label_set_text(ilbl, "-.-A");
        lv_obj_set_style_text_color(ilbl, COL_GREY, 0);
        lv_obj_set_style_text_font(ilbl, &lv_font_montserrat_14, 0);
        lv_obj_align(ilbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        current_labels[i] = ilbl;
    }

    // Summary bar
    summary_label = lv_label_create(parent);
    lv_label_set_text(summary_label, "...");
    lv_obj_set_style_text_color(summary_label, COL_GREY, 0);
    lv_obj_set_style_text_font(summary_label, &lv_font_montserrat_14, 0);
    lv_obj_align(summary_label, LV_ALIGN_BOTTOM_MID, 0, -2);
}

void bmu_ui_main_update(bmu_ui_ctx_t *ctx)
{
    /* Si le detail est visible, mettre a jour le detail, pas la grille */
    if (s_nav != NULL && s_nav->detail_visible) {
        bmu_ui_detail_update(ctx, s_nav->detail_battery);
        return;
    }

    int nb = ctx->nb_ina > 16 ? 16 : ctx->nb_ina;
    float total_i = 0;
    float sum_v = 0;
    int n_active = 0;

    for (int i = 0; i < nb; i++) {
        float v_mv = bmu_protection_get_voltage(ctx->prot, i);
        float v = v_mv / 1000.0f;
        bmu_battery_state_t state = bmu_protection_get_state(ctx->prot, i);

        char vbuf[12], ibuf[12];
        snprintf(vbuf, sizeof(vbuf), "%.1fV", v);
        lv_label_set_text(voltage_labels[i], vbuf);

        // Read current from battery manager
        float i_a = 0; // Would need INA read — simplified for now
        snprintf(ibuf, sizeof(ibuf), "%.1fA", i_a);
        lv_label_set_text(current_labels[i], ibuf);

        // Status dot color
        lv_obj_set_style_bg_color(status_indicators[i], state_color(state), 0);

        if (v > 1.0f) {
            sum_v += v;
            n_active++;
        }
    }

    // Summary
    float avg_v = n_active > 0 ? sum_v / n_active : 0;
    char summary[64];
    snprintf(summary, sizeof(summary), "Avg:%.1fV  Active:%d/%d", avg_v, n_active, nb);
    lv_label_set_text(summary_label, summary);

    (void)total_i;
}
