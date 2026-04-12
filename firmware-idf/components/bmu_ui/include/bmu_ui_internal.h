// firmware-idf-v2/components/bmu_ui/include/bmu_ui_internal.h
//
// Interface interne entre bmu_ui.c et les tab_*.c

#pragma once
#include "lvgl.h"

struct BmuSnapshotC;

void tab_batt_create(lv_obj_t *parent);
void tab_batt_update(const struct BmuSnapshotC *snap);

void tab_soh_create(lv_obj_t *parent);
void tab_soh_update(const struct BmuSnapshotC *snap);

void tab_sys_create(lv_obj_t *parent);
void tab_sys_update(const struct BmuSnapshotC *snap);

void tab_climate_create(lv_obj_t *parent);
void tab_climate_update(void);

void tab_config_create(lv_obj_t *parent);
void tab_config_update(void);
