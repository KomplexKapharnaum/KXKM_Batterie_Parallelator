// firmware-idf-v2/components/bmu_ui/src/tab_batt.c
//
// Phase 17 -- onglet BATT : grille 4x4 de cellules batterie.
// Chaque cellule affiche index, tension, courant, etat via couleur de fond.
// Ecran 320x240, zone tab ~320x212, cellule ~77x50.

#include <stdlib.h>
#include <string.h>

#include "bmu_ui_internal.h"
#include "bmu_core.h"

#define COLS 4
#define ROWS 4
#define CELL_W 77
#define CELL_H 50
#define GAP    2

// Couleurs par etat
static lv_color_t state_color(uint8_t state)
{
    switch (state) {
    case 1:  return lv_color_hex(0x226633); // Online  -- vert sombre
    case 2:  return lv_color_hex(0x666622); // PreCharging -- jaune sombre
    case 3:  return lv_color_hex(0x662222); // ForcedOff -- rouge sombre
    case 4:  return lv_color_hex(0x882222); // Fault -- rouge
    default: return lv_color_hex(0x333333); // Offline -- gris
    }
}

// Widgets par cellule
typedef struct {
    lv_obj_t *panel;
    lv_obj_t *lbl_id;
    lv_obj_t *lbl_v;
    lv_obj_t *lbl_i;
} batt_cell_t;

static batt_cell_t s_cells[MAX_BATTERIES];

void tab_batt_create(lv_obj_t *parent)
{
    // Desactiver le scroll par defaut du tab content
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < MAX_BATTERIES; i++) {
        int col = i % COLS;
        int row = i / COLS;

        lv_obj_t *panel = lv_obj_create(parent);
        lv_obj_set_size(panel, CELL_W, CELL_H);
        lv_obj_set_pos(panel, col * (CELL_W + GAP), row * (CELL_H + GAP));
        lv_obj_set_style_bg_color(panel, lv_color_hex(0x333333), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(panel, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_all(panel, 2, LV_PART_MAIN);
        lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_id = lv_label_create(panel);
        lv_label_set_text_fmt(lbl_id, "B%d", i);
        lv_obj_set_style_text_color(lbl_id, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_id, &lv_font_montserrat_10, LV_PART_MAIN);
        lv_obj_align(lbl_id, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *lbl_v = lv_label_create(panel);
        lv_label_set_text(lbl_v, "--.-V");
        lv_obj_set_style_text_color(lbl_v, lv_color_hex(0xcccccc), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_v, &lv_font_montserrat_10, LV_PART_MAIN);
        lv_obj_align(lbl_v, LV_ALIGN_LEFT_MID, 0, 2);

        lv_obj_t *lbl_i = lv_label_create(panel);
        lv_label_set_text(lbl_i, "OFF");
        lv_obj_set_style_text_color(lbl_i, lv_color_hex(0x999999), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_i, &lv_font_montserrat_10, LV_PART_MAIN);
        lv_obj_align(lbl_i, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        s_cells[i].panel  = panel;
        s_cells[i].lbl_id = lbl_id;
        s_cells[i].lbl_v  = lbl_v;
        s_cells[i].lbl_i  = lbl_i;
    }
}

void tab_batt_update(const struct BmuSnapshotC *snap)
{
    for (int i = 0; i < MAX_BATTERIES; i++) {
        const struct BmuBatteryC *b = &snap->batteries[i];
        batt_cell_t *c = &s_cells[i];

        // Couleur de fond selon etat
        lv_obj_set_style_bg_color(c->panel, state_color(b->state), LV_PART_MAIN);

        if (b->state == 0) {
            // Offline
            lv_label_set_text(c->lbl_v, "--.-V");
            lv_label_set_text(c->lbl_i, "OFF");
        } else {
            // Tension : integer math, eg 24523 mV -> "24.5V"
            int32_t v_abs = b->voltage_mv < 0 ? -b->voltage_mv : b->voltage_mv;
            int32_t v_int = v_abs / 1000;
            int32_t v_dec = (v_abs % 1000) / 100;
            lv_label_set_text_fmt(c->lbl_v, "%d.%dV", (int)v_int, (int)v_dec);

            // Courant : signe + valeur
            int32_t i_abs = b->current_ma < 0 ? -b->current_ma : b->current_ma;
            int32_t i_int = i_abs / 1000;
            int32_t i_dec = (i_abs % 1000) / 100;
            const char *sign = b->current_ma < 0 ? "-" : "+";
            lv_label_set_text_fmt(c->lbl_i, "%s%d.%dA", sign, (int)i_int, (int)i_dec);
        }
    }
}
