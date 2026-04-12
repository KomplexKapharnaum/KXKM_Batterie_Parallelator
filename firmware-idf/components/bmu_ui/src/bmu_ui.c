// firmware-idf-v2/components/bmu_ui/src/bmu_ui.c
//
// Phase 17 -- point d'entree UI : tabview 5 onglets + refresh periodique.

#include <string.h>

#include "bmu_ui.h"
#include "bmu_ui_internal.h"
#include "bmu_core.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "ui";

esp_err_t bmu_ui_init(void)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_screen_active();

    // Supprime le splash precedent
    lv_obj_clean(scr);

    lv_obj_t *tv = lv_tabview_create(scr);
    lv_tabview_set_tab_bar_size(tv, 28);

    lv_obj_t *t1 = lv_tabview_add_tab(tv, "BATT");
    lv_obj_t *t2 = lv_tabview_add_tab(tv, "SOH");
    lv_obj_t *t3 = lv_tabview_add_tab(tv, "SYS");
    lv_obj_t *t4 = lv_tabview_add_tab(tv, "CLIM");
    lv_obj_t *t5 = lv_tabview_add_tab(tv, "CONF");

    tab_batt_create(t1);
    tab_soh_create(t2);
    tab_sys_create(t3);
    tab_climate_create(t4);
    tab_config_create(t5);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "UI init complete (5 tabs)");
    return ESP_OK;
}

void bmu_ui_update_data(struct BmuCore *core)
{
    struct BmuSnapshotC snap;
    memset(&snap, 0, sizeof(snap));
    if (core) {
        bmu_core_get_cached_snapshot(core, &snap);
    }

    if (!lvgl_port_lock(10)) {
        return;
    }

    tab_batt_update(&snap);
    tab_soh_update(&snap);
    tab_sys_update(&snap);
    tab_climate_update();
    tab_config_update();

    lvgl_port_unlock();
}
