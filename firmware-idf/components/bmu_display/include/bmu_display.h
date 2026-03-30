#pragma once
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct bmu_protection_ctx_t;
struct bmu_battery_manager_t;

typedef struct {
    struct bmu_protection_ctx_t    *prot;
    struct bmu_battery_manager_t   *mgr;
    uint8_t                         nb_ina;
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
