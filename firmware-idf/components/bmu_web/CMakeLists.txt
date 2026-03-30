idf_component_register(
    SRCS "bmu_web.cpp" "bmu_web_security.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server spiffs cJSON bmu_protection bmu_config
)
