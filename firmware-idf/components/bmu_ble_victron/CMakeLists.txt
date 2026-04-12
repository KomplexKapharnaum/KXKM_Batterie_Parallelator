idf_component_register(
    SRCS "src/fleet_agg.c" "src/bmu_ble_victron.c" "src/instant_readout.c"
    INCLUDE_DIRS "include"
    REQUIRES bt mbedtls nvs_flash bmu_core_rs log
)
