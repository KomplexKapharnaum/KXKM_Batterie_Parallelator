// firmware-idf-v2/components/bmu_ui/src/tab_config.c
//
// Phase 17 -- onglet CONF : affichage statique des parametres de config.
// Pas de bmu_core_get_config(), valeurs hardcodees a la creation.

#include <string.h>
#include <stdio.h>

#include "bmu_ui_internal.h"
#include "bmu_ui.h"

static lv_obj_t *s_lbl_config = NULL;

/* ---- Phase 19 : passkey overlay ---- */
static lv_obj_t *s_passkey_mbox = NULL;

void tab_config_create(lv_obj_t *parent)
{
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(parent, 4, LV_PART_MAIN);

    s_lbl_config = lv_label_create(parent);
    lv_obj_set_style_text_font(s_lbl_config, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_config, lv_color_hex(0xcccccc), LV_PART_MAIN);
    lv_obj_align(s_lbl_config, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_width(s_lbl_config, 310);

    // Valeurs statiques correspondant a main.cpp init_bmu_core()
    lv_label_set_text(s_lbl_config,
        "--- BMU Config ---\n"
        "Umin:  24.0 V\n"
        "Umax:  30.0 V\n"
        "Imax:  1000 mA\n"
        "Vdiff: 1000 mV\n"
        "Switch max: 5\n"
        "Reconnect: 10 s\n"
        "Tick: 200 ms\n"
        "\n"
        "SoH model: fpnn_v3_int8"
    );
}

void tab_config_update(void)
{
    // Config statique, rien a mettre a jour.
    // Quand bmu_core_get_config() sera disponible, on
    // pourra afficher les valeurs dynamiques ici.
}

/* ---- Phase 19 : passkey overlay pour BLE SC pairing ---- */

void bmu_ui_show_passkey(uint32_t passkey)
{
    /* Si deja affiche, fermer l'ancien d'abord */
    bmu_ui_hide_passkey();

    char text[64];
    snprintf(text, sizeof(text), "BLE Passkey\n\n%06lu", (unsigned long)passkey);

    /* LVGL 9 msgbox: parent=NULL -> overlay sur ecran actif */
    s_passkey_mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(s_passkey_mbox, "BLE Pairing");
    lv_msgbox_add_text(s_passkey_mbox, text);

    /* Style du texte principal : grande police pour lisibilite a distance */
    lv_obj_t *content = lv_msgbox_get_content(s_passkey_mbox);
    if (content) {
        lv_obj_set_style_text_font(content, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_align(content, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }

    lv_obj_set_size(s_passkey_mbox, 240, 160);
    lv_obj_center(s_passkey_mbox);
}

void bmu_ui_hide_passkey(void)
{
    if (s_passkey_mbox != NULL) {
        lv_msgbox_close(s_passkey_mbox);
        s_passkey_mbox = NULL;
    }
}
