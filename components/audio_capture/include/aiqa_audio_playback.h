#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_AUDIO_PLAYBACK_DEFAULT_SAMPLE_RATE_HZ 24000u
#define AIQA_AUDIO_PLAYBACK_BITS_PER_SAMPLE 16u
#define AIQA_AUDIO_PLAYBACK_CHANNELS 1u
#define AIQA_AUDIO_PLAYBACK_DEFAULT_VOLUME_PERCENT 85
#define AIQA_AUDIO_PLAYBACK_MAX_SAFE_VOLUME_PERCENT 100
#define AIQA_AUDIO_PLAYBACK_CHUNK_BYTES 1024u
#define AIQA_AUDIO_PLAYBACK_WRITE_TIMEOUT_MS 500u

typedef struct {
    uint32_t sample_rate_hz;
    uint8_t bits_per_sample;
    uint8_t channels;
    uint8_t volume_percent;
    size_t chunk_bytes;
    uint32_t write_timeout_ms;
} aiqa_audio_playback_config_t;

aiqa_audio_playback_config_t aiqa_audio_playback_default_config(void);
bool aiqa_audio_playback_config_is_safe(const aiqa_audio_playback_config_t *config);

#ifdef __cplusplus
}
#endif
