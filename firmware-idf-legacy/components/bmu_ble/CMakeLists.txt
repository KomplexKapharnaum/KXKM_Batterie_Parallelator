idf_component_register(
    SRCS "bmu_ble.cpp" "bmu_ble_battery_svc.cpp" "bmu_ble_system_svc.cpp" "bmu_ble_control_svc.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bt bmu_protection bmu_config nvs_flash esp_timer bmu_rint bmu_soh bmu_ble_victron_gatt bmu_balancer
    PRIV_REQUIRES bmu_vedirect bmu_wifi bmu_storage bmu_ble_victron_scan
)
