// firmware-idf-v2/components/bmu_ui/include/bmu_ui.h
//
// Phase 17 -- LVGL 5-tab display (BATT, SOH, SYS, CLIM, CONF).
// Ecran 320x240, ST7789 sur ESP32-S3-BOX-3, LVGL 9.x API.

#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

struct BmuCore;

// Cree le tabview 5 onglets sur l'ecran actif. Appeler une seule fois
// apres bsp_display_start() + esp_lvgl_port deja actif.
esp_err_t bmu_ui_init(void);

// Met a jour tous les onglets avec les donnees du core.
// Appeler periodiquement depuis task_ui (5 Hz).
void bmu_ui_update_data(struct BmuCore *core);

// Phase 19 : overlay passkey BLE SC pairing (6 chiffres).
// show_passkey cree un msgbox LVGL, hide_passkey le ferme.
// Thread-safe : doit etre appele depuis le contexte LVGL (ou via lv_async_call).
void bmu_ui_show_passkey(uint32_t passkey);
void bmu_ui_hide_passkey(void);

#ifdef __cplusplus
}
#endif
