idf_component_register(
    SRCS
        "src/aiqa_pairing_esp_nvs.c"
    INCLUDE_DIRS "include"
    REQUIRES management_session nvs_flash
)
