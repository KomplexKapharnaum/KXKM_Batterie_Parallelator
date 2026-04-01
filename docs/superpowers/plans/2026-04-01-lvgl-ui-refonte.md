# LVGL UI Refonte Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refonte complète de l'interface LVGL du BMU ESP32-S3-BOX-3 : 5 onglets high contrast, top bar stats, liste batteries avec barres, SOH IA, système compact, alertes avec badge, config NVS runtime.

**Architecture:** Réécriture des 8 fichiers UI existants dans `firmware-idf/components/bmu_display/`. Palette high contrast noir pur (#000). Tab bar 5 onglets en bas avec icônes LVGL (LV_SYMBOL_*). Chaque écran est un fichier .cpp indépendant avec create()/update(). Le display controller (bmu_display.cpp) orchestre le tout.

**Tech Stack:** ESP-IDF v5.4, LVGL v9.1.0, lv_font_montserrat_14, ESP32-S3-BOX-3 BSP

**Spec:** `docs/superpowers/specs/2026-04-01-lvgl-ui-refonte-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `bmu_display.cpp` | Rewrite | Display init, 5-tab tabview, periodic timer, backlight, chart push with real INA current |
| `include/bmu_ui.h` | Modify | Add color constants, config screen declarations, update types |
| `bmu_ui_main.cpp` | Rewrite | Top bar (stats) + battery list with progress bars + tap handler |
| `bmu_ui_detail.cpp` | Rewrite | Detail overlay: chart auto-scale, real current, confirmation dialogs, nav fix |
| `bmu_ui_soh.cpp` | Rewrite | SOH screen: mean bar + per-battery bars with color thresholds |
| `bmu_ui_system.cpp` | Rewrite | 5 compact sections: connexion, climat, solar, firmware, I2C |
| `bmu_ui_alerts.cpp` | Rewrite | Scrollable alert list with badge counter, incremental update |
| `bmu_ui_config.cpp` | Create | Config screen: device name, WiFi, thresholds steppers, MQTT, BLE toggle, brightness slider, save button |
| `bmu_ui_solar.cpp` | Delete | Merged into bmu_ui_system.cpp |
| `bmu_ui_debug.cpp` | Delete | Merged into bmu_ui_system.cpp (compact I2C section) |

---

### Task 1: Color palette + header update

**Files:**
- Modify: `firmware-idf/components/bmu_display/include/bmu_ui.h`

- [ ] **Step 1: Add color constants and config screen declarations to bmu_ui.h**

Add after the existing includes:

```c
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
```

Add config screen declarations (after existing screen declarations):

```c
/* Config screen */
void bmu_ui_config_create(lv_obj_t *parent);
void bmu_ui_config_update(void);
```

- [ ] **Step 2: Build to verify header compiles**

```bash
cd firmware-idf && export IDF_PATH=$HOME/esp/esp-idf && . $IDF_PATH/export.sh && idf.py build 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/components/bmu_display/include/bmu_ui.h
git commit -m "feat(ui): add high contrast color palette + config declarations"
```

---

### Task 2: Display controller — 5 tabs + real current

**Files:**
- Modify: `firmware-idf/components/bmu_display/bmu_display.cpp`

- [ ] **Step 1: Rewrite bmu_display.cpp**

Key changes from current code:
- Background: `UI_COLOR_BG` (black pure) instead of 0x1E1E1E
- Tab bar: 0x0A0A0A instead of 0x121212
- 5 tabs instead of 6: BATT, SOH, SYS, ALERT, CONFIG
- Remove Solar and Debug tabs (merged into SYS)
- Tab labels use LV_SYMBOL constants: LV_SYMBOL_CHARGE " Batt", LV_SYMBOL_EYE_OPEN " SOH", LV_SYMBOL_SETTINGS " Sys", LV_SYMBOL_WARNING " Alert", LV_SYMBOL_EDIT " Config"
- Chart push: read real current via `bmu_ina237_read_current()` instead of hardcoded 0.0f
- Add `#include "bmu_climate.h"` for temperature in top bar

Replace the tab creation section:

```c
/* Creer les 5 onglets */
lv_obj_t *tab_batt   = lv_tabview_add_tab(s_tabview, LV_SYMBOL_CHARGE " Batt");
lv_obj_t *tab_soh    = lv_tabview_add_tab(s_tabview, LV_SYMBOL_EYE_OPEN " SOH");
lv_obj_t *tab_sys    = lv_tabview_add_tab(s_tabview, LV_SYMBOL_SETTINGS " Sys");
lv_obj_t *tab_alerts = lv_tabview_add_tab(s_tabview, LV_SYMBOL_WARNING " Alert");
lv_obj_t *tab_config = lv_tabview_add_tab(s_tabview, LV_SYMBOL_EDIT " Config");

/* Fond noir pour chaque onglet */
lv_obj_t *tabs[] = {tab_batt, tab_soh, tab_sys, tab_alerts, tab_config};
for (int i = 0; i < 5; i++) {
    lv_obj_set_style_bg_color(tabs[i], UI_COLOR_BG, 0);
}
```

Replace the chart push to read real current:

```c
static void chart_history_push_all(void)
{
    int nb = s_ui_ctx.nb_ina > 16 ? 16 : s_ui_ctx.nb_ina;
    for (int i = 0; i < nb; i++) {
        float v_mv = bmu_protection_get_voltage(s_ui_ctx.prot, i);
        float i_a = 0.0f;
        bmu_ina237_read_current(&s_ui_ctx.mgr->ina_devices[i], &i_a);
        bmu_chart_history_push(&s_ui_ctx.chart_hist[i], v_mv, i_a);
    }
}
```

Replace the screen creation calls:

```c
bmu_ui_main_set_nav_state(&s_nav);
bmu_ui_main_create(tab_batt, &s_ui_ctx);
bmu_ui_soh_create(tab_soh);
bmu_ui_system_create(tab_sys, &s_ui_ctx);
bmu_ui_alerts_create(tab_alerts);
bmu_ui_config_create(tab_config);
```

Replace the periodic update:

```c
bmu_ui_main_update(&s_ui_ctx);
bmu_ui_soh_update();
bmu_ui_system_update(&s_ui_ctx);
/* alerts update on demand only */
bmu_ui_config_update();
```

- [ ] **Step 2: Build to verify**

```bash
idf.py build 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/components/bmu_display/bmu_display.cpp
git commit -m "feat(ui): 5-tab layout + real INA current + black theme"
```

---

### Task 3: Battery dashboard — top bar + list

**Files:**
- Rewrite: `firmware-idf/components/bmu_display/bmu_ui_main.cpp`

- [ ] **Step 1: Rewrite bmu_ui_main.cpp**

Complete rewrite. Key layout:

**Top bar (fixed, ~55px):**
- Line 1: device name (blue) + connection indicators (BLE/WiFi/MQTT dots)
- Line 2: 5 stat tiles (V MOY, I TOTAL, Ah IN, Ah OUT, CLIMAT)

**Battery list (scrollable):**
- Each row: border-left (3px color) + "B1" + progress bar + "26.4V" + "1.2A"
- Bar width = (voltage - 24000) / (30000 - 24000) * 100%, clamped 0-100
- Colors: green OK, red OFF/error, orange warning (voltage near threshold)
- Background: red-dark for OFF batteries, black for others
- Tap callback: set nav state + create detail overlay

**Top bar data sources:**
- V MOY: average of connected battery voltages
- I TOTAL: sum of all currents (from chart history last point)
- Ah IN / Ah OUT: sum of `bmu_battery_manager_get_ah_charge/discharge()`
- CLIMAT: `bmu_climate_get_temperature()` / `bmu_climate_get_humidity()`
- Connection: `bmu_wifi_is_connected()`, `bmu_mqtt_is_connected()`, BLE from config

**Progress bar implementation:**
Use `lv_bar` widget with custom colors. Map voltage to percentage:
```c
int pct = (int)((v_mv - 24000.0f) / 6000.0f * 100.0f);
if (pct < 0) pct = 0;
if (pct > 100) pct = 100;
lv_bar_set_value(bar, pct, LV_ANIM_OFF);
```

Bar color by state:
```c
if (state == BMU_STATE_CONNECTED) {
    lv_obj_set_style_bg_color(bar, UI_COLOR_OK, LV_PART_INDICATOR);
} else if (state == BMU_STATE_DISCONNECTED || state == BMU_STATE_LOCKED) {
    lv_obj_set_style_bg_color(bar, UI_COLOR_ERR, LV_PART_INDICATOR);
} else {
    lv_obj_set_style_bg_color(bar, UI_COLOR_WARN, LV_PART_INDICATOR);
}
```

Static widgets arrays for up to 16 batteries:
```c
static lv_obj_t *s_bat_rows[16];
static lv_obj_t *s_bat_bars[16];
static lv_obj_t *s_bat_vlabels[16];
static lv_obj_t *s_bat_ilabels[16];
static lv_obj_t *s_bat_borders[16];
```

Top bar stat labels:
```c
static lv_obj_t *s_vmoy_label, *s_itot_label, *s_ahin_label, *s_ahout_label, *s_climate_label;
static lv_obj_t *s_ble_dot, *s_wifi_dot, *s_mqtt_dot;
static lv_obj_t *s_device_name_label;
```

- [ ] **Step 2: Build**

```bash
idf.py build 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/components/bmu_display/bmu_ui_main.cpp
git commit -m "feat(ui): battery list with progress bars + top bar stats"
```

---

### Task 4: Detail overlay — chart auto-scale + confirmation

**Files:**
- Rewrite: `firmware-idf/components/bmu_display/bmu_ui_detail.cpp`

- [ ] **Step 1: Rewrite bmu_ui_detail.cpp**

Key fixes from spec:
1. **s_nav_ref initialization**: pass nav state pointer from main via `bmu_ui_main_set_nav_state()`
2. **Auto-scale chart**: compute min/max from chart history data, add 10% margin
3. **Real current**: read from chart_hist instead of hardcoded 0
4. **Confirmation dialog**: `lv_msgbox_create()` before switch ON/OFF

Auto-scale implementation:
```c
static void update_chart_range(bmu_chart_history_t *hist, lv_chart_t *chart)
{
    float v_min = 32000, v_max = 20000;
    for (int i = 0; i < hist->count; i++) {
        float v = hist->voltage_mv[i];
        if (v > 0 && v < v_min) v_min = v;
        if (v > v_max) v_max = v;
    }
    float margin = (v_max - v_min) * 0.1f;
    if (margin < 500) margin = 500;
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y,
                       (int)(v_min - margin), (int)(v_max + margin));
}
```

Confirmation dialog:
```c
static void switch_confirm_cb(lv_event_t *e)
{
    lv_obj_t *msgbox = lv_event_get_current_target(e);
    const char *btn_txt = lv_msgbox_get_active_btn_text(msgbox);
    if (btn_txt == NULL) return;

    if (strcmp(btn_txt, "Oui") == 0) {
        /* Proceed with switch */
        bmu_protection_web_switch(s_prot, s_detail_idx, s_switch_action);
        ESP_LOGI(TAG, "Switch bat[%d] -> %s (confirmed)", s_detail_idx,
                 s_switch_action ? "ON" : "OFF");
    }
    lv_msgbox_close(msgbox);
}

static void show_switch_confirm(bool on)
{
    s_switch_action = on;
    static const char *btns[] = {"Oui", "Non", ""};
    char msg[64];
    snprintf(msg, sizeof(msg), "%s batterie %d ?",
             on ? "Connecter" : "Deconnecter", s_detail_idx + 1);
    lv_obj_t *msgbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(msgbox, on ? "Switch ON" : "Switch OFF");
    lv_msgbox_add_text(msgbox, msg);
    lv_msgbox_add_close_button(msgbox);
    lv_obj_t *btn_yes = lv_msgbox_add_footer_button(msgbox, "Oui");
    lv_obj_t *btn_no = lv_msgbox_add_footer_button(msgbox, "Non");
    lv_obj_add_event_cb(btn_yes, switch_confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_no, [](lv_event_t *e) {
        lv_msgbox_close(lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target(e))));
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_center(msgbox);
}
```

Temperature from AHT30:
```c
float temp = bmu_climate_get_temperature();
if (bmu_climate_is_available()) {
    snprintf(buf, sizeof(buf), "%.1f C", temp);
} else {
    snprintf(buf, sizeof(buf), "---");
}
lv_label_set_text(s_temp_label, buf);
```

- [ ] **Step 2: Build**

```bash
idf.py build 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/components/bmu_display/bmu_ui_detail.cpp
git commit -m "fix(ui): detail auto-scale + real current + confirm dialogs"
```

---

### Task 5: SOH screen

**Files:**
- Rewrite: `firmware-idf/components/bmu_display/bmu_ui_soh.cpp`

- [ ] **Step 1: Rewrite bmu_ui_soh.cpp**

Layout:
- Header: "State of Health" (cyan) + timestamp label
- Mean SOH card: large percentage + bar (cyan border)
- Per-battery list: bars with color thresholds
- Legend: 3 color dots at bottom

Color logic:
```c
static lv_color_t soh_color(float soh_pct)
{
    if (soh_pct >= 70.0f) return UI_COLOR_OK;
    if (soh_pct >= 40.0f) return UI_COLOR_WARN;
    return lv_color_hex(0xFF3333);
}
```

Static widgets:
```c
static lv_obj_t *s_soh_mean_label;
static lv_obj_t *s_soh_mean_bar;
static lv_obj_t *s_soh_bars[16];
static lv_obj_t *s_soh_pct_labels[16];
static lv_obj_t *s_soh_warn_labels[16];
static lv_obj_t *s_timestamp_label;
```

Update reads from `bmu_soh_get_cached(i)`, computes mean of valid entries.

- [ ] **Step 2: Build**

```bash
idf.py build 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/components/bmu_display/bmu_ui_soh.cpp
git commit -m "feat(ui): SOH screen with color-coded health bars"
```

---

### Task 6: System screen — 5 compact sections

**Files:**
- Rewrite: `firmware-idf/components/bmu_display/bmu_ui_system.cpp`

- [ ] **Step 1: Rewrite bmu_ui_system.cpp**

Merges old system + solar + debug screens into 5 compact sections.

Each section = container with:
- Section label (gray, 9px equivalent at montserrat_14 with LV_OPA_50)
- Data on the right

Sections:
1. **CONNEXION**: BLE dot + WiFi dot+IP + MQTT dot
2. **CLIMAT**: temp + humidity from `bmu_climate_get_*()`
3. **SOLAIRE**: MPPT state + PV voltage + power + yield (hidden if not connected)
4. **FIRMWARE**: version + heap + uptime + INA/TCA count
5. **I2C BUS**: device count + error count + 3 last log lines

Data sources:
- `bmu_wifi_is_connected()`, `bmu_wifi_get_ip()`
- `bmu_mqtt_is_connected()`
- `bmu_climate_get_temperature()`, `bmu_climate_get_humidity()`
- `bmu_vedirect_is_connected()`, `bmu_vedirect_get_data()`
- `esp_get_free_heap_size()`, `esp_timer_get_time()`
- `bmu_ui_debug_get_device_count()`, `bmu_ui_debug_get_error_count()`

For I2C log lines, keep the existing debug ring buffer but expose a getter:
```c
/* In bmu_ui.h */
const char *bmu_ui_debug_get_log_line(int index); /* 0=newest */
int bmu_ui_debug_get_device_count(void);
int bmu_ui_debug_get_error_count(void);
```

- [ ] **Step 2: Build**

```bash
idf.py build 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/components/bmu_display/bmu_ui_system.cpp
git commit -m "feat(ui): compact system screen — 5 sections"
```

---

### Task 7: Alerts screen with badge

**Files:**
- Rewrite: `firmware-idf/components/bmu_display/bmu_ui_alerts.cpp`

- [ ] **Step 1: Rewrite bmu_ui_alerts.cpp**

Key improvements:
- Border-left color per alert type (red/orange/green/blue)
- Background color per type (dark-red, dark-orange, dark, dark)
- Incremental add (don't rebuild entire list)
- Badge counter exposed for tab bar
- CLEAR button top-right

Alert entry with richer structure:
```c
typedef enum {
    ALERT_ERROR = 0,
    ALERT_WARNING,
    ALERT_INFO,
    ALERT_SYSTEM
} alert_type_t;

typedef struct {
    char text[80];
    char detail[80];
    char timestamp[12];
    alert_type_t type;
} alert_entry_t;
```

Badge counter API:
```c
/* In bmu_ui.h */
int bmu_ui_alerts_get_count(void);
void bmu_ui_alerts_add_ex(const char *timestamp, const char *title,
                          const char *detail, alert_type_t type);
```

Colors by type:
```c
static lv_color_t alert_border_color(alert_type_t t) {
    switch (t) {
        case ALERT_ERROR:   return UI_COLOR_ERR;
        case ALERT_WARNING: return UI_COLOR_WARN;
        case ALERT_INFO:    return UI_COLOR_OK;
        case ALERT_SYSTEM:  return UI_COLOR_INFO;
    }
    return UI_COLOR_TEXT_DIM;
}
```

- [ ] **Step 2: Build**

```bash
idf.py build 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/components/bmu_display/bmu_ui_alerts.cpp
git commit -m "feat(ui): alert list with border colors + badge counter"
```

---

### Task 8: Config screen — NVS runtime

**Files:**
- Create: `firmware-idf/components/bmu_display/bmu_ui_config.cpp`

- [ ] **Step 1: Create bmu_ui_config.cpp**

Scrollable container with sections:

**Device Name:**
```c
static lv_obj_t *s_name_textarea;
/* Create textarea + keyboard trigger */
s_name_textarea = lv_textarea_create(section);
lv_textarea_set_text(s_name_textarea, bmu_config_get_device_name());
lv_textarea_set_one_line(s_name_textarea, true);
lv_textarea_set_max_length(s_name_textarea, 31);
```

**WiFi SSID/Password:**
Same pattern with two textareas. Password field uses `lv_textarea_set_password_mode(ta, true)`.

**Seuils Protection — Steppers:**
For each threshold, create a row with [−] value [+]:
```c
static void create_stepper(lv_obj_t *parent, const char *label_text,
                           int *value, int min_val, int max_val, int step,
                           lv_obj_t **value_label)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 24);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_width(lbl, 50);

    lv_obj_t *btn_minus = lv_btn_create(row);
    lv_obj_set_size(btn_minus, 28, 20);
    lv_obj_set_style_bg_color(btn_minus, UI_COLOR_CARD_ALT, 0);
    lv_obj_t *minus_lbl = lv_label_create(btn_minus);
    lv_label_set_text(minus_lbl, LV_SYMBOL_MINUS);
    lv_obj_center(minus_lbl);

    *value_label = lv_label_create(row);
    lv_obj_set_style_text_color(*value_label, UI_COLOR_TEXT, 0);

    lv_obj_t *btn_plus = lv_btn_create(row);
    lv_obj_set_size(btn_plus, 28, 20);
    lv_obj_set_style_bg_color(btn_plus, UI_COLOR_CARD_ALT, 0);
    lv_obj_t *plus_lbl = lv_label_create(btn_plus);
    lv_label_set_text(plus_lbl, LV_SYMBOL_PLUS);
    lv_obj_center(plus_lbl);

    /* Callbacks update the value and label */
}
```

**BLE Toggle:**
```c
lv_obj_t *sw = lv_switch_create(row);
lv_obj_set_style_bg_color(sw, UI_COLOR_OK, LV_PART_INDICATOR | LV_STATE_CHECKED);
```

**Brightness Slider:**
```c
lv_obj_t *slider = lv_slider_create(row);
lv_slider_set_range(slider, 0, 100);
lv_slider_set_value(slider, 70, LV_ANIM_OFF);
lv_obj_set_style_bg_color(slider, UI_COLOR_SOLAR, LV_PART_INDICATOR);
```

**Save Button:**
```c
lv_obj_t *save_btn = lv_btn_create(container);
lv_obj_set_size(save_btn, lv_pct(100), 32);
lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x00CC44), 0);
lv_obj_t *save_lbl = lv_label_create(save_btn);
lv_label_set_text(save_lbl, LV_SYMBOL_SAVE " SAUVEGARDER");
lv_obj_set_style_text_color(save_lbl, lv_color_hex(0x000000), 0);
lv_obj_center(save_lbl);
```

Save callback writes all values via `bmu_config_set_*()`.

- [ ] **Step 2: Add to CMakeLists**

In `firmware-idf/components/bmu_display/CMakeLists.txt`, add `bmu_ui_config.cpp` to SRCS.

- [ ] **Step 3: Build**

```bash
idf.py build 2>&1 | tail -5
```

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_display/bmu_ui_config.cpp \
        firmware-idf/components/bmu_display/CMakeLists.txt
git commit -m "feat(ui): config screen — NVS settings with steppers"
```

---

### Task 9: Remove old files + update CMakeLists

**Files:**
- Delete: `firmware-idf/components/bmu_display/bmu_ui_solar.cpp`
- Delete: `firmware-idf/components/bmu_display/bmu_ui_debug.cpp`
- Modify: `firmware-idf/components/bmu_display/CMakeLists.txt`

- [ ] **Step 1: Remove old files**

```bash
git rm firmware-idf/components/bmu_display/bmu_ui_solar.cpp
git rm firmware-idf/components/bmu_display/bmu_ui_debug.cpp
```

- [ ] **Step 2: Update CMakeLists.txt**

The SRCS list should now be:
```cmake
idf_component_register(
    SRCS "bmu_display.cpp"
         "bmu_ui_main.cpp"
         "bmu_ui_detail.cpp"
         "bmu_ui_soh.cpp"
         "bmu_ui_system.cpp"
         "bmu_ui_alerts.cpp"
         "bmu_ui_config.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp-box-3 esp_timer
    PRIV_REQUIRES bmu_protection bmu_config bmu_wifi bmu_mqtt
                  bmu_storage bmu_sntp bmu_vedirect bmu_climate
                  bmu_soh esp_app_format
)
```

Note: debug ring buffer functions (`bmu_ui_debug_log`, etc.) need to be moved to bmu_ui_system.cpp or a shared static file. Keep the ring buffer in system.cpp since it's the only consumer now.

- [ ] **Step 3: Build**

```bash
idf.py build 2>&1 | tail -5
```

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_display/CMakeLists.txt
git commit -m "refactor(ui): remove solar+debug files, merged into system"
```

---

### Task 10: Integration build + flash test

**Files:** None (verification only)

- [ ] **Step 1: Clean build**

```bash
cd firmware-idf && rm -rf build
export IDF_PATH=$HOME/esp/esp-idf && . $IDF_PATH/export.sh
idf.py set-target esp32s3 && idf.py build 2>&1 | tail -10
```

Expected: `Project build complete`, binary < 2 MB

- [ ] **Step 2: Flash to device**

```bash
idf.py -p /dev/cu.usbmodem3101 flash
```

- [ ] **Step 3: Verify on hardware**

Check:
- 5 tabs visible with icons at bottom
- BATT tab: top bar shows stats, battery list with bars
- SOH tab: health bars with colors
- SYS tab: 5 compact sections
- ALERT tab: list with colors
- CONFIG tab: steppers work, save button writes NVS
- Tap battery → detail overlay with chart
- Detail: Switch ON/OFF shows confirmation dialog
- Backlight dims after timeout, touch wakes

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat(ui): LVGL refonte complete — 5 tabs high contrast"
```
