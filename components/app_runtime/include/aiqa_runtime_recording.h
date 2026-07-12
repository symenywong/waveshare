#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *pcm;
    size_t capacity;
    size_t length;
} aiqa_runtime_recording_t;

esp_err_t aiqa_runtime_recording_init(aiqa_runtime_recording_t *recording, size_t capacity);
void aiqa_runtime_recording_reset(aiqa_runtime_recording_t *recording);
void aiqa_runtime_recording_clear(aiqa_runtime_recording_t *recording);
esp_err_t aiqa_runtime_recording_append_i16(
    aiqa_runtime_recording_t *recording,
    const int16_t *samples,
    size_t sample_count);
bool aiqa_runtime_recording_has_audio(const aiqa_runtime_recording_t *recording);

#ifdef __cplusplus
}
#endif
