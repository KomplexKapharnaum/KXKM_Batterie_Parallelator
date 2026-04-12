idf_component_register(
    SRCS "src/bmu_ble.c" "src/bmu_ble_gatt.c"
         "src/bmu_ble_control.c" "src/bmu_ble_hmac.c" "src/bmu_ble_audit.c"
         "src/bmu_ble_wifi_prov.c"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash bmu_core_rs log mbedtls bmu_ui bmu_wifi
)
