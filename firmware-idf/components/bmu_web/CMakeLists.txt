idf_component_register(
    SRCS "bmu_web.cpp" "bmu_web_security.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server spiffs bmu_protection bmu_config
    PRIV_REQUIRES bmu_wifi bmu_vedirect bmu_storage bmu_climate bmu_rint
)
