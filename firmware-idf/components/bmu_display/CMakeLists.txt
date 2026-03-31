idf_component_register(
    SRCS "bmu_display.cpp" "bmu_ui_main.cpp" "bmu_ui_detail.cpp" "bmu_ui_system.cpp" "bmu_ui_alerts.cpp" "bmu_ui_debug.cpp" "bmu_ui_solar.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp-box-3 esp_timer
    PRIV_REQUIRES bmu_protection bmu_config bmu_wifi bmu_mqtt bmu_storage bmu_sntp bmu_vedirect esp_app_format
)
