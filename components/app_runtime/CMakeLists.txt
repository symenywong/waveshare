idf_component_register(
    SRCS "src/aiqa_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES app_core audio_capture board_wave_175c chat_client config_store net_connect provider_common freertos log nvs_flash
)
