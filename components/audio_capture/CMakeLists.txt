idf_component_register(
    SRCS
        "src/aiqa_audio_capture.c"
        "src/aiqa_audio_capture_hw.c"
        "src/aiqa_audio_playback.c"
        "src/aiqa_audio_playback_hw.c"
    INCLUDE_DIRS "include"
    REQUIRES board_wave_175c espressif__esp_codec_dev esp_driver_i2s esp_driver_i2c freertos log
)
