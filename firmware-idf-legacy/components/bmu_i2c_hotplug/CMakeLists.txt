idf_component_register(
    SRCS "bmu_i2c_hotplug.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bmu_i2c bmu_ina237 bmu_tca9535 bmu_protection bmu_config bmu_ble bmu_types
    PRIV_REQUIRES esp_timer
)
