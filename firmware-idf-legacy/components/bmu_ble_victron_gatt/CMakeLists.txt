idf_component_register(
    SRCS "bmu_ble_victron_gatt.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bt bmu_protection bmu_config esp_timer
    PRIV_REQUIRES bmu_climate
)
