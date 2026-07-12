idf_component_register(
    SRCS
        "src/aiqa_tts_client.c"
        "src/aiqa_tts_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store provider_common esp_http_client esp-tls mbedtls
)
