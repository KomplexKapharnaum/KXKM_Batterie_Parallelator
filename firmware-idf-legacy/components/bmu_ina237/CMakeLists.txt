idf_component_register(
    SRCS "bmu_ina237.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver bmu_i2c_bitbang
    PRIV_REQUIRES bmu_i2c
)
