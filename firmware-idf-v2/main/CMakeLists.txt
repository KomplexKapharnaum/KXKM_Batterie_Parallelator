idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_timer
        esp_system
        nvs_flash
        esp_lvgl_port
        esp-box-3
)

# Version projet injectée comme macro
add_compile_definitions(CONFIG_APP_PROJECT_VER="2.0.0-phase11")
