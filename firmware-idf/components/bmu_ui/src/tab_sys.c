// firmware-idf-v2/components/bmu_ui/src/tab_sys.c
//
// Phase 17 -- onglet SYS : uptime, heap, WiFi, MQTT, topology, n_bat, tick.

#include <string.h>
#include <inttypes.h>

#include "bmu_ui_internal.h"
#include "bmu_core.h"
#include "bmu_wifi.h"
#include "bmu_mqtt.h"
#include "esp_system.h"

static lv_obj_t *s_lbl_info = NULL;

void tab_sys_create(lv_obj_t *parent)
{
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(parent, 4, LV_PART_MAIN);

    s_lbl_info = lv_label_create(parent);
    lv_label_set_text(s_lbl_info, "SYS: loading...");
    lv_obj_set_style_text_font(s_lbl_info, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_info, lv_color_hex(0xcccccc), LV_PART_MAIN);
    lv_obj_align(s_lbl_info, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_width(s_lbl_info, 310);
}

void tab_sys_update(const struct BmuSnapshotC *snap)
{
    if (!s_lbl_info) return;

    uint64_t uptime_s = snap->system.monotonic_us / 1000000ULL;
    uint32_t up_h  = (uint32_t)(uptime_s / 3600);
    uint32_t up_m  = (uint32_t)((uptime_s % 3600) / 60);
    uint32_t up_s  = (uint32_t)(uptime_s % 60);

    uint32_t heap_kb = (uint32_t)(esp_get_free_heap_size() / 1024);

    bool wifi = bmu_wifi_is_connected();
    bool mqtt = bmu_mqtt_is_connected();

    lv_label_set_text_fmt(s_lbl_info,
        "Uptime: %"PRIu32"h%02"PRIu32"m%02"PRIu32"s\n"
        "Heap: %"PRIu32" kB free\n"
        "WiFi: %s\n"
        "MQTT: %s\n"
        "Topo: %s\n"
        "Batteries: %d / %d\n"
        "Tick p50: %"PRIu32" us\n"
        "Tick p99: %"PRIu32" us\n"
        "WDT feeds: %"PRIu32,
        up_h, up_m, up_s,
        heap_kb,
        wifi ? "CONNECTED" : "---",
        mqtt ? "CONNECTED" : "---",
        snap->system.topology_ok ? "OK" : "FAIL",
        (int)snap->n_bat, MAX_BATTERIES,
        snap->system.tick_us_p50,
        snap->system.tick_us_p99,
        snap->system.wdt_feeds
    );
}
