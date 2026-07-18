#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t bytes_per_second;
    size_t initial_buffer_bytes;
    size_t resume_buffer_bytes;
} aiqa_tts_playback_policy_t;

typedef struct {
    size_t count;
    uint64_t total_wait_us;
    uint64_t max_wait_us;
} aiqa_tts_playback_starvation_stats_t;

bool aiqa_tts_playback_policy_init(
    aiqa_tts_playback_policy_t *policy,
    uint32_t sample_rate_hz,
    uint8_t bits_per_sample,
    uint8_t channels,
    uint32_t initial_buffer_ms,
    uint32_t resume_buffer_ms);

bool aiqa_tts_playback_buffer_ready(
    const aiqa_tts_playback_policy_t *policy,
    size_t buffered_bytes,
    bool producer_done,
    bool recovering_from_starvation);

bool aiqa_tts_playback_buffered_bytes(
    size_t held_pcm_bytes,
    size_t queued_pcm_bytes,
    size_t *out_buffered_bytes);

bool aiqa_tts_playback_wait_is_starvation(
    uint64_t wait_us,
    uint32_t threshold_ms);

void aiqa_tts_playback_starvation_stats_init(
    aiqa_tts_playback_starvation_stats_t *stats);

void aiqa_tts_playback_record_starvation(
    aiqa_tts_playback_starvation_stats_t *stats,
    uint64_t wait_us);

#ifdef __cplusplus
}
#endif
