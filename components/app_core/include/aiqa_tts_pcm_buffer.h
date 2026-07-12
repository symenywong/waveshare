#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AIQA_TTS_PCM_BUFFER_OK = 0,
    AIQA_TTS_PCM_BUFFER_INVALID_ARG,
    AIQA_TTS_PCM_BUFFER_FULL,
} aiqa_tts_pcm_buffer_status_t;

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t length;
    bool overflow;
} aiqa_tts_pcm_buffer_t;

void aiqa_tts_pcm_buffer_init(aiqa_tts_pcm_buffer_t *buffer, uint8_t *storage, size_t storage_bytes);
aiqa_tts_pcm_buffer_status_t aiqa_tts_pcm_buffer_append(
    aiqa_tts_pcm_buffer_t *buffer,
    const uint8_t *pcm,
    size_t pcm_bytes);

#ifdef __cplusplus
}
#endif
