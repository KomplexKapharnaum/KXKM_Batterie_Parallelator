idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES bmu_i2c bmu_ina237 bmu_tca9535 bmu_protection bmu_config
)
