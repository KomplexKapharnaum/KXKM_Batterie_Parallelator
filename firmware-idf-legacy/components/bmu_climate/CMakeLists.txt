idf_component_register(
    SRCS "bmu_climate.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer
    PRIV_REQUIRES bmu_i2c
)
