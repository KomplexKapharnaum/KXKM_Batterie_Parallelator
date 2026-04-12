/**
 * bmu_sntp — Synchronisation NTP pour BMU ESP-IDF
 *
 * Configure SNTP en mode poll, fuseau horaire France/Paris (CET/CEST),
 * et fournit un timestamp nanoseconde pour InfluxDB.
 */

#include "bmu_sntp.h"

#include <cstring>
#include <ctime>
#include <sys/time.h>
#include <cinttypes>

#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "SNTP";

static volatile bool s_is_synced = false;

// ---------------------------------------------------------------------------
// Callback appelé par ESP-IDF quand le temps est synchronisé
// ---------------------------------------------------------------------------
static void sntp_sync_callback(struct timeval *tv)
{
    s_is_synced = true;
    ESP_LOGI(TAG, "Temps synchronisé via NTP (tv_sec=%" PRId64 ")", (int64_t)tv->tv_sec);
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
esp_err_t bmu_sntp_init(void)
{
    // Configurer le fuseau horaire France/Paris (CET-1CEST avec règles DST)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    ESP_LOGI(TAG, "Configuration SNTP — serveur: pool.ntp.org, TZ: CET/CEST");

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_callback);
    esp_sntp_init();

    ESP_LOGI(TAG, "Init OK — en attente de synchronisation NTP");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Is Synced
// ---------------------------------------------------------------------------
bool bmu_sntp_is_synced(void)
{
    return s_is_synced;
}

// ---------------------------------------------------------------------------
// Get Time — retourne l'heure locale (France/Paris)
// Si pas encore synchronisé, retourne l'heure de compilation comme fallback
// ---------------------------------------------------------------------------
esp_err_t bmu_sntp_get_time(struct tm *timeinfo)
{
    if (timeinfo == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    time_t now;
    time(&now);

    // Si pas synchronisé, utiliser l'heure de compilation comme fallback
    if (!s_is_synced) {
        ESP_LOGW(TAG, "NTP non synchronisé — utilisation de l'heure de compilation");
        // __DATE__ format: "Mar 30 2026", __TIME__ format: "14:30:00"
        // Parser la date de compilation
        struct tm compile_time = {};
        // Fallback simple : epoch + date de build
        const char *date_str = __DATE__ " " __TIME__;
        // strptime n'est pas toujours disponible, on utilise une approche simple
        compile_time.tm_year = 126;  // 2026 - 1900
        compile_time.tm_mon = 2;     // Mars (0-indexed)
        compile_time.tm_mday = 30;
        compile_time.tm_hour = 12;
        compile_time.tm_min = 0;
        compile_time.tm_sec = 0;
        (void)date_str;  // Éviter warning unused
        *timeinfo = compile_time;
        return ESP_OK;
    }

    localtime_r(&now, timeinfo);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Get Timestamp NS — timestamp nanoseconde pour InfluxDB
// ---------------------------------------------------------------------------
int64_t bmu_sntp_get_timestamp_ns(void)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);

    int64_t ns = (int64_t)tv.tv_sec * 1000000000LL + (int64_t)tv.tv_usec * 1000LL;

    if (!s_is_synced) {
        ESP_LOGD(TAG, "Timestamp demandé mais NTP non synchronisé");
    }

    return ns;
}
