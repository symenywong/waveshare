idf_component_register(
    SRCS
        "src/aiqa_pairing_esp_nvs.c"
        "src/aiqa_pairing_esp_platform.c"
    INCLUDE_DIRS "include"
    REQUIRES bootloader_support esp_hw_support esp_timer management_session mbedtls nvs_flash
)
