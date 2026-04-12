// firmware-idf-v2/components/bmu_ui/src/tab_soh.c
//
// Phase 17 -- onglet SOH : 16 barres horizontales avec label "B0: 87%".
// soh_pct = 0-100 ou 0xFF = inconnu.

#include <string.h>

#include "bmu_ui_internal.h"
#include "bmu_core.h"

#define BAR_H  10
#define ROW_H  13

typedef struct {
    lv_obj_t *lbl;
    lv_obj_t *bar;
} soh_row_t;

static soh_row_t s_rows[MAX_BATTERIES];

void tab_soh_create(lv_obj_t *parent)
{
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(parent, 2, LV_PART_MAIN);

    for (int i = 0; i < MAX_BATTERIES; i++) {
        int y = i * ROW_H;

        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text_fmt(lbl, "B%d:--%%", i);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xcccccc), LV_PART_MAIN);
        lv_obj_set_pos(lbl, 0, y);
        lv_obj_set_width(lbl, 60);

        lv_obj_t *bar = lv_bar_create(parent);
        lv_obj_set_size(bar, 240, BAR_H);
        lv_obj_set_pos(bar, 62, y + 1);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x333333), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x00aa44), LV_PART_INDICATOR);

        s_rows[i].lbl = lbl;
        s_rows[i].bar = bar;
    }
}

void tab_soh_update(const struct BmuSnapshotC *snap)
{
    for (int i = 0; i < MAX_BATTERIES; i++) {
        uint8_t soh = snap->batteries[i].soh_pct;

        if (soh == 0xFF || snap->batteries[i].state == 0) {
            lv_label_set_text_fmt(s_rows[i].lbl, "B%d:--%% ", i);
            lv_bar_set_value(s_rows[i].bar, 0, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(s_rows[i].bar, lv_color_hex(0x333333), LV_PART_INDICATOR);
        } else {
            lv_label_set_text_fmt(s_rows[i].lbl, "B%d:%3d%%", i, (int)soh);
            lv_bar_set_value(s_rows[i].bar, soh, LV_ANIM_OFF);

            // Couleur selon niveau
            if (soh >= 70) {
                lv_obj_set_style_bg_color(s_rows[i].bar, lv_color_hex(0x00aa44), LV_PART_INDICATOR);
            } else if (soh >= 40) {
                lv_obj_set_style_bg_color(s_rows[i].bar, lv_color_hex(0xaa8800), LV_PART_INDICATOR);
            } else {
                lv_obj_set_style_bg_color(s_rows[i].bar, lv_color_hex(0xaa2222), LV_PART_INDICATOR);
            }
        }
    }
}
