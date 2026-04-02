idf_component_register(
    SRCS "bmu_ina237.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver
    PRIV_REQUIRES bmu_i2c bmu_i2c_bitbang
)
