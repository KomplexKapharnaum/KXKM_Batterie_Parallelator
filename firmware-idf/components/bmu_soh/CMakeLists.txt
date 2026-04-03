# Stub — SOH disabled (TFLite build issues)
idf_component_register(
    SRCS "bmu_soh_stub.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bmu_protection bmu_config bmu_rint
)
