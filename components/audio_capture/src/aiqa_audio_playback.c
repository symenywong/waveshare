#include "aiqa_audio_playback.h"

aiqa_audio_playback_config_t aiqa_audio_playback_default_config(void)
{
    return (aiqa_audio_playback_config_t){
        .sample_rate_hz = AIQA_AUDIO_PLAYBACK_DEFAULT_SAMPLE_RATE_HZ,
        .bits_per_sample = AIQA_AUDIO_PLAYBACK_BITS_PER_SAMPLE,
        .channels = AIQA_AUDIO_PLAYBACK_CHANNELS,
        .volume_percent = AIQA_AUDIO_PLAYBACK_DEFAULT_VOLUME_PERCENT,
        .chunk_bytes = AIQA_AUDIO_PLAYBACK_CHUNK_BYTES,
        .write_timeout_ms = AIQA_AUDIO_PLAYBACK_WRITE_TIMEOUT_MS,
    };
}

bool aiqa_audio_playback_config_is_safe(const aiqa_audio_playback_config_t *config)
{
    if (config == 0) {
        return false;
    }
    if (config->sample_rate_hz != AIQA_AUDIO_PLAYBACK_DEFAULT_SAMPLE_RATE_HZ) {
        return false;
    }
    if (config->bits_per_sample != AIQA_AUDIO_PLAYBACK_BITS_PER_SAMPLE ||
        config->channels != AIQA_AUDIO_PLAYBACK_CHANNELS) {
        return false;
    }
    if (config->volume_percent > AIQA_AUDIO_PLAYBACK_MAX_SAFE_VOLUME_PERCENT) {
        return false;
    }
    if (config->chunk_bytes == 0 || config->chunk_bytes > 4096 ||
        (config->chunk_bytes % sizeof(int16_t)) != 0) {
        return false;
    }
    return config->write_timeout_ms >= 100 && config->write_timeout_ms <= 2000;
}
