idf_component_register(
    SRCS "src/bmu_soh.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer bmu_core_rs
    PRIV_REQUIRES espressif__esp-tflite-micro
    EMBED_FILES "models/fpnn_soh_v3_int8.tflite"
)
