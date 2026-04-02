/**
 * @file bmu_config.cpp
 * @brief Configuration runtime avec cache memoire + persistance NVS.
 *
 * Pattern : au boot bmu_config_load() lit NVS namespace "bmu".
 * Si une cle est absente, le defaut Kconfig est utilise.
 * Les setters ecrivent en NVS ET mettent a jour le cache.
 * Les getters retournent le cache (pas d'I/O).
 */

#include "bmu_config.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <cstring>
#include <cstdio>

static const char *TAG = "CFG";

/* ── NVS namespace et cles ─────────────────────────────────────────── */
#define NVS_NS          "bmu"
#define NVS_KEY_NAME    "dev_name"
#define NVS_KEY_SSID    "wifi_ssid"
#define NVS_KEY_PASS    "wifi_pass"
#define NVS_KEY_VMIN    "v_min"
#define NVS_KEY_VMAX    "v_max"
#define NVS_KEY_IMAX    "i_max"
#define NVS_KEY_VDIFF   "v_diff"
#define NVS_KEY_MQTT    "mqtt_uri"

/* ── Cache statique ────────────────────────────────────────────────── */
static char     s_device_name[BMU_CONFIG_NAME_MAX];
static char     s_wifi_ssid[BMU_CONFIG_SSID_MAX];
static char     s_wifi_pass[BMU_CONFIG_PASS_MAX];
static char     s_mqtt_uri[BMU_CONFIG_URI_MAX];
static uint16_t s_v_min;
static uint16_t s_v_max;
static uint16_t s_i_max;
static uint16_t s_v_diff;
static bool     s_loaded = false;

/* ── Helpers NVS ───────────────────────────────────────────────────── */

/**
 * @brief Lit une chaine NVS. Si absente, copie le defaut dans buf.
 */
static void load_str(nvs_handle_t h, const char *key,
                     char *buf, size_t buf_sz, const char *def)
{
    size_t len = buf_sz;
    esp_err_t ret = nvs_get_str(h, key, buf, &len);
    if (ret != ESP_OK) {
        strncpy(buf, def, buf_sz - 1);
        buf[buf_sz - 1] = '\0';
        ESP_LOGD(TAG, "NVS '%s' absent — defaut '%s'", key, def);
    } else {
        ESP_LOGI(TAG, "NVS '%s' = '%s'", key, buf);
    }
}

/**
 * @brief Lit un uint16 NVS. Si absent, retourne le defaut.
 */
static uint16_t load_u16(nvs_handle_t h, const char *key, uint16_t def)
{
    uint16_t val = 0;
    esp_err_t ret = nvs_get_u16(h, key, &val);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "NVS '%s' absent — defaut %u", key, (unsigned)def);
        return def;
    }
    ESP_LOGI(TAG, "NVS '%s' = %u", key, (unsigned)val);
    return val;
}

/**
 * @brief Ecrit une chaine dans NVS + commit.
 */
static esp_err_t save_str(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open write failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = nvs_set_str(h, key, value);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS save '%s' failed: %s", key, esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Ecrit un uint16 dans NVS + commit.
 */
static esp_err_t save_u16(const char *key, uint16_t value)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open write failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = nvs_set_u16(h, key, value);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS save '%s' failed: %s", key, esp_err_to_name(ret));
    }
    return ret;
}

/* ── API publique : load ───────────────────────────────────────────── */

esp_err_t bmu_config_load(void)
{
    /* Remplir le cache avec les defauts Kconfig */
    strncpy(s_device_name, CONFIG_BMU_DEVICE_NAME, sizeof(s_device_name) - 1);
    strncpy(s_wifi_ssid,   CONFIG_BMU_WIFI_SSID,   sizeof(s_wifi_ssid) - 1);
    strncpy(s_wifi_pass,   CONFIG_BMU_WIFI_PASSWORD, sizeof(s_wifi_pass) - 1);
    strncpy(s_mqtt_uri,    CONFIG_BMU_MQTT_BROKER_URI, sizeof(s_mqtt_uri) - 1);
    s_v_min  = (uint16_t)CONFIG_BMU_MIN_VOLTAGE_MV;
    s_v_max  = (uint16_t)CONFIG_BMU_MAX_VOLTAGE_MV;
    s_i_max  = (uint16_t)CONFIG_BMU_MAX_CURRENT_MA;
    s_v_diff = (uint16_t)CONFIG_BMU_VOLTAGE_DIFF_MV;

    /* Tenter de surcharger depuis NVS */
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (ret == ESP_OK) {
        load_str(h, NVS_KEY_NAME, s_device_name, sizeof(s_device_name), CONFIG_BMU_DEVICE_NAME);
        load_str(h, NVS_KEY_SSID, s_wifi_ssid,   sizeof(s_wifi_ssid),   CONFIG_BMU_WIFI_SSID);
        load_str(h, NVS_KEY_PASS, s_wifi_pass,    sizeof(s_wifi_pass),   CONFIG_BMU_WIFI_PASSWORD);
        load_str(h, NVS_KEY_MQTT, s_mqtt_uri,     sizeof(s_mqtt_uri),    CONFIG_BMU_MQTT_BROKER_URI);
        s_v_min  = load_u16(h, NVS_KEY_VMIN,  (uint16_t)CONFIG_BMU_MIN_VOLTAGE_MV);
        s_v_max  = load_u16(h, NVS_KEY_VMAX,  (uint16_t)CONFIG_BMU_MAX_VOLTAGE_MV);
        s_i_max  = load_u16(h, NVS_KEY_IMAX,  (uint16_t)CONFIG_BMU_MAX_CURRENT_MA);
        s_v_diff = load_u16(h, NVS_KEY_VDIFF, (uint16_t)CONFIG_BMU_VOLTAGE_DIFF_MV);
        nvs_close(h);
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "NVS namespace '%s' vide — defauts Kconfig utilises", NVS_NS);
    } else {
        ESP_LOGW(TAG, "NVS open failed: %s — defauts Kconfig utilises", esp_err_to_name(ret));
    }

    s_loaded = true;
    ESP_LOGI(TAG, "Config chargee (device='%s')", s_device_name);
    return ESP_OK;
}

/* ── API publique : log ────────────────────────────────────────────── */

void bmu_config_log(void)
{
    if (s_loaded) {
        ESP_LOGI(TAG, "Config: device='%s'", s_device_name);
        ESP_LOGI(TAG, "Config: V_min=%umV V_max=%umV I_max=%umA V_diff=%umV",
                 (unsigned)s_v_min, (unsigned)s_v_max,
                 (unsigned)s_i_max, (unsigned)s_v_diff);
        ESP_LOGI(TAG, "Config: WiFi SSID='%s' MQTT='%s'",
                 s_wifi_ssid, s_mqtt_uri);
    } else {
        /* Fallback pre-load : affiche les defauts Kconfig */
        ESP_LOGI(TAG, "Config (Kconfig): V_min=%dmV V_max=%dmV I_max=%dmA",
                 BMU_MIN_VOLTAGE_MV, BMU_MAX_VOLTAGE_MV, BMU_MAX_CURRENT_MA);
        ESP_LOGI(TAG, "Config (Kconfig): V_diff=%dmV delay=%dms nb_switch_max=%d",
                 BMU_VOLTAGE_DIFF_MV, BMU_RECONNECT_DELAY_MS, BMU_NB_SWITCH_MAX);
        ESP_LOGI(TAG, "Config (Kconfig): overcurrent_factor=%d/1000 loop=%dms",
                 BMU_OVERCURRENT_FACTOR, BMU_LOOP_PERIOD_MS);
    }
}

/* ── API publique : device name ────────────────────────────────────── */

esp_err_t bmu_config_set_device_name(const char *name)
{
    if (name == nullptr || name[0] == '\0') return ESP_ERR_INVALID_ARG;
    esp_err_t ret = save_str(NVS_KEY_NAME, name);
    if (ret == ESP_OK) {
        strncpy(s_device_name, name, sizeof(s_device_name) - 1);
        s_device_name[sizeof(s_device_name) - 1] = '\0';
        ESP_LOGI(TAG, "Device name → '%s'", s_device_name);
    }
    return ret;
}

const char *bmu_config_get_device_name(void)
{
    return s_device_name;
}

/* ── API publique : WiFi ───────────────────────────────────────────── */

esp_err_t bmu_config_set_wifi(const char *ssid, const char *password)
{
    if (ssid == nullptr) return ESP_ERR_INVALID_ARG;
    const char *pass = (password != nullptr) ? password : "";

    esp_err_t ret = save_str(NVS_KEY_SSID, ssid);
    if (ret != ESP_OK) return ret;

    ret = save_str(NVS_KEY_PASS, pass);
    if (ret != ESP_OK) return ret;

    strncpy(s_wifi_ssid, ssid, sizeof(s_wifi_ssid) - 1);
    s_wifi_ssid[sizeof(s_wifi_ssid) - 1] = '\0';
    strncpy(s_wifi_pass, pass, sizeof(s_wifi_pass) - 1);
    s_wifi_pass[sizeof(s_wifi_pass) - 1] = '\0';

    ESP_LOGI(TAG, "WiFi → SSID='%s'", s_wifi_ssid);
    return ESP_OK;
}

const char *bmu_config_get_wifi_ssid(void)
{
    return s_wifi_ssid;
}

const char *bmu_config_get_wifi_password(void)
{
    return s_wifi_pass;
}

/* ── API publique : thresholds ─────────────────────────────────────── */

esp_err_t bmu_config_set_thresholds(uint16_t min_mv, uint16_t max_mv,
                                     uint16_t max_ma, uint16_t diff_mv)
{
    /* Validation basique — ne pas permettre des valeurs absurdes */
    if (min_mv >= max_mv) {
        ESP_LOGW(TAG, "Thresholds invalides: min_mv(%u) >= max_mv(%u)",
                 (unsigned)min_mv, (unsigned)max_mv);
        return ESP_ERR_INVALID_ARG;
    }
    if (max_ma == 0) {
        ESP_LOGW(TAG, "Thresholds invalides: max_ma == 0");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;
    ret = save_u16(NVS_KEY_VMIN, min_mv);  if (ret != ESP_OK) return ret;
    ret = save_u16(NVS_KEY_VMAX, max_mv);  if (ret != ESP_OK) return ret;
    ret = save_u16(NVS_KEY_IMAX, max_ma);  if (ret != ESP_OK) return ret;
    ret = save_u16(NVS_KEY_VDIFF, diff_mv); if (ret != ESP_OK) return ret;

    s_v_min  = min_mv;
    s_v_max  = max_mv;
    s_i_max  = max_ma;
    s_v_diff = diff_mv;

    ESP_LOGI(TAG, "Thresholds → V_min=%u V_max=%u I_max=%u V_diff=%u",
             (unsigned)s_v_min, (unsigned)s_v_max,
             (unsigned)s_i_max, (unsigned)s_v_diff);
    return ESP_OK;
}

void bmu_config_get_thresholds(uint16_t *min_mv, uint16_t *max_mv,
                                uint16_t *max_ma, uint16_t *diff_mv)
{
    if (min_mv)  *min_mv  = s_v_min;
    if (max_mv)  *max_mv  = s_v_max;
    if (max_ma)  *max_ma  = s_i_max;
    if (diff_mv) *diff_mv = s_v_diff;
}

/* ── API publique : MQTT ───────────────────────────────────────────── */

esp_err_t bmu_config_set_mqtt_uri(const char *uri)
{
    if (uri == nullptr || uri[0] == '\0') return ESP_ERR_INVALID_ARG;
    esp_err_t ret = save_str(NVS_KEY_MQTT, uri);
    if (ret == ESP_OK) {
        strncpy(s_mqtt_uri, uri, sizeof(s_mqtt_uri) - 1);
        s_mqtt_uri[sizeof(s_mqtt_uri) - 1] = '\0';
        ESP_LOGI(TAG, "MQTT URI → '%s'", s_mqtt_uri);
    }
    return ret;
}

const char *bmu_config_get_mqtt_uri(void)
{
    return s_mqtt_uri;
}
