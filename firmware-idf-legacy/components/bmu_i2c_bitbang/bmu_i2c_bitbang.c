/**
 * @file bmu_i2c_bitbang.c
 * @brief Software I2C (bit-bang) driver for ESP32-S3.
 *
 * GPIO open-drain with internal+external pull-ups.
 * Timing via esp_rom_delay_us() for precision.
 * Thread-safe via FreeRTOS mutex.
 */
#include "sdkconfig.h"

#if CONFIG_BMU_I2C_BB_ENABLED

#include "bmu_i2c_bitbang.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdlib.h>

static const char *TAG = "I2C_BB";

typedef struct bmu_i2c_bb_ctx {
    int              sda;
    int              scl;
    uint32_t         half_period_us; /* half clock period */
    SemaphoreHandle_t mutex;
} bmu_i2c_bb_ctx_t;

/* ── GPIO helpers ─────────────────────────────────────────────────── */

static inline void sda_high(bmu_i2c_bb_ctx_t *c) { gpio_set_level(c->sda, 1); }
static inline void sda_low(bmu_i2c_bb_ctx_t *c)  { gpio_set_level(c->sda, 0); }
static inline void scl_high(bmu_i2c_bb_ctx_t *c) { gpio_set_level(c->scl, 1); }
static inline void scl_low(bmu_i2c_bb_ctx_t *c)  { gpio_set_level(c->scl, 0); }
static inline int  sda_read(bmu_i2c_bb_ctx_t *c) { return gpio_get_level(c->sda); }

static inline void delay(bmu_i2c_bb_ctx_t *c)
{
    esp_rom_delay_us(c->half_period_us);
}

/* ── I2C primitives ───────────────────────────────────────────────── */

static void i2c_start(bmu_i2c_bb_ctx_t *c)
{
    sda_high(c); delay(c);
    scl_high(c); delay(c);
    sda_low(c);  delay(c);
    scl_low(c);  delay(c);
}

static void i2c_stop(bmu_i2c_bb_ctx_t *c)
{
    sda_low(c);  delay(c);
    scl_high(c); delay(c);
    sda_high(c); delay(c);
}

static bool i2c_write_byte(bmu_i2c_bb_ctx_t *c, uint8_t byte)
{
    for (int i = 7; i >= 0; i--) {
        if (byte & (1 << i)) sda_high(c); else sda_low(c);
        delay(c);
        scl_high(c); delay(c);
        scl_low(c);  delay(c);
    }
    /* Read ACK */
    sda_high(c); delay(c);
    scl_high(c); delay(c);
    bool ack = (sda_read(c) == 0);
    scl_low(c); delay(c);
    return ack;
}

static uint8_t i2c_read_byte(bmu_i2c_bb_ctx_t *c, bool send_ack)
{
    uint8_t byte = 0;
    sda_high(c); /* release SDA for slave */
    for (int i = 7; i >= 0; i--) {
        delay(c);
        scl_high(c); delay(c);
        if (sda_read(c)) byte |= (1 << i);
        scl_low(c);
    }
    /* Send ACK/NACK */
    if (send_ack) sda_low(c); else sda_high(c);
    delay(c);
    scl_high(c); delay(c);
    scl_low(c);  delay(c);
    sda_high(c);
    return byte;
}

/* ── Public API ───────────────────────────────────────────────────── */

esp_err_t bmu_i2c_bb_init(const bmu_i2c_bb_config_t *cfg,
                           bmu_i2c_bb_handle_t *out_handle)
{
    if (!cfg || !out_handle) return ESP_ERR_INVALID_ARG;

    bmu_i2c_bb_ctx_t *ctx = calloc(1, sizeof(bmu_i2c_bb_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;

    ctx->sda = cfg->sda_gpio;
    ctx->scl = cfg->scl_gpio;
    ctx->half_period_us = 500000 / cfg->freq_hz; /* T/2 in us */
    if (ctx->half_period_us < 1) ctx->half_period_us = 1;

    ctx->mutex = xSemaphoreCreateMutex();

    /* Configure GPIOs as open-drain output + input */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ctx->sda) | (1ULL << ctx->scl),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* Idle state: both lines high */
    sda_high(ctx);
    scl_high(ctx);

    *out_handle = ctx;
    ESP_LOGI(TAG, "Bit-bang I2C on GPIO%d/%d @ %luHz (T/2=%luus)",
             ctx->sda, ctx->scl,
             (unsigned long)cfg->freq_hz,
             (unsigned long)ctx->half_period_us);
    return ESP_OK;
}

esp_err_t bmu_i2c_bb_write_read(bmu_i2c_bb_handle_t handle,
                                 uint8_t addr,
                                 const uint8_t *write_buf,
                                 size_t write_len,
                                 uint8_t *read_buf,
                                 size_t read_len)
{
    bmu_i2c_bb_ctx_t *c = (bmu_i2c_bb_ctx_t *)handle;
    if (!c) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(c->mutex, portMAX_DELAY);

    esp_err_t ret = ESP_OK;

    if (write_len > 0) {
        i2c_start(c);
        if (!i2c_write_byte(c, (addr << 1) | 0)) { /* write address */
            i2c_stop(c);
            xSemaphoreGive(c->mutex);
            return ESP_ERR_NOT_FOUND;
        }
        for (size_t i = 0; i < write_len; i++) {
            if (!i2c_write_byte(c, write_buf[i])) {
                ret = ESP_FAIL;
                break;
            }
        }
        if (ret != ESP_OK) {
            i2c_stop(c);
            xSemaphoreGive(c->mutex);
            return ret;
        }
    }

    if (read_len > 0) {
        i2c_start(c); /* repeated start */
        if (!i2c_write_byte(c, (addr << 1) | 1)) { /* read address */
            i2c_stop(c);
            xSemaphoreGive(c->mutex);
            return ESP_ERR_NOT_FOUND;
        }
        for (size_t i = 0; i < read_len; i++) {
            read_buf[i] = i2c_read_byte(c, i < read_len - 1);
        }
    }

    i2c_stop(c);
    xSemaphoreGive(c->mutex);
    return ESP_OK;
}

esp_err_t bmu_i2c_bb_write(bmu_i2c_bb_handle_t handle,
                            uint8_t addr,
                            const uint8_t *buf, size_t len)
{
    return bmu_i2c_bb_write_read(handle, addr, buf, len, NULL, 0);
}

bool bmu_i2c_bb_probe(bmu_i2c_bb_handle_t handle, uint8_t addr)
{
    bmu_i2c_bb_ctx_t *c = (bmu_i2c_bb_ctx_t *)handle;
    if (!c) return false;

    xSemaphoreTake(c->mutex, portMAX_DELAY);
    i2c_start(c);
    bool ack = i2c_write_byte(c, (addr << 1) | 0);
    i2c_stop(c);
    xSemaphoreGive(c->mutex);
    return ack;
}

esp_err_t bmu_i2c_bb_read_reg16(bmu_i2c_bb_handle_t handle,
                                 uint8_t addr, uint8_t reg,
                                 uint16_t *value)
{
    uint8_t buf[2];
    esp_err_t ret = bmu_i2c_bb_write_read(handle, addr,
                                           &reg, 1, buf, 2);
    if (ret == ESP_OK) {
        *value = ((uint16_t)buf[0] << 8) | buf[1];
    }
    return ret;
}

esp_err_t bmu_i2c_bb_write_reg16(bmu_i2c_bb_handle_t handle,
                                  uint8_t addr, uint8_t reg,
                                  uint16_t value)
{
    uint8_t buf[3] = { reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
    return bmu_i2c_bb_write(handle, addr, buf, 3);
}

#else /* !CONFIG_BMU_I2C_BB_ENABLED */

#include "bmu_i2c_bitbang.h"
esp_err_t bmu_i2c_bb_init(const bmu_i2c_bb_config_t *c, bmu_i2c_bb_handle_t *h)
{ (void)c; (void)h; return ESP_OK; }
esp_err_t bmu_i2c_bb_write_read(bmu_i2c_bb_handle_t h, uint8_t a,
    const uint8_t *w, size_t wl, uint8_t *r, size_t rl)
{ (void)h; (void)a; (void)w; (void)wl; (void)r; (void)rl; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t bmu_i2c_bb_write(bmu_i2c_bb_handle_t h, uint8_t a, const uint8_t *b, size_t l)
{ (void)h; (void)a; (void)b; (void)l; return ESP_ERR_NOT_SUPPORTED; }
bool bmu_i2c_bb_probe(bmu_i2c_bb_handle_t h, uint8_t a) { (void)h; (void)a; return false; }
esp_err_t bmu_i2c_bb_read_reg16(bmu_i2c_bb_handle_t h, uint8_t a, uint8_t r, uint16_t *v)
{ (void)h; (void)a; (void)r; (void)v; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t bmu_i2c_bb_write_reg16(bmu_i2c_bb_handle_t h, uint8_t a, uint8_t r, uint16_t v)
{ (void)h; (void)a; (void)r; (void)v; return ESP_ERR_NOT_SUPPORTED; }

#endif
