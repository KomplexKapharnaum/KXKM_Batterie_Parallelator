#pragma once
#include "lvgl.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Chart history ring buffer ────────────────────────────────────── */

typedef struct {
    float voltage_mv[CONFIG_BMU_CHART_HISTORY_POINTS];
    float current_a[CONFIG_BMU_CHART_HISTORY_POINTS];
    int   head;         // next write index
    int   count;        // number of valid points (0..CONFIG_BMU_CHART_HISTORY_POINTS)
} bmu_chart_history_t;

/* ── UI context ───────────────────────────────────────────────────── */

typedef struct {
    bmu_protection_ctx_t           *prot;
    bmu_battery_manager_t          *mgr;
    uint8_t                         nb_ina;
    bmu_chart_history_t             chart_hist[BMU_MAX_BATTERIES];
} bmu_ui_ctx_t;

/* ── Navigation state (managed by bmu_display.cpp) ────────────────── */

typedef struct {
    bool    detail_visible;   // true = detail overlay shown, false = grid shown
    int     detail_battery;   // battery index shown in detail (-1 = none)
    lv_obj_t *detail_panel;   // the detail overlay object (NULL when hidden)
} bmu_nav_state_t;

/* ── Chart history helpers ────────────────────────────────────────── */

static inline void bmu_chart_history_push(bmu_chart_history_t *h, float v_mv, float i_a)
{
    h->voltage_mv[h->head] = v_mv;
    h->current_a[h->head]  = i_a;
    h->head = (h->head + 1) % CONFIG_BMU_CHART_HISTORY_POINTS;
    if (h->count < CONFIG_BMU_CHART_HISTORY_POINTS) h->count++;
}

/* ── Main grid screen ─────────────────────────────────────────────── */

void bmu_ui_main_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx);
void bmu_ui_main_update(bmu_ui_ctx_t *ctx);
void bmu_ui_main_set_nav_state(bmu_nav_state_t *nav);

/* ── Detail screen ────────────────────────────────────────────────── */

void bmu_ui_detail_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx, int battery_idx);
void bmu_ui_detail_update(bmu_ui_ctx_t *ctx, int battery_idx);
void bmu_ui_detail_destroy(void);

/* ── System screen ────────────────────────────────────────────────── */

void bmu_ui_system_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx);
void bmu_ui_system_update(bmu_ui_ctx_t *ctx);

/* ── Alerts screen ────────────────────────────────────────────────── */

void bmu_ui_alerts_create(lv_obj_t *parent);
void bmu_ui_alerts_add(const char *timestamp, const char *message, lv_color_t color);

/* ── Debug I2C screen ─────────────────────────────────────────────── */

void bmu_ui_debug_create(lv_obj_t *parent);
void bmu_ui_debug_update(void);
void bmu_ui_debug_log(const char *msg);
void bmu_ui_debug_log_i2c_error(uint8_t addr, const char *type);
void bmu_ui_debug_set_device_count(int count);

/* ── Solar VE.Direct screen ───────────────────────────────────────── */

void bmu_ui_solar_create(lv_obj_t *parent);
void bmu_ui_solar_update(void);

#ifdef __cplusplus
}
#endif
