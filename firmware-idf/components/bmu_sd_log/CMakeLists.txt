idf_component_register(
    SRCS "src/bmu_sd_log.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer fatfs vfs wear_levelling bmu_core_rs
)
