#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_AUDIO_SAMPLE_RATE_HZ 24000u
#define AIQA_AUDIO_BITS_PER_SAMPLE 16u
#define AIQA_AUDIO_CHANNELS 1u
#define AIQA_AUDIO_MAX_RECORD_SECONDS 20u
#define AIQA_AUDIO_RING_BUFFER_SECONDS 2u
#define AIQA_AUDIO_MAX_SAFE_PCM_BYTES (1024u * 1024u)

typedef struct {
    uint32_t sample_rate_hz;
    uint8_t bits_per_sample;
    uint8_t channels;
    uint32_t max_record_seconds;
    size_t max_pcm_bytes;
    size_t ring_buffer_bytes;
} aiqa_audio_capture_config_t;

aiqa_audio_capture_config_t aiqa_audio_capture_default_config(void);
size_t aiqa_audio_pcm_bytes_per_second(const aiqa_audio_capture_config_t *config);
bool aiqa_audio_capture_config_is_safe(const aiqa_audio_capture_config_t *config);

#ifdef __cplusplus
}
#endif
