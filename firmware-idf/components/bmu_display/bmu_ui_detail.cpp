#include "bmu_ui.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include <cstdio>

static lv_obj_t *detail_labels[8] = {};
static int current_battery = -1;

static const char *state_name(bmu_battery_state_t s) {
    switch (s) {
        case BMU_STATE_CONNECTED:    return "CONNECTED";
        case BMU_STATE_DISCONNECTED: return "DISCONNECTED";
        case BMU_STATE_RECONNECTING: return "RECONNECTING";
        case BMU_STATE_ERROR:        return "ERROR";
        case BMU_STATE_LOCKED:       return "LOCKED";
        default:                     return "UNKNOWN";
    }
}

void bmu_ui_detail_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx, int idx)
{
    current_battery = idx;
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x1E1E1E), 0);

    // Back button
    lv_obj_t *btn_back = lv_button_create(parent);
    lv_obj_set_size(btn_back, 60, 24);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_t *btn_lbl = lv_label_create(btn_back);
    lv_label_set_text(btn_lbl, LV_SYMBOL_LEFT " Back");

    // Title
    char title[16];
    snprintf(title, sizeof(title), "BAT %d", idx + 1);
    lv_obj_t *tlbl = lv_label_create(parent);
    lv_label_set_text(tlbl, title);
    lv_obj_set_style_text_color(tlbl, lv_color_hex(0x2979FF), 0);
    lv_obj_set_style_text_font(tlbl, &lv_font_montserrat_14, 0);
    lv_obj_align(tlbl, LV_ALIGN_TOP_MID, 0, 4);

    // Info labels
    const char *labels[] = {
        "Tension:", "Courant:", "Puissance:", "Temperature:",
        "Ah decharge:", "Ah charge:", "Nb coupures:", "Etat:"
    };
    for (int i = 0; i < 8; i++) {
        lv_obj_t *row = lv_label_create(parent);
        lv_label_set_text(row, labels[i]);
        lv_obj_set_style_text_color(row, lv_color_hex(0x9E9E9E), 0);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 10, 36 + i * 22);

        detail_labels[i] = lv_label_create(parent);
        lv_label_set_text(detail_labels[i], "---");
        lv_obj_set_style_text_color(detail_labels[i], lv_color_white(), 0);
        lv_obj_align(detail_labels[i], LV_ALIGN_TOP_LEFT, 140, 36 + i * 22);
    }
}

void bmu_ui_detail_update(bmu_ui_ctx_t *ctx, int idx)
{
    if (idx < 0 || idx >= ctx->nb_ina) return;
    float v_mv = bmu_protection_get_voltage(ctx->prot, idx);
    float ah_d = bmu_battery_manager_get_ah_discharge(ctx->mgr, idx);
    float ah_c = bmu_battery_manager_get_ah_charge(ctx->mgr, idx);
    bmu_battery_state_t state = bmu_protection_get_state(ctx->prot, idx);

    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f V", v_mv / 1000.0f);
    lv_label_set_text(detail_labels[0], buf);
    lv_label_set_text(detail_labels[1], "---"); // Need INA read
    lv_label_set_text(detail_labels[2], "---");
    lv_label_set_text(detail_labels[3], "---");
    snprintf(buf, sizeof(buf), "%.3f Ah", ah_d);
    lv_label_set_text(detail_labels[4], buf);
    snprintf(buf, sizeof(buf), "%.3f Ah", ah_c);
    lv_label_set_text(detail_labels[5], buf);
    lv_label_set_text(detail_labels[6], "---");
    lv_label_set_text(detail_labels[7], state_name(state));
}
