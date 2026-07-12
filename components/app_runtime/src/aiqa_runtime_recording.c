#include "aiqa_runtime_recording.h"

#include "esp_heap_caps.h"
#include "sdkconfig.h"

#include <string.h>

static void secure_zero(void *data, size_t length)
{
    volatile unsigned char *cursor = (volatile unsigned char *)data;
    while (cursor != NULL && length > 0) {
        *cursor++ = 0;
        --length;
    }
}

static uint8_t *allocate_pcm_buffer(size_t capacity)
{
    if (capacity == 0) {
        return NULL;
    }
#if CONFIG_SPIRAM
    uint8_t *buffer = (uint8_t *)heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer != NULL) {
        return buffer;
    }
#endif
    return (uint8_t *)heap_caps_malloc(capacity, MALLOC_CAP_8BIT);
}

esp_err_t aiqa_runtime_recording_init(aiqa_runtime_recording_t *recording, size_t capacity)
{
    if (recording == NULL || capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (recording->pcm != NULL && recording->capacity >= capacity) {
        aiqa_runtime_recording_reset(recording);
        return ESP_OK;
    }

    aiqa_runtime_recording_clear(recording);
    recording->pcm = allocate_pcm_buffer(capacity);
    if (recording->pcm == NULL) {
        recording->capacity = 0;
        recording->length = 0;
        return ESP_ERR_NO_MEM;
    }
    recording->capacity = capacity;
    recording->length = 0;
    return ESP_OK;
}

void aiqa_runtime_recording_reset(aiqa_runtime_recording_t *recording)
{
    if (recording == NULL || recording->pcm == NULL) {
        return;
    }
    secure_zero(recording->pcm, recording->capacity);
    recording->length = 0;
}

void aiqa_runtime_recording_clear(aiqa_runtime_recording_t *recording)
{
    if (recording == NULL) {
        return;
    }
    if (recording->pcm != NULL) {
        secure_zero(recording->pcm, recording->capacity);
        heap_caps_free(recording->pcm);
    }
    recording->pcm = NULL;
    recording->capacity = 0;
    recording->length = 0;
}

esp_err_t aiqa_runtime_recording_append_i16(
    aiqa_runtime_recording_t *recording,
    const int16_t *samples,
    size_t sample_count)
{
    if (recording == NULL || recording->pcm == NULL || samples == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sample_count == 0) {
        return ESP_OK;
    }
    if (sample_count > SIZE_MAX / sizeof(int16_t)) {
        return ESP_ERR_NO_MEM;
    }
    const size_t bytes = sample_count * sizeof(int16_t);
    if (bytes > recording->capacity - recording->length) {
        return ESP_ERR_NO_MEM;
    }
    (void)memcpy(recording->pcm + recording->length, samples, bytes);
    recording->length += bytes;
    return ESP_OK;
}

bool aiqa_runtime_recording_has_audio(const aiqa_runtime_recording_t *recording)
{
    return recording != NULL && recording->pcm != NULL && recording->length > 0;
}
