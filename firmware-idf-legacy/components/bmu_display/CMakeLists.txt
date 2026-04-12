idf_component_register(
    SRCS "bmu_display.cpp"
         "bmu_ui_main.cpp"
         "bmu_ui_detail.cpp"
         "bmu_ui_soh.cpp"
         "bmu_ui_system.cpp"
         "bmu_ui_alerts.cpp"
         "bmu_ui_config.cpp"
         "bmu_ui_debug.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp-box-3 esp_timer bmu_types
    PRIV_REQUIRES bmu_protection bmu_config bmu_wifi bmu_mqtt
                  bmu_storage bmu_sntp bmu_vedirect bmu_climate
                  bmu_soh bmu_ble bmu_ble_victron_scan bmu_ina237
                  esp_app_format bmu_rint bmu_balancer
)
