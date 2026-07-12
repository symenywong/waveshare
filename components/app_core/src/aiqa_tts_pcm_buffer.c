#include "aiqa_tts_pcm_buffer.h"

#include <string.h>

void aiqa_tts_pcm_buffer_init(aiqa_tts_pcm_buffer_t *buffer, uint8_t *storage, size_t storage_bytes)
{
    if (buffer == NULL) {
        return;
    }

    buffer->data = storage;
    buffer->capacity = storage_bytes;
    buffer->length = 0;
    buffer->overflow = false;
}

aiqa_tts_pcm_buffer_status_t aiqa_tts_pcm_buffer_append(
    aiqa_tts_pcm_buffer_t *buffer,
    const uint8_t *pcm,
    size_t pcm_bytes)
{
    if (buffer == NULL || buffer->data == NULL || buffer->capacity == 0 ||
        (pcm == NULL && pcm_bytes > 0)) {
        return AIQA_TTS_PCM_BUFFER_INVALID_ARG;
    }
    if (pcm_bytes == 0) {
        return AIQA_TTS_PCM_BUFFER_OK;
    }
    if (pcm_bytes > buffer->capacity - buffer->length) {
        buffer->overflow = true;
        return AIQA_TTS_PCM_BUFFER_FULL;
    }

    (void)memcpy(buffer->data + buffer->length, pcm, pcm_bytes);
    buffer->length += pcm_bytes;
    return AIQA_TTS_PCM_BUFFER_OK;
}
