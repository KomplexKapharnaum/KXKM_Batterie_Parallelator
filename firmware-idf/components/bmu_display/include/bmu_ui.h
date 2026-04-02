#pragma once
#include "lvgl.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_config.h"

/* ── High Contrast palette ──────────────────────────────────────── */
#define UI_COLOR_BG          lv_color_hex(0x000000)
#define UI_COLOR_CARD        lv_color_hex(0x0A0A0A)
#define UI_COLOR_CARD_ALT    lv_color_hex(0x111111)
#define UI_COLOR_TEXT         lv_color_hex(0xFFFFFF)
#define UI_COLOR_TEXT_SEC     lv_color_hex(0xCCCCCC)
#define UI_COLOR_TEXT_DIM     lv_color_hex(0x666666)
#define UI_COLOR_OK           lv_color_hex(0x00FF66)
#define UI_COLOR_WARN         lv_color_hex(0xFFAA00)
#define UI_COLOR_ERR          lv_color_hex(0xFF0044)
#define UI_COLOR_INFO         lv_color_hex(0x00AAFF)
#define UI_COLOR_SOH          lv_color_hex(0x00FFCC)
#define UI_COLOR_SOLAR        lv_color_hex(0xFFD600)
#define UI_COLOR_BG_ERR       lv_color_hex(0x0A0000)
#define UI_COLOR_BG_WARN      lv_color_hex(0x0A0800)
#define UI_COLOR_BORDER       lv_color_hex(0x222222)

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
    bmu_chart_history_t            *chart_hist; /* heap-allocated (PSRAM) */
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

void bmu_ui_detail_set_nav_state(bmu_nav_state_t *nav);
void bmu_ui_detail_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx, int battery_idx);
void bmu_ui_detail_update(bmu_ui_ctx_t *ctx, int battery_idx);
void bmu_ui_detail_destroy(void);

/* ── System screen ────────────────────────────────────────────────── */

void bmu_ui_system_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx);
void bmu_ui_system_update(bmu_ui_ctx_t *ctx);

/* ── Alerts screen ────────────────────────────────────────────────── */

typedef enum {
    ALERT_ERROR = 0,
    ALERT_WARNING,
    ALERT_INFO,
    ALERT_SYSTEM
} alert_type_t;

void bmu_ui_alerts_create(lv_obj_t *parent);
void bmu_ui_alerts_add(const char *timestamp, const char *message, lv_color_t color);
void bmu_ui_alerts_add_ex(const char *timestamp, const char *title,
                          const char *detail, alert_type_t type);
int  bmu_ui_alerts_get_count(void);

/* ── Debug ring buffer (impl in system.cpp) ───────────────────────── */

void        bmu_ui_debug_log(const char *msg);
void        bmu_ui_debug_log_i2c_error(uint8_t addr, const char *type);
void        bmu_ui_debug_set_device_count(int count);
const char *bmu_ui_debug_get_log_line(int index); /* 0=newest */
int         bmu_ui_debug_get_device_count(void);
int         bmu_ui_debug_get_error_count(void);

/* ── SOH prediction screen ───────────────────────────────────────── */

void bmu_ui_soh_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx);
void bmu_ui_soh_update(bmu_ui_ctx_t *ctx);

/* ── Config screen ───────────────────────────────────────────────── */

void bmu_ui_config_create(lv_obj_t *parent);
void bmu_ui_config_update(void);

#ifdef __cplusplus
}
#endif
