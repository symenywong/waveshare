#pragma once

#include "aiqa_asr_protocol.h"

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_ASR_DEFAULT_TIMEOUT_MS 30000u

typedef struct {
    aiqa_asr_status_t status;
    int http_status;
    char text[AIQA_ASR_RESPONSE_TEXT_MAX_LEN];
} aiqa_asr_result_t;

typedef struct {
    const uint8_t *pcm;
    size_t pcm_bytes;
    uint32_t sample_rate_hz;
    uint16_t bits_per_sample;
    uint16_t channels;
} aiqa_asr_pcm_audio_t;

esp_err_t aiqa_asr_transcribe_once(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *audio_ref,
    aiqa_asr_result_t *result);

esp_err_t aiqa_asr_transcribe_pcm_wav_once(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const aiqa_asr_pcm_audio_t *audio,
    aiqa_asr_result_t *result);

void aiqa_asr_cancel_active_request(void);

#ifdef __cplusplus
}
#endif
