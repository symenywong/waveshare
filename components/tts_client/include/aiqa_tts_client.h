#pragma once

#include "aiqa_tts_protocol.h"

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_TTS_DEFAULT_TIMEOUT_MS 45000u

typedef struct {
    aiqa_tts_status_t status;
    int http_status;
    size_t audio_bytes;
    size_t audio_chunks;
} aiqa_tts_result_t;

typedef void (*aiqa_tts_audio_cb_t)(const uint8_t *pcm, size_t pcm_bytes, void *user_ctx);

esp_err_t aiqa_tts_speak_streaming(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *text,
    aiqa_tts_audio_cb_t on_audio,
    void *user_ctx,
    aiqa_tts_result_t *result);

void aiqa_tts_cancel_active_request(void);

#ifdef __cplusplus
}
#endif
