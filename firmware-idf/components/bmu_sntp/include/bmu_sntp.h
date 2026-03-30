#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bmu_sntp_init(void);
bool bmu_sntp_is_synced(void);
esp_err_t bmu_sntp_get_time(struct tm *timeinfo);
int64_t bmu_sntp_get_timestamp_ns(void);  // For InfluxDB

#ifdef __cplusplus
}
#endif
