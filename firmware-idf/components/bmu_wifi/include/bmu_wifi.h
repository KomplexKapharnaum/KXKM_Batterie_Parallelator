#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bmu_wifi_init(void);
bool bmu_wifi_is_connected(void);
esp_err_t bmu_wifi_get_ip(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
