idf_component_register(
    SRCS
        "src/aiqa_runtime.c"
        "src/aiqa_runtime_events.c"
        "src/aiqa_runtime_recording.c"
        "src/aiqa_runtime_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES app_core asr_client audio_capture board_wave_175c chat_client config_store heap net_connect provider_common freertos log nvs_flash
)
