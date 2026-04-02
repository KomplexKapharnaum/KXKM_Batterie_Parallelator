idf_component_register(
    SRCS "bmu_storage.cpp"
    INCLUDE_DIRS "include"
    REQUIRES fatfs sdmmc nvs_flash vfs wear_levelling
)
