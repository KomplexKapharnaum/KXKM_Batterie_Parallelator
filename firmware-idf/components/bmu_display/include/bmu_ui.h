#pragma once
#include "lvgl.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bmu_protection_ctx_t           *prot;
    bmu_battery_manager_t          *mgr;
    uint8_t                         nb_ina;
} bmu_ui_ctx_t;

// Each screen creates its content on the provided parent object
void bmu_ui_main_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx);
void bmu_ui_main_update(bmu_ui_ctx_t *ctx);

void bmu_ui_detail_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx, int battery_idx);
void bmu_ui_detail_update(bmu_ui_ctx_t *ctx, int battery_idx);

void bmu_ui_system_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx);
void bmu_ui_system_update(bmu_ui_ctx_t *ctx);

void bmu_ui_alerts_create(lv_obj_t *parent);
void bmu_ui_alerts_add(const char *timestamp, const char *message, lv_color_t color);

void bmu_ui_debug_create(lv_obj_t *parent);
void bmu_ui_debug_update(void);
void bmu_ui_debug_log(const char *msg);
void bmu_ui_debug_log_i2c_error(uint8_t addr, const char *type);
void bmu_ui_debug_set_device_count(int count);

#ifdef __cplusplus
}
#endif
