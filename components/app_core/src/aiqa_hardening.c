#include "aiqa_hardening.h"

aiqa_hardening_policy_t aiqa_hardening_default_policy(void)
{
    return (aiqa_hardening_policy_t){
        .min_model_heap_bytes = AIQA_HARDENING_MIN_MODEL_HEAP_BYTES,
        .rate_limit_cooldown_ms = AIQA_HARDENING_RATE_LIMIT_COOLDOWN_MS,
        .max_consecutive_provider_failures = AIQA_HARDENING_MAX_PROVIDER_FAILURES,
        .redact_transcripts = true,
        .redact_answers = true,
    };
}

bool aiqa_hardening_policy_is_safe(const aiqa_hardening_policy_t *policy)
{
    if (policy == 0) {
        return false;
    }
    if (policy->min_model_heap_bytes < (32u * 1024u)) {
        return false;
    }
    if (policy->rate_limit_cooldown_ms < 1000u || policy->rate_limit_cooldown_ms > (10u * 60u * 1000u)) {
        return false;
    }
    if (policy->max_consecutive_provider_failures == 0 ||
        policy->max_consecutive_provider_failures > 10u) {
        return false;
    }
    return policy->redact_transcripts && policy->redact_answers;
}

bool aiqa_hardening_heap_allows_model_request(
    const aiqa_hardening_policy_t *policy,
    size_t free_heap_bytes)
{
    return aiqa_hardening_policy_is_safe(policy) && free_heap_bytes >= policy->min_model_heap_bytes;
}

uint32_t aiqa_hardening_rate_limit_until_ms(
    const aiqa_hardening_policy_t *policy,
    uint32_t now_ms,
    uint32_t retry_after_ms)
{
    if (!aiqa_hardening_policy_is_safe(policy)) {
        return now_ms;
    }
    const uint32_t delay_ms = retry_after_ms > 0 ? retry_after_ms : policy->rate_limit_cooldown_ms;
    return now_ms + delay_ms;
}

bool aiqa_hardening_request_in_cooldown(uint32_t now_ms, uint32_t cooldown_until_ms)
{
    return now_ms < cooldown_until_ms;
}
