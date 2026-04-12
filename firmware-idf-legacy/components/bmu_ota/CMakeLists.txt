idf_component_register(
    SRCS "bmu_ota.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_https_ota esp_http_client app_update esp_event
)
