idf_component_register(
    SRCS
        "src/aiqa_chat_client.c"
        "src/aiqa_chat_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store esp_http_client esp-tls log provider_common
)
