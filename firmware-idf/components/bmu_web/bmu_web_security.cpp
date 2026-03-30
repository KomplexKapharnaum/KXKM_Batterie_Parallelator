/*
 * bmu_web_security.cpp — Token auth + rate limiter pour BMU Web
 *
 * Port ESP-IDF des modules Arduino WebRouteSecurity + WebMutationRateLimit.
 * Corrections :
 *   - Comparaison constante en temps (timing-safe)
 *   - LRU eviction corrigée (audit MED-4 : on évince le slot le plus ancien,
 *     pas toujours le slot 0)
 */

#include "bmu_web_security.h"
#include "sdkconfig.h"

#include <cstring>
#include <cstdlib>

/* -------------------------------------------------------------------------- */
/*  Token auth                                                                */
/* -------------------------------------------------------------------------- */

static const char *s_configured_token = CONFIG_BMU_WEB_ADMIN_TOKEN;

bool bmu_web_token_enabled(void)
{
    return s_configured_token != nullptr && s_configured_token[0] != '\0';
}

/**
 * Comparaison constante en temps — empêche les attaques timing side-channel.
 * On parcourt toujours max(lenA, lenB) octets.
 */
static bool constant_time_equals(const char *a, const char *b)
{
    if (a == nullptr || b == nullptr) {
        return false;
    }

    const size_t len_a = std::strlen(a);
    const size_t len_b = std::strlen(b);
    const size_t max_len = (len_a > len_b) ? len_a : len_b;

    unsigned char diff = static_cast<unsigned char>(len_a ^ len_b);
    for (size_t i = 0; i < max_len; ++i) {
        const unsigned char ca = (i < len_a) ? static_cast<unsigned char>(a[i]) : 0;
        const unsigned char cb = (i < len_b) ? static_cast<unsigned char>(b[i]) : 0;
        diff |= static_cast<unsigned char>(ca ^ cb);
    }

    return diff == 0;
}

bool bmu_web_token_valid(const char *provided)
{
    if (!bmu_web_token_enabled()) {
        return false;
    }
    if (provided == nullptr || provided[0] == '\0') {
        return false;
    }
    return constant_time_equals(provided, s_configured_token);
}

/* -------------------------------------------------------------------------- */
/*  Rate limiter — avec LRU eviction (fix audit MED-4)                       */
/* -------------------------------------------------------------------------- */

typedef struct {
    uint32_t ip_key;
    uint32_t window_start_ms;
    uint8_t  request_count;
} rate_slot_t;

static rate_slot_t s_slots[CONFIG_BMU_WEB_MAX_RATE_SLOTS];

bool bmu_web_rate_check(uint32_t client_ip, uint32_t now_ms)
{
    const uint8_t  max_requests = CONFIG_BMU_WEB_RATE_MAX_REQUESTS;
    const uint32_t window_ms   = CONFIG_BMU_WEB_RATE_WINDOW_MS;

    if (max_requests == 0 || window_ms == 0) {
        return false;
    }

    int candidate = -1;
    int lru_idx   = -1;
    uint32_t oldest_start = UINT32_MAX;

    for (int i = 0; i < CONFIG_BMU_WEB_MAX_RATE_SLOTS; ++i) {
        /* Match existant */
        if (s_slots[i].ip_key == client_ip) {
            candidate = i;
            break;
        }
        /* Slot libre */
        if (candidate < 0 && s_slots[i].ip_key == 0) {
            candidate = i;
        }
        /* Suivi LRU : on retient le slot avec le window_start le plus ancien */
        if (s_slots[i].window_start_ms < oldest_start) {
            oldest_start = s_slots[i].window_start_ms;
            lru_idx = i;
        }
    }

    /* Pas de match ni de slot libre → LRU eviction (corrige MED-4) */
    if (candidate < 0) {
        candidate = (lru_idx >= 0) ? lru_idx : 0;
    }

    rate_slot_t &slot = s_slots[candidate];
    const bool new_window =
        (slot.ip_key != client_ip) || ((now_ms - slot.window_start_ms) > window_ms);

    if (new_window) {
        slot.ip_key          = client_ip;
        slot.window_start_ms = now_ms;
        slot.request_count   = 1;
        return false;
    }

    if (slot.request_count >= max_requests) {
        return true;   /* rate limit dépassé */
    }

    slot.request_count++;
    return false;
}
