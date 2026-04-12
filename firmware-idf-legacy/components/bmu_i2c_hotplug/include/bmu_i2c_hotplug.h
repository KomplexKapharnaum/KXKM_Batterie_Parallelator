#pragma once

#include "bmu_ina237.h"
#include "bmu_tca9535.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_types.h"
#include "esp_err.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_bus_handle_t  bus;
    bmu_ina237_t            *ina_devices;    /* static array [BMU_MAX_BATTERIES] */
    bmu_tca9535_handle_t    *tca_devices;    /* static array [BMU_MAX_TCA] */
    uint8_t                 *nb_ina;         /* pointer to live counter */
    uint8_t                 *nb_tca;         /* pointer to live counter */
    bool                    *topology_ok;    /* pointer to live flag */
    bmu_protection_ctx_t    *prot;
    bmu_battery_manager_t   *mgr;
    QueueHandle_t            q_cmd;
    SemaphoreHandle_t        nb_ina_mutex;  /* Optionnel: protège les writes sur *nb_ina */
} bmu_hotplug_cfg_t;

typedef struct {
    uint32_t scan_count;       /* total scans performed */
    uint32_t topo_changes;     /* topology changes detected */
    uint8_t  last_nb_ina;      /* nb_ina after last scan */
    uint8_t  last_nb_tca;      /* nb_tca after last scan */
} bmu_hotplug_stats_t;

/**
 * @brief Initialize hotplug task (does not start scanning).
 */
esp_err_t bmu_hotplug_init(const bmu_hotplug_cfg_t *cfg);

/**
 * @brief Start periodic scanning task.
 */
esp_err_t bmu_hotplug_start(void);

/**
 * @brief Stop periodic scanning task.
 */
esp_err_t bmu_hotplug_stop(void);

/**
 * @brief Get scan statistics.
 */
void bmu_hotplug_get_stats(bmu_hotplug_stats_t *stats);

#ifdef __cplusplus
}
#endif
