idf_component_register(
    SRCS
        "src/aiqa_chat_client.c"
        "src/aiqa_chat_intent_protocol.c"
        "src/aiqa_chat_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES app_core config_store esp_http_client esp-tls esp_timer json log provider_common
)
