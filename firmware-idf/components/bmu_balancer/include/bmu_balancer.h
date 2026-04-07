#pragma once

#include "esp_err.h"
#include "bmu_protection.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialiser le balancer. Appeler apres bmu_protection_init(). */
esp_err_t bmu_balancer_init(bmu_protection_ctx_t *prot);

/**
 * Tick du balancer — appeler une fois par cycle main loop (500ms).
 * Gere le duty-cycling et declenche les mesures R_int opportunistes.
 * @return nombre de batteries en cours d'equilibrage (OFF par duty)
 */
int bmu_balancer_tick(void);

/** Retourne true si la batterie idx est actuellement en phase OFF (duty) */
bool bmu_balancer_is_off(uint8_t idx);

/** Retourne le duty-cycle effectif (0-100%) pour une batterie */
int bmu_balancer_get_duty_pct(uint8_t idx);

#ifdef __cplusplus
}
#endif
