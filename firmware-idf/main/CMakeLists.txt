idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES bmu_types bmu_i2c bmu_i2c_bitbang bmu_i2c_hotplug bmu_ina237 bmu_tca9535 bmu_protection bmu_config bmu_wifi bmu_web bmu_storage bmu_mqtt bmu_influx bmu_sntp bmu_display bmu_vedirect bmu_climate bmu_ota bmu_ble bmu_vrm bmu_ble_victron bmu_ble_victron_gatt bmu_ble_victron_scan bmu_rint bmu_balancer
)
