#include "aiqa_tts_playback_policy.h"

#include <limits.h>

static bool bytes_for_duration(
    size_t bytes_per_second,
    uint32_t duration_ms,
    size_t *out_bytes)
{
    if (bytes_per_second == 0 || duration_ms == 0 || out_bytes == NULL) {
        return false;
    }

    if (duration_ms > UINT64_MAX / bytes_per_second) {
        return false;
    }
    const uint64_t scaled = (uint64_t)bytes_per_second * duration_ms;
    if (scaled > UINT64_MAX - 999u) {
        return false;
    }
    const uint64_t bytes = (scaled + 999u) / 1000u;
    if (bytes == 0 || bytes > SIZE_MAX) {
        return false;
    }
    *out_bytes = (size_t)bytes;
    return true;
}

bool aiqa_tts_playback_policy_init(
    aiqa_tts_playback_policy_t *policy,
    uint32_t sample_rate_hz,
    uint8_t bits_per_sample,
    uint8_t channels,
    uint32_t initial_buffer_ms,
    uint32_t resume_buffer_ms)
{
    if (policy == NULL || sample_rate_hz == 0 || bits_per_sample == 0 ||
        (bits_per_sample % 8u) != 0 || channels == 0) {
        return false;
    }

    const uint64_t bytes_per_second =
        (uint64_t)sample_rate_hz * channels * (bits_per_sample / 8u);
    if (bytes_per_second == 0 || bytes_per_second > SIZE_MAX) {
        return false;
    }

    aiqa_tts_playback_policy_t initialized = {
        .bytes_per_second = (size_t)bytes_per_second,
    };
    if (!bytes_for_duration(initialized.bytes_per_second,
                            initial_buffer_ms,
                            &initialized.initial_buffer_bytes) ||
        !bytes_for_duration(initialized.bytes_per_second,
                            resume_buffer_ms,
                            &initialized.resume_buffer_bytes)) {
        return false;
    }

    *policy = initialized;
    return true;
}

bool aiqa_tts_playback_buffer_ready(
    const aiqa_tts_playback_policy_t *policy,
    size_t buffered_bytes,
    bool producer_done,
    bool recovering_from_starvation)
{
    if (policy == NULL || buffered_bytes == 0) {
        return false;
    }
    if (producer_done) {
        return true;
    }

    const size_t required_bytes = recovering_from_starvation
                                      ? policy->resume_buffer_bytes
                                      : policy->initial_buffer_bytes;
    return required_bytes > 0 && buffered_bytes >= required_bytes;
}

bool aiqa_tts_playback_buffered_bytes(
    size_t held_pcm_bytes,
    size_t queued_pcm_bytes,
    size_t *out_buffered_bytes)
{
    if (out_buffered_bytes == NULL || SIZE_MAX - held_pcm_bytes < queued_pcm_bytes) {
        return false;
    }
    *out_buffered_bytes = held_pcm_bytes + queued_pcm_bytes;
    return true;
}

bool aiqa_tts_playback_wait_is_starvation(
    uint64_t wait_us,
    uint32_t threshold_ms)
{
    if (threshold_ms == 0) {
        return wait_us > 0;
    }
    return wait_us >= ((uint64_t)threshold_ms * 1000u);
}

void aiqa_tts_playback_starvation_stats_init(
    aiqa_tts_playback_starvation_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    *stats = (aiqa_tts_playback_starvation_stats_t){0};
}

void aiqa_tts_playback_record_starvation(
    aiqa_tts_playback_starvation_stats_t *stats,
    uint64_t wait_us)
{
    if (stats == NULL) {
        return;
    }
    if (stats->count < SIZE_MAX) {
        stats->count += 1u;
    }
    if (UINT64_MAX - stats->total_wait_us < wait_us) {
        stats->total_wait_us = UINT64_MAX;
    } else {
        stats->total_wait_us += wait_us;
    }
    if (wait_us > stats->max_wait_us) {
        stats->max_wait_us = wait_us;
    }
}
