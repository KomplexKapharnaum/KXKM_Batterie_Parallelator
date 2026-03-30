/**
 * bmu_influx — Client HTTP InfluxDB v2 pour BMU ESP-IDF
 *
 * Bufferise les lignes line-protocol et les envoie par POST
 * vers l'API /api/v2/write d'InfluxDB.
 */

#include "bmu_influx.h"

#include <cstdio>
#include <cstring>
#include <cinttypes>

#include "esp_log.h"
#include "esp_http_client.h"
#include "sdkconfig.h"

static const char *TAG = "INFLUX";

// --- Buffer statique pour les lignes line-protocol ---
static constexpr size_t BUFFER_MAX_BYTES = 4096;
static char s_buffer[BUFFER_MAX_BYTES];
static size_t s_buffer_len = 0;
static int s_buffer_lines = 0;

// URL complète construite à l'init
static char s_write_url[256];

// Token d'authentification formaté
static char s_auth_header[256];

// Indicateur d'initialisation
static bool s_initialized = false;

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
esp_err_t bmu_influx_init(void)
{
    // Avertissement sécurité si HTTP (pas HTTPS)
    const char *url = CONFIG_BMU_INFLUX_URL;
    if (strncmp(url, "http://", 7) == 0) {
        ESP_LOGW(TAG, "URL InfluxDB utilise HTTP non chiffré — HTTPS recommandé (audit)");
    }

    // Construire l'URL d'écriture
    int ret = snprintf(s_write_url, sizeof(s_write_url),
                       "%s/api/v2/write?org=%s&bucket=%s&precision=ns",
                       CONFIG_BMU_INFLUX_URL,
                       CONFIG_BMU_INFLUX_ORG,
                       CONFIG_BMU_INFLUX_BUCKET);
    if (ret < 0 || (size_t)ret >= sizeof(s_write_url)) {
        ESP_LOGE(TAG, "URL trop longue pour le buffer interne");
        return ESP_ERR_NO_MEM;
    }

    // Construire le header Authorization
    ret = snprintf(s_auth_header, sizeof(s_auth_header),
                   "Token %s", CONFIG_BMU_INFLUX_TOKEN);
    if (ret < 0 || (size_t)ret >= sizeof(s_auth_header)) {
        ESP_LOGE(TAG, "Token trop long pour le buffer interne");
        return ESP_ERR_NO_MEM;
    }

    if (strlen(CONFIG_BMU_INFLUX_TOKEN) == 0) {
        ESP_LOGW(TAG, "Aucun token InfluxDB configuré — les écritures échoueront probablement");
    }

    s_buffer_len = 0;
    s_buffer_lines = 0;
    s_initialized = true;

    ESP_LOGI(TAG, "Init OK — url=%s, org=%s, bucket=%s, buffer=%d lignes",
             CONFIG_BMU_INFLUX_URL, CONFIG_BMU_INFLUX_ORG,
             CONFIG_BMU_INFLUX_BUCKET, CONFIG_BMU_INFLUX_BUFFER_SIZE);

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Flush — envoie le buffer vers InfluxDB par HTTP POST
// ---------------------------------------------------------------------------
esp_err_t bmu_influx_flush(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_buffer_len == 0) {
        return ESP_OK;  // Rien à envoyer
    }

    esp_http_client_config_t config = {};
    config.url = s_write_url;
    config.method = HTTP_METHOD_POST;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Impossible de créer le client HTTP");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Authorization", s_auth_header);
    esp_http_client_set_header(client, "Content-Type", "text/plain");
    esp_http_client_set_post_field(client, s_buffer, (int)s_buffer_len);

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Échec POST InfluxDB: %s (non bloquant)", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "InfluxDB a répondu HTTP %d (non bloquant)", status);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Flush OK — %d lignes envoyées, HTTP %d", s_buffer_lines, status);

    // Réinitialiser le buffer
    s_buffer_len = 0;
    s_buffer_lines = 0;

    esp_http_client_cleanup(client);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Write — ajoute une ligne au buffer, flush automatique si plein
// ---------------------------------------------------------------------------
esp_err_t bmu_influx_write(const char *measurement, const char *tags, const char *fields, int64_t timestamp_ns)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (measurement == nullptr || fields == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    // Construire la ligne line-protocol dans un buffer temporaire
    char line[512];
    int len;
    if (tags != nullptr && strlen(tags) > 0) {
        len = snprintf(line, sizeof(line), "%s,%s %s %" PRId64 "\n",
                       measurement, tags, fields, timestamp_ns);
    } else {
        len = snprintf(line, sizeof(line), "%s %s %" PRId64 "\n",
                       measurement, fields, timestamp_ns);
    }

    if (len < 0 || (size_t)len >= sizeof(line)) {
        ESP_LOGW(TAG, "Ligne line-protocol trop longue, ignorée");
        return ESP_ERR_NO_MEM;
    }

    // Vérifier si le buffer peut contenir cette ligne
    if (s_buffer_len + (size_t)len >= BUFFER_MAX_BYTES) {
        // Flush automatique avant d'ajouter
        esp_err_t err = bmu_influx_flush();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Flush automatique échoué, ligne perdue");
            return err;
        }
    }

    // Copier la ligne dans le buffer
    memcpy(s_buffer + s_buffer_len, line, (size_t)len);
    s_buffer_len += (size_t)len;
    s_buffer_lines++;

    // Flush si le nombre de lignes atteint la limite configurée
    if (s_buffer_lines >= CONFIG_BMU_INFLUX_BUFFER_SIZE) {
        return bmu_influx_flush();
    }

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Write Battery — raccourci pour la télémétrie batterie
// ---------------------------------------------------------------------------
esp_err_t bmu_influx_write_battery(int battery_id, float voltage_mv, float current_a,
                                    float ah_discharge, float ah_charge, const char *state)
{
    if (state == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    char tags[64];
    snprintf(tags, sizeof(tags), "id=%d", battery_id);

    char fields[256];
    snprintf(fields, sizeof(fields),
             "voltage_mv=%.1f,current_a=%.3f,ah_discharge=%.4f,ah_charge=%.4f,state=\"%s\"",
             voltage_mv, current_a, ah_discharge, ah_charge, state);

    // Utiliser 0 comme timestamp — InfluxDB assignera le timestamp serveur
    // L'appelant peut utiliser bmu_sntp_get_timestamp_ns() pour un timestamp précis
    return bmu_influx_write("battery", tags, fields, 0);
}
