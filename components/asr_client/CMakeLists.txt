idf_component_register(
    SRCS
        "src/aiqa_asr_client.c"
        "src/aiqa_asr_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store esp_http_client esp-tls esp_timer freertos log provider_common
)
