/**
 * @file bmu_climate.cpp
 * @brief Driver AHT30 — capteur temperature/humidite I2C.
 *
 * Protocole AHT30 (datasheet Aosong) :
 *   - Init : envoyer [0xBE, 0x08, 0x00], attendre 10ms
 *   - Mesure : envoyer [0xAC, 0x33, 0x00], attendre 80ms
 *   - Lecture 7 octets : [status, hum[19:12], hum[11:4], hum[3:0]|temp[19:16],
 *                         temp[15:8], temp[7:0], crc]
 *   - Humidite = (raw / 2^20) * 100
 *   - Temperature = (raw / 2^20) * 200 - 50
 */

#include "bmu_climate.h"
#include "bmu_i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cmath>
#include <inttypes.h>

static const char *TAG = "CLIMATE";

/* ── Constantes AHT30 ───────────────────────────────────────────────── */
#define AHT30_ADDR              0x38
#define AHT30_CMD_INIT          0xBE
#define AHT30_CMD_MEASURE       0xAC
#define AHT30_STATUS_BUSY_BIT   (1 << 7)
#define AHT30_STATUS_CAL_BIT    (1 << 3)
#define AHT30_READ_DELAY_MS     80
#define AHT30_INIT_DELAY_MS     10
#define AHT30_TIMER_PERIOD_US   (5 * 1000 * 1000)  /* 5 secondes */

/* ── Etat interne ────────────────────────────────────────────────────── */
static i2c_master_dev_handle_t s_dev     = NULL;
static esp_timer_handle_t      s_timer   = NULL;
static volatile bool           s_available = false;
static volatile float          s_temperature = NAN;
static volatile float          s_humidity    = NAN;
static uint32_t                s_failure_streak = 0;

/* ── Fonctions internes ──────────────────────────────────────────────── */

/**
 * @brief Envoie la commande d'initialisation AHT30 [0xBE, 0x08, 0x00].
 */
static esp_err_t aht30_send_init(void)
{
    uint8_t cmd[3] = { AHT30_CMD_INIT, 0x08, 0x00 };

    if (bmu_i2c_lock() != ESP_OK) return ESP_ERR_TIMEOUT;
    esp_err_t ret = i2c_master_transmit(s_dev, cmd, sizeof(cmd), pdMS_TO_TICKS(50));
    bmu_i2c_unlock();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AHT30 init cmd failed: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(AHT30_INIT_DELAY_MS));
    return ESP_OK;
}

/**
 * @brief Declenche une mesure et lit les 7 octets de resultat.
 */
static esp_err_t aht30_trigger_and_read(float *temperature_c, float *humidity_pct)
{
    /* Envoyer commande de mesure [0xAC, 0x33, 0x00] */
    uint8_t cmd[3] = { AHT30_CMD_MEASURE, 0x33, 0x00 };

    if (bmu_i2c_lock() != ESP_OK) return ESP_ERR_TIMEOUT;
    esp_err_t ret = i2c_master_transmit(s_dev, cmd, sizeof(cmd), pdMS_TO_TICKS(50));
    bmu_i2c_unlock();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AHT30 measure cmd failed: %s", esp_err_to_name(ret));
        bmu_i2c_record_failure();
        return ret;
    }

    /* Attendre la conversion (~80ms) */
    vTaskDelay(pdMS_TO_TICKS(AHT30_READ_DELAY_MS));

    /* Lire 7 octets */
    uint8_t data[7] = {};
    if (bmu_i2c_lock() != ESP_OK) return ESP_ERR_TIMEOUT;
    ret = i2c_master_receive(s_dev, data, sizeof(data), pdMS_TO_TICKS(50));
    bmu_i2c_unlock();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AHT30 read failed: %s", esp_err_to_name(ret));
        bmu_i2c_record_failure();
        return ret;
    }

    /* Verifier status : bit 7 = busy */
    if (data[0] & AHT30_STATUS_BUSY_BIT) {
        ESP_LOGW(TAG, "AHT30 encore occupe (status=0x%02X)", data[0]);
        return ESP_ERR_NOT_FINISHED;
    }

    /* Decoder humidite : data[1]<<12 | data[2]<<4 | data[3]>>4 */
    uint32_t raw_hum = ((uint32_t)data[1] << 12)
                     | ((uint32_t)data[2] << 4)
                     | ((uint32_t)(data[3] >> 4) & 0x0F);

    /* Decoder temperature : (data[3]&0x0F)<<16 | data[4]<<8 | data[5] */
    uint32_t raw_temp = (((uint32_t)data[3] & 0x0F) << 16)
                      | ((uint32_t)data[4] << 8)
                      | (uint32_t)data[5];

    float hum  = ((float)raw_hum  / 1048576.0f) * 100.0f;   /* 2^20 = 1048576 */
    float temp = ((float)raw_temp / 1048576.0f) * 200.0f - 50.0f;

    /* Validation basique des plages */
    if (hum < 0.0f || hum > 100.0f || temp < -40.0f || temp > 85.0f) {
        ESP_LOGW(TAG, "AHT30 valeurs hors limites: T=%.1f H=%.1f", temp, hum);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (temperature_c != NULL) *temperature_c = temp;
    if (humidity_pct  != NULL) *humidity_pct  = hum;

    bmu_i2c_record_success();
    return ESP_OK;
}

/**
 * @brief Callback timer periodique — lecture AHT30 toutes les 5s.
 *        Execute dans le contexte d'une task FreeRTOS (pas ISR).
 */
static void climate_timer_callback(void *arg)
{
    (void)arg;
    float temp = NAN, hum = NAN;
    esp_err_t ret = aht30_trigger_and_read(&temp, &hum);
    if (ret != ESP_OK && (ret == ESP_ERR_TIMEOUT || ret == ESP_ERR_NOT_FINISHED)) {
        ret = aht30_trigger_and_read(&temp, &hum);
    }
    if (ret == ESP_OK) {
        s_failure_streak = 0;
        s_temperature = temp;
        s_humidity    = hum;
        s_available   = true;
        ESP_LOGD(TAG, "AHT30: T=%.1f°C  H=%.1f%%", temp, hum);
    } else {
        /* Ne pas invalider les anciennes valeurs en cas d'erreur ponctuelle */
        s_failure_streak++;
        if (s_failure_streak == 1 || (s_failure_streak % 12) == 0) {
            ESP_LOGW(TAG, "AHT30 lecture echouee: %s (serie=%" PRIu32 ")",
                     esp_err_to_name(ret), s_failure_streak);
        } else {
            ESP_LOGD(TAG, "AHT30 lecture echouee: %s (serie=%" PRIu32 ")",
                     esp_err_to_name(ret), s_failure_streak);
        }
    }
}

/* ── API publique ────────────────────────────────────────────────────── */

esp_err_t bmu_climate_init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        ESP_LOGE(TAG, "Bus handle NULL — skip AHT30 init");
        return ESP_ERR_INVALID_ARG;
    }

    /* Enregistrer le device AHT30 sur le bus I2C */
    esp_err_t ret = bmu_i2c_add_device(bus, AHT30_ADDR, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Impossible d'ajouter AHT30 (0x%02X): %s",
                 AHT30_ADDR, esp_err_to_name(ret));
        return ret;
    }

    /* Envoyer la commande d'init AHT30 */
    ret = aht30_send_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AHT30 init cmd echouee — capteur absent ?");
        return ret;
    }

    /* Premiere lecture pour valider la presence du capteur */
    float temp, hum;
    ret = aht30_trigger_and_read(&temp, &hum);
    if (ret == ESP_OK) {
        s_temperature = temp;
        s_humidity    = hum;
        s_available   = true;
        ESP_LOGI(TAG, "AHT30 OK — T=%.1f°C  H=%.1f%%", temp, hum);
    } else {
        ESP_LOGW(TAG, "AHT30 premiere lecture echouee: %s — timer demarre quand meme",
                 esp_err_to_name(ret));
    }

    /* Creer le timer periodique (5s) */
    const esp_timer_create_args_t timer_args = {
        .callback        = climate_timer_callback,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "aht30_read",
        .skip_unhandled_events = true,
    };

    ret = esp_timer_create(&timer_args, &s_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_timer_start_periodic(s_timer, AHT30_TIMER_PERIOD_US);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_periodic failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Timer AHT30 demarre — periode 5s");
    return ESP_OK;
}

esp_err_t bmu_climate_read(float *temperature_c, float *humidity_pct)
{
    return aht30_trigger_and_read(temperature_c, humidity_pct);
}

float bmu_climate_get_temperature(void)
{
    return s_temperature;
}

float bmu_climate_get_humidity(void)
{
    return s_humidity;
}

bool bmu_climate_is_available(void)
{
    return s_available;
}
