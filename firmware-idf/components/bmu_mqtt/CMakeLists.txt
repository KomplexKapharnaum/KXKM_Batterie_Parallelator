idf_component_register(
    SRCS "bmu_mqtt.cpp"
    INCLUDE_DIRS "include"
    REQUIRES mqtt esp_event
)
