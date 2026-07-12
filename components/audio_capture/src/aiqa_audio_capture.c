#include "aiqa_audio_capture.h"
#include "aiqa_audio_capture_hw.h"

aiqa_audio_capture_config_t aiqa_audio_capture_default_config(void)
{
    aiqa_audio_capture_config_t config = {
        .sample_rate_hz = AIQA_AUDIO_SAMPLE_RATE_HZ,
        .bits_per_sample = AIQA_AUDIO_BITS_PER_SAMPLE,
        .channels = AIQA_AUDIO_CHANNELS,
        .max_record_seconds = AIQA_AUDIO_MAX_RECORD_SECONDS,
    };
    const size_t bytes_per_second = aiqa_audio_pcm_bytes_per_second(&config);
    config.max_pcm_bytes = bytes_per_second * config.max_record_seconds;
    config.ring_buffer_bytes = bytes_per_second * AIQA_AUDIO_RING_BUFFER_SECONDS;
    return config;
}

size_t aiqa_audio_pcm_bytes_per_second(const aiqa_audio_capture_config_t *config)
{
    if (config == 0 || config->bits_per_sample == 0 || config->channels == 0) {
        return 0;
    }

    return (size_t)config->sample_rate_hz * (size_t)config->channels * ((size_t)config->bits_per_sample / 8u);
}

bool aiqa_audio_capture_config_is_safe(const aiqa_audio_capture_config_t *config)
{
    if (config == 0) {
        return false;
    }
    if (config->sample_rate_hz != AIQA_AUDIO_SAMPLE_RATE_HZ) {
        return false;
    }
    if (config->bits_per_sample != AIQA_AUDIO_BITS_PER_SAMPLE || config->channels != AIQA_AUDIO_CHANNELS) {
        return false;
    }
    if (config->max_record_seconds == 0 || config->max_record_seconds > AIQA_AUDIO_MAX_RECORD_SECONDS) {
        return false;
    }
    if (config->max_pcm_bytes == 0 || config->max_pcm_bytes > AIQA_AUDIO_MAX_SAFE_PCM_BYTES) {
        return false;
    }
    if (config->ring_buffer_bytes == 0 || config->ring_buffer_bytes > config->max_pcm_bytes) {
        return false;
    }

    return config->max_pcm_bytes == aiqa_audio_pcm_bytes_per_second(config) * config->max_record_seconds;
}

aiqa_audio_capture_hw_config_t aiqa_audio_capture_hw_default_config(void)
{
    const aiqa_audio_capture_config_t capture_config = aiqa_audio_capture_default_config();
    return (aiqa_audio_capture_hw_config_t){
        .sample_rate_hz = capture_config.sample_rate_hz,
        .bits_per_sample = capture_config.bits_per_sample,
        .source_channels = AIQA_AUDIO_CAPTURE_ES7210_SOURCE_CHANNELS,
        .output_channels = capture_config.channels,
        .mic_gain_db = AIQA_AUDIO_CAPTURE_MIC_GAIN_DB,
        .read_timeout_ms = AIQA_AUDIO_CAPTURE_READ_TIMEOUT_MS,
        .chunk_frames = AIQA_AUDIO_CAPTURE_CHUNK_FRAMES,
    };
}

bool aiqa_audio_capture_hw_config_is_safe(const aiqa_audio_capture_hw_config_t *config)
{
    if (config == 0) {
        return false;
    }
    if (config->sample_rate_hz != AIQA_AUDIO_SAMPLE_RATE_HZ ||
        config->bits_per_sample != AIQA_AUDIO_BITS_PER_SAMPLE) {
        return false;
    }
    if (config->source_channels != AIQA_AUDIO_CAPTURE_ES7210_SOURCE_CHANNELS ||
        config->output_channels != AIQA_AUDIO_CHANNELS) {
        return false;
    }
    if (config->mic_gain_db < 0 || config->mic_gain_db > 42) {
        return false;
    }
    if (config->read_timeout_ms == 0 || config->read_timeout_ms > 1000) {
        return false;
    }
    return config->chunk_frames > 0 && config->chunk_frames <= AIQA_AUDIO_CAPTURE_CHUNK_FRAMES;
}
