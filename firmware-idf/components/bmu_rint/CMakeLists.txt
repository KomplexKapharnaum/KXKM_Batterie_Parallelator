idf_component_register(
    SRCS "bmu_rint.cpp" "bmu_rint_output.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bmu_ina237 bmu_tca9535 bmu_protection
    PRIV_REQUIRES bmu_config bmu_i2c nvs_flash esp_timer
)
