#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMU_VIC_SCAN_MAX_DEVICES CONFIG_BMU_VIC_SCAN_MAX_DEVICES

typedef struct {
    uint8_t  mac[6];
    uint8_t  record_type;        /* 0x01=solar, 0x02=battery, 0x03=inverter, 0x04=dcdc */
    uint8_t  raw_decrypted[10];
    int64_t  last_seen_ms;
    bool     key_configured;
    bool     decrypted;
    char     label[16];
    /* Parsed fields by record type */
    union {
        struct { uint8_t cs; uint8_t err; uint16_t yield_wh; uint16_t ppv_w;
                 int16_t ibat_da; uint16_t vbat_cv; } solar;
        struct { uint16_t rem_ah_dah; uint16_t v_cv; int16_t i_da;
                 uint16_t soc_pm; uint16_t cons_dah; } battery;
        struct { uint16_t vac_cv; uint16_t iac_da; uint8_t state; } inverter;
        struct { uint16_t vin_cv; uint16_t vout_cv; uint8_t state; } dcdc;
    };
} bmu_vic_device_t;

esp_err_t bmu_vic_scan_init(void);
esp_err_t bmu_vic_scan_start(void);
void      bmu_vic_scan_stop(void);
int       bmu_vic_scan_get_devices(bmu_vic_device_t *out, int max);
const bmu_vic_device_t *bmu_vic_scan_get_device_by_mac(const uint8_t mac[6]);
int       bmu_vic_scan_count(void);

#ifdef __cplusplus
}
#endif
