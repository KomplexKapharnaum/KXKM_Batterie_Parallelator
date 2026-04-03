idf_component_register(
    SRCS "bmu_ble_victron.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bmu_protection bmu_config bmu_vedirect bt esp_timer
    PRIV_REQUIRES mbedtls
)
