idf_component_register(
    SRCS "bmu_protection.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bmu_ina237 bmu_tca9535 bmu_config
)
