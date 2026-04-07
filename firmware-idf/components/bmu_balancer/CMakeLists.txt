idf_component_register(
    SRCS "bmu_balancer.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bmu_protection bmu_rint bmu_tca9535 bmu_ina237
)
