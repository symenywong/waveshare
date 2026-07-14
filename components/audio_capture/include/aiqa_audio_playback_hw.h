#pragma once

#include "aiqa_audio_playback.h"

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t aiqa_audio_playback_hw_init(const aiqa_audio_playback_config_t *config);
esp_err_t aiqa_audio_playback_hw_start(void);
esp_err_t aiqa_audio_playback_hw_write_pcm(const uint8_t *pcm, size_t pcm_bytes);
esp_err_t aiqa_audio_playback_hw_stop(void);
esp_err_t aiqa_audio_playback_hw_play_test_tone(uint32_t frequency_hz, uint32_t duration_ms);
bool aiqa_audio_playback_hw_is_ready(void);
esp_err_t aiqa_audio_playback_hw_set_volume(uint8_t volume_percent);
uint8_t aiqa_audio_playback_hw_get_volume(void);

#ifdef __cplusplus
}
#endif
