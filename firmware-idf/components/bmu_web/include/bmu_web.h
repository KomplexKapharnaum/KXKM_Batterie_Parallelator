#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations — evite d'inclure les headers lourds */
struct bmu_protection_ctx_t;
struct bmu_battery_manager_t;

typedef struct {
    struct bmu_protection_ctx_t    *prot;
    struct bmu_battery_manager_t   *mgr;
} bmu_web_ctx_t;

/**
 * @brief Démarre le serveur HTTP + WebSocket.
 *        Enregistre toutes les routes REST et le handler WS.
 */
esp_err_t bmu_web_start(bmu_web_ctx_t *ctx);

/**
 * @brief Arrête proprement le serveur HTTP et la tâche de push WS.
 */
esp_err_t bmu_web_stop(void);

#ifdef __cplusplus
}
#endif
