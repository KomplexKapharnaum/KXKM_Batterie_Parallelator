/* Stub — SOH disabled (TFLite build issues) */
#include "bmu_soh.h"

esp_err_t bmu_soh_init(void) { return ESP_ERR_NOT_SUPPORTED; }

float bmu_soh_predict(bmu_battery_manager_t *mgr, bmu_protection_ctx_t *prot,
                      int battery_index) {
    (void)mgr; (void)prot; (void)battery_index;
    return -1.0f;
}

void bmu_soh_update_all(bmu_battery_manager_t *mgr, bmu_protection_ctx_t *prot,
                        int nb_ina) {
    (void)mgr; (void)prot; (void)nb_ina;
}

float bmu_soh_get_cached(int idx) {
    (void)idx;
    return -1.0f;
}
