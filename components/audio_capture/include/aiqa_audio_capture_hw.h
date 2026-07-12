#pragma once

#include "aiqa_audio_capture.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_AUDIO_CAPTURE_ES7210_SOURCE_CHANNELS 4u
#define AIQA_AUDIO_CAPTURE_MIC_GAIN_DB 30
#define AIQA_AUDIO_CAPTURE_READ_TIMEOUT_MS 250u
#define AIQA_AUDIO_CAPTURE_CHUNK_FRAMES 256u

typedef struct {
    uint32_t sample_rate_hz;
    uint8_t bits_per_sample;
    uint8_t source_channels;
    uint8_t output_channels;
    int mic_gain_db;
    uint32_t read_timeout_ms;
    size_t chunk_frames;
} aiqa_audio_capture_hw_config_t;

typedef struct {
    size_t bytes_read;
    size_t mono_samples;
    int16_t peak_abs;
    bool has_signal;
} aiqa_audio_capture_stats_t;

aiqa_audio_capture_hw_config_t aiqa_audio_capture_hw_default_config(void);
bool aiqa_audio_capture_hw_config_is_safe(const aiqa_audio_capture_hw_config_t *config);
esp_err_t aiqa_audio_capture_hw_init(const aiqa_audio_capture_hw_config_t *config);
esp_err_t aiqa_audio_capture_hw_start(void);
esp_err_t aiqa_audio_capture_hw_read_mono(
    int16_t *out_samples,
    size_t out_sample_count,
    aiqa_audio_capture_stats_t *stats);
esp_err_t aiqa_audio_capture_hw_stop(void);
bool aiqa_audio_capture_hw_is_ready(void);

#ifdef __cplusplus
}
#endif
