idf_component_register(
    SRCS "src/bmu_i2c_glue.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_timer
        esp-box-3
        bmu_core_rs
)
