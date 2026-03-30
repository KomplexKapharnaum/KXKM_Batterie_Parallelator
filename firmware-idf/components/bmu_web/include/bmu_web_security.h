#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Vérifie si un token admin est configuré (non vide).
 * @return true si les mutations sont activées, false sinon.
 */
bool bmu_web_token_enabled(void);

/**
 * @brief Comparaison constante en temps du token fourni vs configuré.
 * @param provided  Token reçu dans la requête.
 * @return true si le token correspond, false sinon.
 */
bool bmu_web_token_valid(const char *provided);

/**
 * @brief Vérifie si le client dépasse le rate limit.
 * @param client_ip  Adresse IP du client (uint32).
 * @param now_ms     Timestamp courant en ms (esp_timer).
 * @return true si le rate limit est dépassé (requête à rejeter).
 */
bool bmu_web_rate_check(uint32_t client_ip, int64_t now_ms);

#ifdef __cplusplus
}
#endif
