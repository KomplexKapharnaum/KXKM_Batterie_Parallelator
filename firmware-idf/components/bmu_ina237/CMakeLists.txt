idf_component_register(
    SRCS "bmu_ina237.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bmu_i2c
)
