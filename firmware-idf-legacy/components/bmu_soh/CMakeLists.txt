if(CONFIG_BMU_SOH_ENABLED)
    idf_component_register(
        SRCS "bmu_soh.cpp"
        INCLUDE_DIRS "include"
        REQUIRES bmu_protection bmu_ina237
        PRIV_REQUIRES bmu_config bmu_rint espressif__esp-tflite-micro
        EMBED_FILES "models/fpnn_soh_int8.tflite"
    )
else()
    idf_component_register(
        SRCS "bmu_soh_stub.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES bmu_protection bmu_config bmu_rint
    )
endif()
