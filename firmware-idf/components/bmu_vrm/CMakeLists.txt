idf_component_register(
    SRCS "bmu_vrm.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bmu_protection bmu_config bmu_vedirect esp_timer
    PRIV_REQUIRES mqtt esp-tls bmu_ina237
)
