idf_component_register(
    SRCS "src/bmu_mqtt.c" "src/bmu_mqtt_replay.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt esp_event log bmu_core_rs bmu_sd_log
)
