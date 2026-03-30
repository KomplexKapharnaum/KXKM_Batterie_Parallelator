idf_component_register(
    SRCS "bmu_display.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_lcd_touch driver esp_timer
    PRIV_REQUIRES bmu_protection bmu_config
)
