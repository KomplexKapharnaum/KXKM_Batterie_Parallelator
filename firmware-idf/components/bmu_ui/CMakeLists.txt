idf_component_register(
    SRCS
        "src/bmu_ui.c"
        "src/tab_batt.c"
        "src/tab_soh.c"
        "src/tab_sys.c"
        "src/tab_climate.c"
        "src/tab_config.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_lvgl_port
        bmu_core_rs
        bmu_climate
        bmu_wifi
        bmu_mqtt
        esp_system
        log
)
