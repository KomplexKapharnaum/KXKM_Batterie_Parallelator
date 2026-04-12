idf_component_register(
    SRCS "bmu_influx.cpp" "bmu_influx_store.cpp"
    INCLUDE_DIRS "include" "."
    REQUIRES esp_http_client bmu_storage
)
