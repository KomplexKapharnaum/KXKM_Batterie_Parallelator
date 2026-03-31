# Phase 8: Display Dashboard Enhanced — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add tap-to-detail navigation, real-time V/I chart, and Solar VE.Direct tab to the BOX-3 LVGL display.

**Architecture:** The battery grid (tab Batt) gains tap-to-detail navigation: clicking a cell creates a detail overlay with lv_chart + numeric values + action buttons, replacing the grid content. A "Back" button destroys the detail and restores the grid. A 5th "Solar" tab reads VE.Direct data from the Victron MPPT charger. Chart history is stored in a static ring buffer sized by Kconfig. All UI follows the existing `bmu_ui_ctx_t` pattern with `bmu_display.cpp` orchestrating periodic updates.

**Tech Stack:** ESP-IDF v5.4, LVGL v9, lv_chart, bmu_vedirect

**Depends on:** Phase 7 display tabview (commit 9015472), bmu_vedirect component

---

## File Structure

```
firmware-idf/components/bmu_display/
├── CMakeLists.txt              # Modified — add bmu_ui_solar.cpp to SRCS, bmu_vedirect to PRIV_REQUIRES
├── Kconfig                     # Modified — add CONFIG_BMU_CHART_HISTORY_POINTS
├── include/
│   └── bmu_ui.h                # Modified — add solar + chart declarations
├── bmu_display.cpp             # Modified — 5th tab Solar, detail navigation state, periodic update routing
├── bmu_ui_main.cpp             # Modified — add LV_EVENT_CLICKED on cells
├── bmu_ui_detail.cpp           # Rewritten — chart + values + action buttons
└── bmu_ui_solar.cpp            # New — VE.Direct solar display
```

---

### Task 1: Add Kconfig + chart history ring buffer

**Files:**
- Modify: `firmware-idf/components/bmu_display/Kconfig`
- Modify: `firmware-idf/components/bmu_display/include/bmu_ui.h`

- [ ] **Step 1: Add CONFIG_BMU_CHART_HISTORY_POINTS to Kconfig**

Replace the entire `Kconfig` with:

```kconfig
menu "BMU Display"
    config BMU_DISPLAY_BACKLIGHT_GPIO
        int "Backlight GPIO"
        default 47
    config BMU_DISPLAY_BL_DIM_TIMEOUT_S
        int "Backlight dim timeout (seconds, 0=never)"
        default 30
    config BMU_DISPLAY_REFRESH_MS
        int "UI refresh period (ms)"
        default 100
    config BMU_CHART_HISTORY_POINTS
        int "Chart history ring buffer size (points per battery)"
        default 600
        help
            Number of data points kept per battery for the V/I chart.
            At 500ms sampling, 600 = 5 minutes of history.
            Memory cost: 600 * 8 bytes * 16 batteries = 76.8 KB.
endmenu
```

- [ ] **Step 2: Add chart ring buffer struct and solar declarations to bmu_ui.h**

Replace the entire `bmu_ui.h` with:

```cpp
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
```

---

### Task 2: Rewrite bmu_ui_detail.cpp — chart + values + action buttons

**Files:**
- Rewrite: `firmware-idf/components/bmu_display/bmu_ui_detail.cpp`

- [ ] **Step 1: Rewrite bmu_ui_detail.cpp**

Replace the entire file with:

```cpp
/**
 * @file bmu_ui_detail.cpp
 * @brief Ecran detail batterie — graphique V/I + valeurs + boutons actions.
 *
 * Cree dynamiquement au tap sur une cellule, detruit au retour.
 * Utilise lv_chart pour l'historique temps reel.
 */

#include "bmu_ui.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "esp_log.h"
#include <cstdio>

static const char *TAG = "UI_DETAIL";

/* ── Couleurs ─────────────────────────────────────────────────────── */

#define COL_BG          lv_color_hex(0x1E1E1E)
#define COL_CARD        lv_color_hex(0x2D2D2D)
#define COL_BLUE        lv_color_hex(0x2979FF)
#define COL_GREEN       lv_color_hex(0x00C853)
#define COL_RED         lv_color_hex(0xFF1744)
#define COL_ORANGE      lv_color_hex(0xFF9100)
#define COL_GREY        lv_color_hex(0x9E9E9E)
#define COL_WHITE       lv_color_white()
#define COL_CHART_V     lv_color_hex(0x42A5F5)  // bleu voltage
#define COL_CHART_I     lv_color_hex(0x66BB6A)  // vert courant

/* ── State statique ───────────────────────────────────────────────── */

static lv_obj_t *s_panel = NULL;         // panneau overlay principal
static lv_obj_t *s_chart = NULL;         // lv_chart widget
static lv_chart_series_t *s_ser_v = NULL; // serie tension
static lv_chart_series_t *s_ser_i = NULL; // serie courant
static lv_obj_t *s_val_labels[8] = {};    // labels valeurs numeriques
static int s_battery_idx = -1;
static bmu_ui_ctx_t *s_ctx_ref = NULL;
static bmu_nav_state_t *s_nav_ref = NULL;

/* ── Noms d'etats ─────────────────────────────────────────────────── */

static const char *state_name(bmu_battery_state_t s)
{
    switch (s) {
        case BMU_STATE_CONNECTED:    return "CONNECTED";
        case BMU_STATE_DISCONNECTED: return "DISCONNECTED";
        case BMU_STATE_RECONNECTING: return "RECONNECTING";
        case BMU_STATE_ERROR:        return "ERROR";
        case BMU_STATE_LOCKED:       return "LOCKED";
        default:                     return "UNKNOWN";
    }
}

static lv_color_t state_color(bmu_battery_state_t s)
{
    switch (s) {
        case BMU_STATE_CONNECTED:    return COL_GREEN;
        case BMU_STATE_DISCONNECTED: return COL_RED;
        case BMU_STATE_RECONNECTING: return COL_ORANGE;
        case BMU_STATE_ERROR:        return COL_RED;
        case BMU_STATE_LOCKED:       return COL_GREY;
        default:                     return COL_GREY;
    }
}

/* ── Callbacks boutons ────────────────────────────────────────────── */

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Retour a la grille depuis BAT %d", s_battery_idx + 1);
    bmu_ui_detail_destroy();
}

static void confirm_switch_on_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    const char *btn_text = lv_msgbox_get_active_button_text(mbox);
    if (btn_text && strcmp(btn_text, "Oui") == 0) {
        ESP_LOGI(TAG, "Switch ON BAT %d via display", s_battery_idx + 1);
        bmu_protection_web_switch(s_ctx_ref->prot, s_battery_idx, true);
    }
    lv_msgbox_close(mbox);
}

static void confirm_switch_off_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    const char *btn_text = lv_msgbox_get_active_button_text(mbox);
    if (btn_text && strcmp(btn_text, "Oui") == 0) {
        ESP_LOGI(TAG, "Switch OFF BAT %d via display", s_battery_idx + 1);
        bmu_protection_web_switch(s_ctx_ref->prot, s_battery_idx, false);
    }
    lv_msgbox_close(mbox);
}

static void confirm_reset_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    const char *btn_text = lv_msgbox_get_active_button_text(mbox);
    if (btn_text && strcmp(btn_text, "Oui") == 0) {
        ESP_LOGI(TAG, "Reset compteur BAT %d via display", s_battery_idx + 1);
        bmu_protection_reset_switch_count(s_ctx_ref->prot, s_battery_idx);
    }
    lv_msgbox_close(mbox);
}

static void switch_on_btn_cb(lv_event_t *e)
{
    (void)e;
    static const char *btns[] = {"Oui", "Non", ""};
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Confirmer");
    lv_msgbox_add_text(mbox, "Activer cette batterie ?");
    lv_msgbox_add_footer_button(mbox, "Oui");
    lv_msgbox_add_footer_button(mbox, "Non");
    lv_obj_set_style_bg_color(mbox, COL_CARD, 0);
    lv_obj_set_style_text_font(mbox, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(mbox, confirm_switch_on_cb, LV_EVENT_VALUE_CHANGED, mbox);
}

static void switch_off_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Confirmer");
    lv_msgbox_add_text(mbox, "Desactiver cette batterie ?");
    lv_msgbox_add_footer_button(mbox, "Oui");
    lv_msgbox_add_footer_button(mbox, "Non");
    lv_obj_set_style_bg_color(mbox, COL_CARD, 0);
    lv_obj_set_style_text_font(mbox, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(mbox, confirm_switch_off_cb, LV_EVENT_VALUE_CHANGED, mbox);
}

static void reset_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Confirmer");
    lv_msgbox_add_text(mbox, "Remettre le compteur a zero ?");
    lv_msgbox_add_footer_button(mbox, "Oui");
    lv_msgbox_add_footer_button(mbox, "Non");
    lv_obj_set_style_bg_color(mbox, COL_CARD, 0);
    lv_obj_set_style_text_font(mbox, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(mbox, confirm_reset_cb, LV_EVENT_VALUE_CHANGED, mbox);
}

/* ── Create ───────────────────────────────────────────────────────── */

void bmu_ui_detail_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx, int idx)
{
    if (idx < 0 || idx >= ctx->nb_ina) return;

    s_battery_idx = idx;
    s_ctx_ref = ctx;

    /* Panneau overlay couvrant tout le parent */
    s_panel = lv_obj_create(parent);
    lv_obj_set_size(s_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_panel, COL_BG, 0);
    lv_obj_set_style_border_width(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 4, 0);
    lv_obj_set_style_radius(s_panel, 0, 0);
    lv_obj_move_foreground(s_panel);

    /* ── Ligne du haut : Back + titre ─────────────────────────────── */
    lv_obj_t *btn_back = lv_button_create(s_panel);
    lv_obj_set_size(btn_back, 56, 22);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_back, COL_CARD, 0);
    lv_obj_t *btn_lbl = lv_label_create(btn_back);
    lv_label_set_text(btn_lbl, LV_SYMBOL_LEFT " Ret");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(btn_back, back_btn_cb, LV_EVENT_CLICKED, NULL);

    char title[16];
    snprintf(title, sizeof(title), "BAT %d", idx + 1);
    lv_obj_t *tlbl = lv_label_create(s_panel);
    lv_label_set_text(tlbl, title);
    lv_obj_set_style_text_color(tlbl, COL_BLUE, 0);
    lv_obj_set_style_text_font(tlbl, &lv_font_montserrat_14, 0);
    lv_obj_align(tlbl, LV_ALIGN_TOP_MID, 0, 2);

    /* ── Graphique V/I (moitie gauche) ────────────────────────────── */
    s_chart = lv_chart_create(s_panel);
    lv_obj_set_size(s_chart, 150, 90);
    lv_obj_align(s_chart, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, 60);  // affiche les 60 derniers points (30s)
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_color(s_chart, COL_GREY, 0);
    lv_obj_set_style_border_width(s_chart, 1, 0);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR); // pas de points

    lv_chart_set_div_line_count(s_chart, 3, 0);

    /* Serie tension (axe Y primaire) — bleu */
    s_ser_v = lv_chart_add_series(s_chart, COL_CHART_V, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 20000, 32000); // 20-32V en mV

    /* Serie courant (axe Y secondaire) — vert */
    s_ser_i = lv_chart_add_series(s_chart, COL_CHART_I, LV_CHART_AXIS_SECONDARY_Y);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_SECONDARY_Y, -5000, 30000); // -5A..30A en mA

    /* ── Valeurs numeriques (moitie droite) ───────────────────────── */
    const char *labels[] = {
        "V:", "I:", "P:", "T:",
        "Ah+:", "Ah-:", "Coup:", "Etat:"
    };
    for (int i = 0; i < 8; i++) {
        lv_obj_t *name = lv_label_create(s_panel);
        lv_label_set_text(name, labels[i]);
        lv_obj_set_style_text_color(name, COL_GREY, 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 158, 26 + i * 16);

        s_val_labels[i] = lv_label_create(s_panel);
        lv_label_set_text(s_val_labels[i], "---");
        lv_obj_set_style_text_color(s_val_labels[i], COL_WHITE, 0);
        lv_obj_set_style_text_font(s_val_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_align(s_val_labels[i], LV_ALIGN_TOP_LEFT, 200, 26 + i * 16);
    }

    /* ── Boutons actions (bas de l'ecran) ─────────────────────────── */
    lv_obj_t *btn_on = lv_button_create(s_panel);
    lv_obj_set_size(btn_on, 80, 26);
    lv_obj_align(btn_on, LV_ALIGN_BOTTOM_LEFT, 4, -2);
    lv_obj_set_style_bg_color(btn_on, COL_GREEN, 0);
    lv_obj_t *lbl_on = lv_label_create(btn_on);
    lv_label_set_text(lbl_on, "Switch ON");
    lv_obj_set_style_text_font(lbl_on, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_on, lv_color_black(), 0);
    lv_obj_center(lbl_on);
    lv_obj_add_event_cb(btn_on, switch_on_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_off = lv_button_create(s_panel);
    lv_obj_set_size(btn_off, 80, 26);
    lv_obj_align(btn_off, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_color(btn_off, COL_RED, 0);
    lv_obj_t *lbl_off = lv_label_create(btn_off);
    lv_label_set_text(lbl_off, "Switch OFF");
    lv_obj_set_style_text_font(lbl_off, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_off, COL_WHITE, 0);
    lv_obj_center(lbl_off);
    lv_obj_add_event_cb(btn_off, switch_off_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_reset = lv_button_create(s_panel);
    lv_obj_set_size(btn_reset, 80, 26);
    lv_obj_align(btn_reset, LV_ALIGN_BOTTOM_RIGHT, -4, -2);
    lv_obj_set_style_bg_color(btn_reset, COL_ORANGE, 0);
    lv_obj_t *lbl_rst = lv_label_create(btn_reset);
    lv_label_set_text(lbl_rst, "Reset");
    lv_obj_set_style_text_font(lbl_rst, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_rst, lv_color_black(), 0);
    lv_obj_center(lbl_rst);
    lv_obj_add_event_cb(btn_reset, reset_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Premiere mise a jour */
    bmu_ui_detail_update(ctx, idx);

    ESP_LOGI(TAG, "Detail cree pour BAT %d", idx + 1);
}

/* ── Update ───────────────────────────────────────────────────────── */

void bmu_ui_detail_update(bmu_ui_ctx_t *ctx, int idx)
{
    if (s_panel == NULL || idx < 0 || idx >= ctx->nb_ina) return;

    float v_mv = bmu_protection_get_voltage(ctx->prot, idx);
    float v = v_mv / 1000.0f;
    bmu_battery_state_t state = bmu_protection_get_state(ctx->prot, idx);
    float ah_d = bmu_battery_manager_get_ah_discharge(ctx->mgr, idx);
    float ah_c = bmu_battery_manager_get_ah_charge(ctx->mgr, idx);
    int nb_sw = ctx->prot->nb_switch[idx];

    /* Calculer courant et puissance depuis la tension et l'historique */
    bmu_chart_history_t *h = &ctx->chart_hist[idx];
    float i_a = 0.0f;
    if (h->count > 0) {
        int last = (h->head - 1 + CONFIG_BMU_CHART_HISTORY_POINTS) % CONFIG_BMU_CHART_HISTORY_POINTS;
        i_a = h->current_a[last];
    }
    float p_w = v * i_a;

    /* Valeurs numeriques */
    char buf[20];
    snprintf(buf, sizeof(buf), "%.2f V", v);
    lv_label_set_text(s_val_labels[0], buf);

    snprintf(buf, sizeof(buf), "%.2f A", i_a);
    lv_label_set_text(s_val_labels[1], buf);

    snprintf(buf, sizeof(buf), "%.1f W", p_w);
    lv_label_set_text(s_val_labels[2], buf);

    lv_label_set_text(s_val_labels[3], "---");  // Temperature — pas encore disponible

    snprintf(buf, sizeof(buf), "%.3f", ah_c);
    lv_label_set_text(s_val_labels[4], buf);

    snprintf(buf, sizeof(buf), "%.3f", ah_d);
    lv_label_set_text(s_val_labels[5], buf);

    snprintf(buf, sizeof(buf), "%d", nb_sw);
    lv_label_set_text(s_val_labels[6], buf);

    lv_label_set_text(s_val_labels[7], state_name(state));
    lv_obj_set_style_text_color(s_val_labels[7], state_color(state), 0);

    /* Mettre a jour le graphique avec les donnees du ring buffer */
    if (s_chart != NULL && h->count > 0) {
        int points = h->count < 60 ? h->count : 60;
        int start = (h->head - points + CONFIG_BMU_CHART_HISTORY_POINTS) % CONFIG_BMU_CHART_HISTORY_POINTS;

        for (int i = 0; i < 60; i++) {
            if (i < points) {
                int ri = (start + i) % CONFIG_BMU_CHART_HISTORY_POINTS;
                lv_chart_set_value_by_id(s_chart, s_ser_v, i, (int32_t)h->voltage_mv[ri]);
                lv_chart_set_value_by_id(s_chart, s_ser_i, i, (int32_t)(h->current_a[ri] * 1000.0f));
            } else {
                lv_chart_set_value_by_id(s_chart, s_ser_v, i, LV_CHART_POINT_NONE);
                lv_chart_set_value_by_id(s_chart, s_ser_i, i, LV_CHART_POINT_NONE);
            }
        }
        lv_chart_refresh(s_chart);
    }
}

/* ── Destroy ──────────────────────────────────────────────────────── */

void bmu_ui_detail_destroy(void)
{
    if (s_panel != NULL) {
        lv_obj_delete(s_panel);
        s_panel = NULL;
    }
    s_chart = NULL;
    s_ser_v = NULL;
    s_ser_i = NULL;
    for (int i = 0; i < 8; i++) s_val_labels[i] = NULL;

    /* Mettre a jour l'etat de navigation */
    if (s_nav_ref != NULL) {
        s_nav_ref->detail_visible = false;
        s_nav_ref->detail_battery = -1;
        s_nav_ref->detail_panel = NULL;
    }
    s_battery_idx = -1;
    ESP_LOGI(TAG, "Detail detruit, retour grille");
}
```

---

### Task 3: Add tap navigation in bmu_ui_main.cpp + bmu_display.cpp

**Files:**
- Modify: `firmware-idf/components/bmu_display/bmu_ui_main.cpp`
- Modify: `firmware-idf/components/bmu_display/bmu_display.cpp`

- [ ] **Step 1: Add click callback and nav state to bmu_ui_main.cpp**

Replace the entire `bmu_ui_main.cpp` with:

```cpp
#include "bmu_ui.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "esp_log.h"
#include <cstdio>

static const char *TAG = "UI_MAIN";

// Colors
#define COL_GREEN   lv_color_hex(0x00C853)
#define COL_RED     lv_color_hex(0xFF1744)
#define COL_ORANGE  lv_color_hex(0xFF9100)
#define COL_GREY    lv_color_hex(0x9E9E9E)
#define COL_BLUE    lv_color_hex(0x2979FF)
#define COL_BG      lv_color_hex(0x1E1E1E)
#define COL_CARD    lv_color_hex(0x2D2D2D)

static lv_obj_t *battery_cells[16] = {};
static lv_obj_t *voltage_labels[16] = {};
static lv_obj_t *current_labels[16] = {};
static lv_obj_t *status_indicators[16] = {};
static lv_obj_t *summary_label = NULL;
static lv_obj_t *s_grid_parent = NULL;
static bmu_ui_ctx_t *s_ctx_ref = NULL;
static bmu_nav_state_t *s_nav = NULL;

static lv_color_t state_color(bmu_battery_state_t state) {
    switch (state) {
        case BMU_STATE_CONNECTED:    return COL_GREEN;
        case BMU_STATE_DISCONNECTED: return COL_RED;
        case BMU_STATE_RECONNECTING: return COL_ORANGE;
        case BMU_STATE_ERROR:        return COL_RED;
        case BMU_STATE_LOCKED:       return COL_GREY;
        default:                     return COL_GREY;
    }
}

/* ── Callback tap sur cellule ─────────────────────────────────────── */

static void cell_clicked_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_nav == NULL || s_ctx_ref == NULL || s_grid_parent == NULL) return;
    if (s_nav->detail_visible) return; // deja en detail

    ESP_LOGI(TAG, "Tap sur BAT %d → detail", idx + 1);

    s_nav->detail_visible = true;
    s_nav->detail_battery = idx;

    /* Creer le detail en overlay sur le parent du tab */
    bmu_ui_detail_create(s_grid_parent, s_ctx_ref, idx);
}

void bmu_ui_main_set_nav_state(bmu_nav_state_t *nav)
{
    s_nav = nav;
}

void bmu_ui_main_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx)
{
    s_grid_parent = parent;
    s_ctx_ref = ctx;

    // Set dark background
    lv_obj_set_style_bg_color(parent, COL_BG, 0);

    // Header
    lv_obj_t *header = lv_label_create(parent);
    lv_label_set_text(header, "KXKM BMU");
    lv_obj_set_style_text_color(header, COL_BLUE, 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 4, 2);

    // Grid container — 4 columns
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_set_size(grid, 312, 200);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 2, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    int nb = ctx->nb_ina > 16 ? 16 : ctx->nb_ina;
    for (int i = 0; i < nb; i++) {
        // Cell card
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_set_size(cell, 74, 46);
        lv_obj_set_style_bg_color(cell, COL_CARD, 0);
        lv_obj_set_style_radius(cell, 4, 0);
        lv_obj_set_style_pad_all(cell, 2, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        battery_cells[i] = cell;

        /* Rendre la cellule clickable */
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cell, cell_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        // Battery number
        lv_obj_t *num = lv_label_create(cell);
        char buf[8];
        snprintf(buf, sizeof(buf), "B%d", i + 1);
        lv_label_set_text(num, buf);
        lv_obj_set_style_text_color(num, lv_color_white(), 0);
        lv_obj_set_style_text_font(num, &lv_font_montserrat_14, 0);
        lv_obj_align(num, LV_ALIGN_TOP_LEFT, 0, 0);

        // Status dot
        lv_obj_t *dot = lv_obj_create(cell);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, COL_GREY, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, 0, 0);
        status_indicators[i] = dot;

        // Voltage
        lv_obj_t *vlbl = lv_label_create(cell);
        lv_label_set_text(vlbl, "--.-V");
        lv_obj_set_style_text_color(vlbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(vlbl, &lv_font_montserrat_14, 0);
        lv_obj_align(vlbl, LV_ALIGN_BOTTOM_LEFT, 0, -10);
        voltage_labels[i] = vlbl;

        // Current
        lv_obj_t *ilbl = lv_label_create(cell);
        lv_label_set_text(ilbl, "-.-A");
        lv_obj_set_style_text_color(ilbl, COL_GREY, 0);
        lv_obj_set_style_text_font(ilbl, &lv_font_montserrat_14, 0);
        lv_obj_align(ilbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        current_labels[i] = ilbl;
    }

    // Summary bar
    summary_label = lv_label_create(parent);
    lv_label_set_text(summary_label, "...");
    lv_obj_set_style_text_color(summary_label, COL_GREY, 0);
    lv_obj_set_style_text_font(summary_label, &lv_font_montserrat_14, 0);
    lv_obj_align(summary_label, LV_ALIGN_BOTTOM_MID, 0, -2);
}

void bmu_ui_main_update(bmu_ui_ctx_t *ctx)
{
    /* Si le detail est visible, mettre a jour le detail, pas la grille */
    if (s_nav != NULL && s_nav->detail_visible) {
        bmu_ui_detail_update(ctx, s_nav->detail_battery);
        return;
    }

    int nb = ctx->nb_ina > 16 ? 16 : ctx->nb_ina;
    float total_i = 0;
    float sum_v = 0;
    int n_active = 0;

    for (int i = 0; i < nb; i++) {
        float v_mv = bmu_protection_get_voltage(ctx->prot, i);
        float v = v_mv / 1000.0f;
        bmu_battery_state_t state = bmu_protection_get_state(ctx->prot, i);

        char vbuf[12], ibuf[12];
        snprintf(vbuf, sizeof(vbuf), "%.1fV", v);
        lv_label_set_text(voltage_labels[i], vbuf);

        // Read current from battery manager
        float i_a = 0; // Would need INA read — simplified for now
        snprintf(ibuf, sizeof(ibuf), "%.1fA", i_a);
        lv_label_set_text(current_labels[i], ibuf);

        // Status dot color
        lv_obj_set_style_bg_color(status_indicators[i], state_color(state), 0);

        if (v > 1.0f) {
            sum_v += v;
            n_active++;
        }
    }

    // Summary
    float avg_v = n_active > 0 ? sum_v / n_active : 0;
    char summary[64];
    snprintf(summary, sizeof(summary), "Avg:%.1fV  Active:%d/%d", avg_v, n_active, nb);
    lv_label_set_text(summary_label, summary);
}
```

- [ ] **Step 2: Update bmu_display.cpp — add nav state, 5th tab, chart history push, solar update**

Replace the entire `bmu_display.cpp` with:

```cpp
/**
 * @file bmu_display.cpp
 * @brief Display init for ESP32-S3-BOX-3 — LVGL v9 tabview with 5 screens.
 *
 * Ecrans : Batteries | Systeme | Alertes | Debug I2C | Solar
 * Navigation : swipe horizontal ou tap sur les onglets en bas.
 * Tap sur une cellule batterie → ecran detail en overlay.
 */

#include "bmu_display.h"
#include "bmu_ui.h"
#include "bmu_vedirect.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char *TAG = "DISP";

static bmu_display_ctx_t *s_ctx = NULL;
static bmu_ui_ctx_t s_ui_ctx = {};
static bmu_nav_state_t s_nav = {};
static lv_display_t *s_disp = NULL;
static lv_obj_t *s_tabview = NULL;
static int64_t s_last_touch_us = 0;
static bool s_dimmed = false;
static bool s_update_req = false;
static bool s_ui_ready = false;
static esp_timer_handle_t s_periodic_timer = NULL;

/* Compteur pour le push chart (500ms = toutes les 5 iterations @ 100ms refresh) */
static int s_chart_push_counter = 0;
#define CHART_PUSH_INTERVAL  5  // 5 * 100ms = 500ms

/* ── Backlight ──────────────────────────────────────────────────────── */

static void backlight_set_full(void)
{
    bsp_display_brightness_set(100);
    bsp_display_backlight_on();
    s_dimmed = false;
}

static void backlight_check_dim(void)
{
    const uint32_t timeout_s = CONFIG_BMU_DISPLAY_BL_DIM_TIMEOUT_S;
    if (timeout_s == 0) return;

    const int64_t now = esp_timer_get_time();
    if (!s_dimmed && (now - s_last_touch_us) > (int64_t)timeout_s * 1000000LL) {
        bsp_display_brightness_set(12);
        s_dimmed = true;
    }
}

/* ── Chart history push (500ms) ────────────────────────────────────── */

static void chart_history_push_all(void)
{
    int nb = s_ui_ctx.nb_ina > 16 ? 16 : s_ui_ctx.nb_ina;
    for (int i = 0; i < nb; i++) {
        float v_mv = bmu_protection_get_voltage(s_ui_ctx.prot, i);
        float i_a = 0.0f; // TODO: lire depuis INA quand API disponible
        bmu_chart_history_push(&s_ui_ctx.chart_hist[i], v_mv, i_a);
    }
}

/* ── Periodic update (timer callback) ──────────────────────────────── */

static void display_periodic_cb(void *arg)
{
    (void)arg;
    backlight_check_dim();

    if (s_disp == NULL || !s_ui_ready) return;

    /* Push chart data toutes les 500ms */
    s_chart_push_counter++;
    if (s_chart_push_counter >= CHART_PUSH_INTERVAL) {
        s_chart_push_counter = 0;
        chart_history_push_all();
    }

    if (bsp_display_lock(0)) {
        /* bmu_ui_main_update gere : grille OU detail selon nav state */
        bmu_ui_main_update(&s_ui_ctx);
        bmu_ui_system_update(&s_ui_ctx);
        bmu_ui_debug_update();
        bmu_ui_solar_update();

        if (s_update_req) {
            s_update_req = false;
            lv_obj_invalidate(lv_scr_act());
        }
        bsp_display_unlock();
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t bmu_display_init(bmu_display_ctx_t *ctx)
{
    if (ctx == NULL) {
        ESP_LOGE(TAG, "Contexte display NULL");
        return ESP_ERR_INVALID_ARG;
    }

    s_ctx = ctx;
    s_ui_ctx.prot = ctx->prot;
    s_ui_ctx.mgr = ctx->mgr;
    s_ui_ctx.nb_ina = ctx->nb_ina;
    s_ui_ready = false;

    /* Init navigation state */
    s_nav.detail_visible = false;
    s_nav.detail_battery = -1;
    s_nav.detail_panel = NULL;

    /* Init chart history (zero) */
    memset(s_ui_ctx.chart_hist, 0, sizeof(s_ui_ctx.chart_hist));

    ESP_LOGI(TAG, "=== Initialisation affichage BMU via BSP BOX-3 ===");

    /* Init display via BSP */
    s_disp = bsp_display_start();
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "bsp_display_start a echoue");
        return ESP_FAIL;
    }

    if (!bsp_display_lock(0)) {
        ESP_LOGE(TAG, "Impossible de prendre le lock LVGL");
        return ESP_FAIL;
    }

    /* ── Dark background ──────────────────────────────────────────── */
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1E1E1E), 0);

    /* ── Tabview — onglets en bas, 5 ecrans ───────────────────────── */
    s_tabview = lv_tabview_create(lv_scr_act());
    lv_tabview_set_tab_bar_position(s_tabview, LV_DIR_BOTTOM);
    lv_tabview_set_tab_bar_size(s_tabview, 30);
    lv_obj_set_size(s_tabview, BSP_LCD_H_RES, BSP_LCD_V_RES);

    /* Style tabview : fond sombre, onglets discrets */
    lv_obj_set_style_bg_color(s_tabview, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(s_tabview, 0, 0);

    /* Style tab bar */
    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(s_tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(tab_bar, 0, 0);
    lv_obj_set_style_pad_all(tab_bar, 0, 0);

    /* Creer les 5 onglets */
    lv_obj_t *tab_batt   = lv_tabview_add_tab(s_tabview, LV_SYMBOL_CHARGE " Batt");
    lv_obj_t *tab_sys    = lv_tabview_add_tab(s_tabview, LV_SYMBOL_SETTINGS " Sys");
    lv_obj_t *tab_alerts = lv_tabview_add_tab(s_tabview, LV_SYMBOL_WARNING " Alert");
    lv_obj_t *tab_debug  = lv_tabview_add_tab(s_tabview, LV_SYMBOL_EYE_OPEN " I2C");
    lv_obj_t *tab_solar  = lv_tabview_add_tab(s_tabview, LV_SYMBOL_CHARGE " Solar");

    /* Fond sombre pour chaque onglet */
    lv_obj_set_style_bg_color(tab_batt,   lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_color(tab_sys,    lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_color(tab_alerts, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_color(tab_debug,  lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_color(tab_solar,  lv_color_hex(0x1E1E1E), 0);

    /* ── Creer le contenu de chaque ecran ─────────────────────────── */
    bmu_ui_main_set_nav_state(&s_nav);
    bmu_ui_main_create(tab_batt, &s_ui_ctx);
    bmu_ui_system_create(tab_sys, &s_ui_ctx);
    bmu_ui_alerts_create(tab_alerts);
    bmu_ui_debug_create(tab_debug);
    bmu_ui_solar_create(tab_solar);

    s_ui_ready = true;
    bsp_display_unlock();

    /* ── Touch wakeup ─────────────────────────────────────────────── */
    s_last_touch_us = esp_timer_get_time();
    backlight_set_full();

    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev != NULL) {
        lv_indev_add_event_cb(indev, [](lv_event_t *e) {
            (void)e;
            bmu_display_wake();
        }, LV_EVENT_PRESSING, NULL);
    }

    /* ── Periodic timer for updates ───────────────────────────────── */
    if (s_periodic_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = display_periodic_cb,
            .arg = NULL,
            .name = "disp_periodic",
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_periodic_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(
            s_periodic_timer,
            CONFIG_BMU_DISPLAY_REFRESH_MS * 1000ULL));
    }

    ESP_LOGI(TAG, "=== Affichage BMU pret (refresh=%dms, dim=%ds, chart=%d pts) ===",
             CONFIG_BMU_DISPLAY_REFRESH_MS,
             CONFIG_BMU_DISPLAY_BL_DIM_TIMEOUT_S,
             CONFIG_BMU_CHART_HISTORY_POINTS);
    return ESP_OK;
}

void bmu_display_request_update(void)
{
    s_update_req = true;
}

void bmu_display_wake(void)
{
    s_last_touch_us = esp_timer_get_time();
    if (s_dimmed) {
        backlight_set_full();
    }
}

lv_obj_t *bmu_display_get_screen_container(void)
{
    return s_tabview;
}
```

---

### Task 4: Create bmu_ui_solar.cpp + add 5th tab

**Files:**
- Create: `firmware-idf/components/bmu_display/bmu_ui_solar.cpp`
- Modify: `firmware-idf/components/bmu_display/CMakeLists.txt`

- [ ] **Step 1: Create bmu_ui_solar.cpp**

Create the new file `firmware-idf/components/bmu_display/bmu_ui_solar.cpp`:

```cpp
/**
 * @file bmu_ui_solar.cpp
 * @brief Ecran Solar — affiche les donnees VE.Direct du chargeur Victron MPPT.
 *
 * Lit bmu_vedirect_get_data() et affiche PV tension, puissance, etat MPPT,
 * yield, et infos chargeur. Si VE.Direct non connecte : "Pas de chargeur detecte".
 */

#include "bmu_ui.h"
#include "bmu_vedirect.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "UI_SOLAR";

/* ── Couleurs ─────────────────────────────────────────────────────── */

#define COL_BG          lv_color_hex(0x1E1E1E)
#define COL_CARD        lv_color_hex(0x2D2D2D)
#define COL_BLUE        lv_color_hex(0x2979FF)
#define COL_GREEN       lv_color_hex(0x00C853)
#define COL_ORANGE      lv_color_hex(0xFF9100)
#define COL_YELLOW      lv_color_hex(0xFFD600)
#define COL_RED         lv_color_hex(0xFF1744)
#define COL_GREY        lv_color_hex(0x9E9E9E)
#define COL_WHITE       lv_color_white()

/* ── Labels statiques ─────────────────────────────────────────────── */

static lv_obj_t *s_no_charger_label = NULL;
static lv_obj_t *s_data_container = NULL;

// Donnees PV
static lv_obj_t *s_pv_voltage = NULL;
static lv_obj_t *s_pv_power = NULL;
static lv_obj_t *s_charge_current = NULL;
static lv_obj_t *s_batt_voltage = NULL;

// Etat MPPT
static lv_obj_t *s_charge_state = NULL;

// Yield
static lv_obj_t *s_yield_today = NULL;
static lv_obj_t *s_yield_total = NULL;
static lv_obj_t *s_max_power = NULL;

// Infos chargeur
static lv_obj_t *s_product_id = NULL;
static lv_obj_t *s_serial = NULL;
static lv_obj_t *s_firmware = NULL;

/* ── Helpers ──────────────────────────────────────────────────────── */

static lv_color_t cs_color(uint8_t cs)
{
    switch (cs) {
        case 0:  return COL_GREY;    // Off
        case 3:  return COL_ORANGE;  // Bulk
        case 4:  return COL_YELLOW;  // Absorption
        case 5:  return COL_GREEN;   // Float
        case 7:  return COL_BLUE;    // Equalize
        default: return COL_GREY;
    }
}

static lv_obj_t *make_row(lv_obj_t *parent, const char *label_text, int y, lv_obj_t **value_out)
{
    lv_obj_t *name = lv_label_create(parent);
    lv_label_set_text(name, label_text);
    lv_obj_set_style_text_color(name, COL_GREY, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 4, y);

    *value_out = lv_label_create(parent);
    lv_label_set_text(*value_out, "---");
    lv_obj_set_style_text_color(*value_out, COL_WHITE, 0);
    lv_obj_set_style_text_font(*value_out, &lv_font_montserrat_14, 0);
    lv_obj_align(*value_out, LV_ALIGN_TOP_LEFT, 130, y);

    return name;
}

/* ── Create ───────────────────────────────────────────────────────── */

void bmu_ui_solar_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, COL_BG, 0);

    /* Titre */
    lv_obj_t *header = lv_label_create(parent);
    lv_label_set_text(header, LV_SYMBOL_CHARGE " Solar VE.Direct");
    lv_obj_set_style_text_color(header, COL_BLUE, 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 4, 2);

    /* Message "pas de chargeur" (cache par defaut si connecte) */
    s_no_charger_label = lv_label_create(parent);
    lv_label_set_text(s_no_charger_label, "Pas de chargeur detecte");
    lv_obj_set_style_text_color(s_no_charger_label, COL_ORANGE, 0);
    lv_obj_set_style_text_font(s_no_charger_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_no_charger_label, LV_ALIGN_CENTER, 0, 0);

    /* Conteneur donnees (cache si pas de chargeur) */
    s_data_container = lv_obj_create(parent);
    lv_obj_set_size(s_data_container, LV_PCT(100), LV_PCT(90));
    lv_obj_align(s_data_container, LV_ALIGN_TOP_LEFT, 0, 20);
    lv_obj_set_style_bg_opa(s_data_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_data_container, 0, 0);
    lv_obj_set_style_pad_all(s_data_container, 0, 0);

    /* Colonne gauche : donnees PV */
    int y = 0;
    make_row(s_data_container, "PV tension:",  y,      &s_pv_voltage);     y += 18;
    make_row(s_data_container, "PV puiss.:",   y,      &s_pv_power);       y += 18;
    make_row(s_data_container, "I charge:",    y,      &s_charge_current); y += 18;
    make_row(s_data_container, "V batterie:",  y,      &s_batt_voltage);   y += 18;
    make_row(s_data_container, "Etat MPPT:",   y,      &s_charge_state);   y += 18;

    /* Separateur visuel */
    y += 4;

    /* Yield */
    make_row(s_data_container, "Yield jour:",  y,      &s_yield_today);    y += 18;
    make_row(s_data_container, "Yield total:", y,      &s_yield_total);    y += 18;
    make_row(s_data_container, "Pmax jour:",   y,      &s_max_power);      y += 18;

    /* Infos chargeur */
    y += 4;
    make_row(s_data_container, "Product ID:",  y,      &s_product_id);     y += 18;
    make_row(s_data_container, "Serial:",      y,      &s_serial);         y += 18;
    make_row(s_data_container, "Firmware:",    y,      &s_firmware);

    ESP_LOGI(TAG, "Ecran Solar cree");
}

/* ── Update ───────────────────────────────────────────────────────── */

void bmu_ui_solar_update(void)
{
    bool connected = bmu_vedirect_is_connected();

    if (!connected) {
        lv_obj_remove_flag(s_no_charger_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_data_container, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_add_flag(s_no_charger_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_data_container, LV_OBJ_FLAG_HIDDEN);

    const bmu_vedirect_data_t *d = bmu_vedirect_get_data();
    if (d == NULL || !d->valid) return;

    char buf[32];

    snprintf(buf, sizeof(buf), "%.1f V", d->panel_voltage_v);
    lv_label_set_text(s_pv_voltage, buf);

    snprintf(buf, sizeof(buf), "%d W", d->panel_power_w);
    lv_label_set_text(s_pv_power, buf);

    snprintf(buf, sizeof(buf), "%.1f A", d->battery_current_a);
    lv_label_set_text(s_charge_current, buf);

    snprintf(buf, sizeof(buf), "%.2f V", d->battery_voltage_v);
    lv_label_set_text(s_batt_voltage, buf);

    /* Etat charge avec couleur */
    const char *cs_name = bmu_vedirect_cs_name(d->charge_state);
    lv_label_set_text(s_charge_state, cs_name);
    lv_obj_set_style_text_color(s_charge_state, cs_color(d->charge_state), 0);

    /* Yield */
    snprintf(buf, sizeof(buf), "%lu Wh", (unsigned long)d->yield_today_wh);
    lv_label_set_text(s_yield_today, buf);

    snprintf(buf, sizeof(buf), "%.2f kWh", d->yield_total_wh / 1000.0f);
    lv_label_set_text(s_yield_total, buf);

    snprintf(buf, sizeof(buf), "%d W", d->max_power_today_w);
    lv_label_set_text(s_max_power, buf);

    /* Infos chargeur */
    lv_label_set_text(s_product_id, d->product_id);
    lv_label_set_text(s_serial, d->serial);
    lv_label_set_text(s_firmware, d->firmware);
}
```

- [ ] **Step 2: Update CMakeLists.txt — add bmu_ui_solar.cpp and bmu_vedirect dependency**

Replace the entire `CMakeLists.txt` with:

```cmake
idf_component_register(
    SRCS "bmu_display.cpp" "bmu_ui_main.cpp" "bmu_ui_detail.cpp" "bmu_ui_system.cpp" "bmu_ui_alerts.cpp" "bmu_ui_debug.cpp" "bmu_ui_solar.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp-box-3 esp_timer
    PRIV_REQUIRES bmu_protection bmu_config bmu_wifi bmu_mqtt bmu_storage bmu_sntp bmu_vedirect esp_app_format
)
```

---

## Memory Budget

| Item | Size | Notes |
|------|------|-------|
| Chart ring buffer (16 batteries) | 76.8 KB | `600 * 8 bytes * 16` |
| LVGL chart widget (detail) | ~2 KB | Created/destroyed dynamically |
| Solar UI labels | ~1 KB | Static, always allocated |
| Nav state | 16 bytes | Static struct |
| **Total delta** | **~80 KB** | Well within ESP32-S3 16MB budget |

## Verification Checklist

After implementation, verify:
- [ ] `idf.py build` succeeds with no warnings in display component
- [ ] Tab bar shows 5 tabs: Batt / Sys / Alert / I2C / Solar
- [ ] Tap on battery cell shows detail overlay with chart + values
- [ ] Back button returns to grid without memory leak (check heap before/after)
- [ ] Switch ON/OFF buttons show confirmation dialog before action
- [ ] Solar tab shows "Pas de chargeur detecte" when VE.Direct disconnected
- [ ] Solar tab shows live data when VE.Direct connected
- [ ] `scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85` passes
