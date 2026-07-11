#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_HARDENING_MIN_MODEL_HEAP_BYTES (64u * 1024u)
#define AIQA_HARDENING_RATE_LIMIT_COOLDOWN_MS 60000u
#define AIQA_HARDENING_MAX_PROVIDER_FAILURES 3u

typedef struct {
    size_t min_model_heap_bytes;
    uint32_t rate_limit_cooldown_ms;
    uint8_t max_consecutive_provider_failures;
    bool redact_transcripts;
    bool redact_answers;
} aiqa_hardening_policy_t;

aiqa_hardening_policy_t aiqa_hardening_default_policy(void);
bool aiqa_hardening_policy_is_safe(const aiqa_hardening_policy_t *policy);
bool aiqa_hardening_heap_allows_model_request(
    const aiqa_hardening_policy_t *policy,
    size_t free_heap_bytes);
uint32_t aiqa_hardening_rate_limit_until_ms(
    const aiqa_hardening_policy_t *policy,
    uint32_t now_ms,
    uint32_t retry_after_ms);
bool aiqa_hardening_request_in_cooldown(uint32_t now_ms, uint32_t cooldown_until_ms);

#ifdef __cplusplus
}
#endif
