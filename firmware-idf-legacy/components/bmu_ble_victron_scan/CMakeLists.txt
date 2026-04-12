idf_component_register(
    SRCS "bmu_ble_victron_scan.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bt bmu_config esp_timer
    PRIV_REQUIRES mbedtls bmu_mqtt bmu_influx
)
