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
 * @brief Initialize display hardware (SPI + ILI9342C + touch + LVGL).
 * Creates a FreeRTOS task for LVGL tick + rendering.
 */
esp_err_t bmu_display_init(bmu_display_ctx_t *ctx);

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
