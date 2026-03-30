idf_component_register(
    SRCS "bmu_display.cpp" "bmu_ui_main.cpp" "bmu_ui_detail.cpp" "bmu_ui_system.cpp" "bmu_ui_alerts.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_lcd_touch driver esp_timer
    PRIV_REQUIRES bmu_protection bmu_config bmu_wifi bmu_mqtt bmu_storage bmu_sntp
)
