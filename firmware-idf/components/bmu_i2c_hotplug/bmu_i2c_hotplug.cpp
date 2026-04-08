#include "bmu_i2c_hotplug.h"
#include "bmu_i2c.h"
#include "bmu_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char *TAG = "HOTPLUG";

static bmu_hotplug_cfg_t   s_cfg = {};
static bmu_hotplug_stats_t s_stats = {};
static TaskHandle_t        s_task = NULL;
static bool                s_initialized = false;

esp_err_t bmu_hotplug_init(const bmu_hotplug_cfg_t *cfg)
{
    if (cfg == NULL || cfg->bus == NULL) return ESP_ERR_INVALID_ARG;
    s_cfg = *cfg;
    s_initialized = true;
    ESP_LOGI(TAG, "Hotplug initialized — interval %ds", CONFIG_BMU_I2C_HOTPLUG_INTERVAL_S);
    return ESP_OK;
}

static void hotplug_task(void *pv);

esp_err_t bmu_hotplug_start(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_task != NULL) return ESP_ERR_INVALID_STATE;

    BaseType_t ret = xTaskCreate(hotplug_task, "hotplug",
                                 CONFIG_BMU_I2C_HOTPLUG_STACK_SIZE,
                                 NULL, CONFIG_BMU_I2C_HOTPLUG_TASK_PRIORITY,
                                 &s_task);
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t bmu_hotplug_stop(void)
{
    if (s_task != NULL) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    return ESP_OK;
}

void bmu_hotplug_get_stats(bmu_hotplug_stats_t *stats)
{
    if (stats) *stats = s_stats;
}

/* ── Differential scan helpers ───────────────────────────────────── */

static bool device_still_present(uint8_t addr)
{
    return bmu_i2c_probe(s_cfg.bus, addr, pdMS_TO_TICKS(20)) == ESP_OK;
}

/**
 * @brief Check existing INA237 devices — count how many still respond.
 * Does NOT remove yet — just counts failures.
 */
static int count_ina_failures(void)
{
    int failures = 0;
    uint8_t cur = *s_cfg.nb_ina;
    for (int i = 0; i < cur; i++) {
        if (!device_still_present(s_cfg.ina_devices[i].addr)) {
            failures++;
        }
    }
    return failures;
}

/**
 * @brief Check existing TCA9535 devices — count how many still respond.
 */
static int count_tca_failures(void)
{
    int failures = 0;
    uint8_t cur = *s_cfg.nb_tca;
    for (int i = 0; i < cur; i++) {
        if (!device_still_present(s_cfg.tca_devices[i].addr)) {
            failures++;
        }
    }
    return failures;
}

/**
 * @brief Remove INA237 devices that no longer respond.
 * Forces battery OFF before removal (safety).
 * Compacts the array to maintain contiguous indexing.
 */
static int remove_gone_ina(void)
{
    int removed = 0;
    uint8_t cur = *s_cfg.nb_ina;

    for (int i = 0; i < cur; ) {
        if (device_still_present(s_cfg.ina_devices[i].addr)) {
            bmu_i2c_record_success();
            i++;
            continue;
        }

        ESP_LOGW(TAG, "INA237 @ 0x%02X GONE — removing slot %d",
                 s_cfg.ina_devices[i].addr, i);

        /* Safety: force battery OFF via its TCA before removal */
        int tca_idx = i / 4;
        int channel = i % 4;
        if (tca_idx < *s_cfg.nb_tca && s_cfg.tca_devices[tca_idx].dev) {
            bmu_tca9535_switch_battery(&s_cfg.tca_devices[tca_idx], channel, false);
            bmu_tca9535_set_led(&s_cfg.tca_devices[tca_idx], channel, true, false);
            ESP_LOGW(TAG, "Safety OFF: bat %d (TCA%d CH%d)", i + 1, tca_idx, channel);
        }

        /* Remove I2C device handle from bus */
        if (s_cfg.ina_devices[i].dev) {
            i2c_master_bus_rm_device(s_cfg.ina_devices[i].dev);
        }

        /* Compact: shift remaining entries left */
        for (int j = i; j < cur - 1; j++) {
            s_cfg.ina_devices[j] = s_cfg.ina_devices[j + 1];
        }
        memset(&s_cfg.ina_devices[cur - 1], 0, sizeof(bmu_ina237_t));
        cur--;
        removed++;
        /* Don't increment i — re-check the slot that shifted in */
    }

    *s_cfg.nb_ina = cur;
    return removed;
}

/**
 * @brief Remove TCA9535 devices that no longer respond.
 * Logs warning for affected batteries losing switch control.
 * Compacts the array.
 */
static int remove_gone_tca(void)
{
    int removed = 0;
    uint8_t cur = *s_cfg.nb_tca;

    for (int i = 0; i < cur; ) {
        if (device_still_present(s_cfg.tca_devices[i].addr)) {
            i++;
            continue;
        }

        ESP_LOGW(TAG, "TCA9535 @ 0x%02X GONE — removing slot %d",
                 s_cfg.tca_devices[i].addr, i);

        /* Warn about affected batteries losing switch control */
        for (int ch = 0; ch < BMU_TCA_CHANNELS_PER_DEVICE; ch++) {
            int bat_idx = i * BMU_TCA_CHANNELS_PER_DEVICE + ch;
            if (bat_idx < *s_cfg.nb_ina) {
                ESP_LOGW(TAG, "TCA removed: bat %d loses switch control", bat_idx + 1);
            }
        }

        if (s_cfg.tca_devices[i].dev) {
            i2c_master_bus_rm_device(s_cfg.tca_devices[i].dev);
        }

        for (int j = i; j < cur - 1; j++) {
            s_cfg.tca_devices[j] = s_cfg.tca_devices[j + 1];
        }
        memset(&s_cfg.tca_devices[cur - 1], 0, sizeof(bmu_tca9535_handle_t));
        cur--;
        removed++;
    }

    *s_cfg.nb_tca = cur;
    return removed;
}

/**
 * @brief Scan for new INA237 devices not in the current array.
 */
static int scan_new_ina(void)
{
    int added = 0;
    uint8_t cur = *s_cfg.nb_ina;

    for (uint8_t addr = INA237_ADDR_MIN; addr <= INA237_ADDR_MAX && cur < BMU_MAX_BATTERIES; addr++) {
        /* Skip addresses already known */
        bool known = false;
        for (int i = 0; i < cur; i++) {
            if (s_cfg.ina_devices[i].addr == addr) { known = true; break; }
        }
        if (known) continue;

        if (bmu_i2c_probe(s_cfg.bus, addr, pdMS_TO_TICKS(20)) != ESP_OK) continue;

        esp_err_t ret = bmu_ina237_init(s_cfg.bus, addr,
                                         INA237_SHUNT_RESISTANCE_UOHM, 10.0f,
                                         &s_cfg.ina_devices[cur]);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "NEW INA237 @ 0x%02X → slot %d", addr, cur);
            cur++;
            added++;
        } else {
            ESP_LOGW(TAG, "INA237 @ 0x%02X probe OK but init failed: %s",
                     addr, esp_err_to_name(ret));
        }
    }

    *s_cfg.nb_ina = cur;
    return added;
}

/**
 * @brief Scan for new TCA9535 devices not in the current array.
 */
static int scan_new_tca(void)
{
    int added = 0;
    uint8_t cur = *s_cfg.nb_tca;

    for (uint8_t addr = TCA9535_BASE_ADDR;
         addr < TCA9535_BASE_ADDR + TCA9535_MAX_DEVICES && cur < BMU_MAX_TCA;
         addr++) {
        bool known = false;
        for (int i = 0; i < cur; i++) {
            if (s_cfg.tca_devices[i].addr == addr) { known = true; break; }
        }
        if (known) continue;

        if (bmu_i2c_probe(s_cfg.bus, addr, pdMS_TO_TICKS(20)) != ESP_OK) continue;

        esp_err_t ret = bmu_tca9535_init(s_cfg.bus, addr, &s_cfg.tca_devices[cur]);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "NEW TCA9535 @ 0x%02X → slot %d", addr, cur);
            cur++;
            added++;
        } else {
            ESP_LOGW(TAG, "TCA9535 @ 0x%02X probe OK but init failed: %s",
                     addr, esp_err_to_name(ret));
        }
    }

    *s_cfg.nb_tca = cur;
    return added;
}

/**
 * @brief Propagate topology change to all dependent subsystems.
 */
static void propagate_topology(uint8_t new_nb_ina, uint8_t new_nb_tca)
{
    bmu_protection_update_topology(s_cfg.prot, new_nb_ina, new_nb_tca);
    bmu_battery_manager_update_nb_ina(s_cfg.mgr, new_nb_ina);

#ifdef CONFIG_BMU_BLE_ENABLED
    extern void bmu_ble_set_nb_ina(uint8_t nb_ina);
    bmu_ble_set_nb_ina(new_nb_ina);
#endif
}

/* ── Main hotplug task ───────────────────────────────────────────── */
static void hotplug_task(void *pv)
{
    const TickType_t period = pdMS_TO_TICKS(CONFIG_BMU_I2C_HOTPLUG_INTERVAL_S * 1000);

    ESP_LOGI(TAG, "Hotplug task started — period %ds", CONFIG_BMU_I2C_HOTPLUG_INTERVAL_S);

    for (;;) {
        vTaskDelay(period);

        uint8_t old_ina = *s_cfg.nb_ina;
        uint8_t old_tca = *s_cfg.nb_tca;

        /* ── Bus error quorum check ──────────────────────────────────
         * If ALL devices fail to respond, it's likely a bus error
         * (pull-up lost, cable disconnect, etc.), not individual removal.
         * In that case: trigger bus reset and skip this scan cycle.
         * Only proceed with individual removal if < 100% failure. */
        if (old_ina > 0 || old_tca > 0) {
            int ina_fails = count_ina_failures();
            int tca_fails = count_tca_failures();
            int total_devices = old_ina + old_tca;
            int total_fails = ina_fails + tca_fails;

            if (total_fails > 0 && total_fails == total_devices) {
                ESP_LOGW(TAG, "ALL %d devices unresponsive — bus error suspected, resetting",
                         total_devices);
                bmu_i2c_record_failure();
                s_stats.scan_count++;
                continue; /* Skip removal — retry next cycle */
            }
        }

        /* Phase 1: Remove devices that no longer respond */
        int rm_ina = remove_gone_ina();
        int rm_tca = remove_gone_tca();

        /* Phase 2: Discover new devices */
        int add_tca = scan_new_tca();
        int add_ina = scan_new_ina();

        uint8_t new_ina = *s_cfg.nb_ina;
        uint8_t new_tca = *s_cfg.nb_tca;

        /* Phase 3: Validate topology */
        bool new_topo = (new_ina > 0) && (new_tca > 0) && (new_tca * 4 == new_ina);

        /* Phase 4: Propagate if topology changed */
        bool changed = (new_ina != old_ina) || (new_tca != old_tca);
        if (changed) {
            s_stats.topo_changes++;
            ESP_LOGI(TAG, "Topology change: INA %d→%d, TCA %d→%d, valid=%s",
                     old_ina, new_ina, old_tca, new_tca, new_topo ? "YES" : "NO");
            propagate_topology(new_ina, new_tca);
        }

        *s_cfg.topology_ok = new_topo;

        s_stats.scan_count++;
        s_stats.last_nb_ina = new_ina;
        s_stats.last_nb_tca = new_tca;

        ESP_LOGD(TAG, "Scan #%lu: %d INA, %d TCA (rm=%d+%d, add=%d+%d)",
                 (unsigned long)s_stats.scan_count, new_ina, new_tca,
                 rm_ina, rm_tca, add_ina, add_tca);
    }
}
