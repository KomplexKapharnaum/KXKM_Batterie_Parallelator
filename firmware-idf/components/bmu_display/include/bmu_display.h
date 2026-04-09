#pragma once
#include "esp_err.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_types.h"

typedef struct _lv_obj_t lv_obj_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bmu_protection_ctx_t           *prot;
    bmu_battery_manager_t          *mgr;
    uint8_t                         nb_ina;
    QueueHandle_t                   q_snapshot;
    bmu_snapshot_t                  last_snap;
} bmu_display_ctx_t;

/**
 * @brief Initialize display hardware (SPI + ILI9342C + touch + LVGL) and
 * create the LVGL UI (tabs, screens, widgets). Does NOT start the periodic
 * update callback — call bmu_display_start_updates() after protection init.
 */
esp_err_t bmu_display_init(bmu_display_ctx_t *ctx);

/**
 * @brief Start the periodic UI update callback (100 ms). Must be called
 * AFTER bmu_protection_init() so the first tick sees the real battery count.
 */
esp_err_t bmu_display_start_updates(void);

/**
 * @brief Force display update (called from main loop or event).
 * Normally the display updates itself via its own task.
 */
void bmu_display_request_update(void);

/**
 * @brief Reset backlight dim timer (called on touch events).
 */
void bmu_display_wake(void);

/**
 * @brief Return the active screen container for UI code to add widgets.
 */
lv_obj_t *bmu_display_get_screen_container(void);

#ifdef __cplusplus
}
#endif
