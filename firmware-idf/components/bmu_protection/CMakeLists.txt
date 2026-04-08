idf_component_register(
    SRCS "bmu_protection.cpp" "bmu_battery_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bmu_types bmu_ina237 bmu_tca9535 bmu_config esp_timer
    PRIV_REQUIRES bmu_rint
)
