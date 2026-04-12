idf_component_register(
    SRCS "src/bmu_ble.c" "src/bmu_ble_gatt.c"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash bmu_core_rs log
)
