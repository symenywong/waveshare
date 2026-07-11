#include "aiqa_ptt_button.h"

static uint32_t elapsed_ms(uint32_t now_ms, uint32_t then_ms)
{
    return now_ms >= then_ms ? now_ms - then_ms : 0;
}

aiqa_ptt_policy_t aiqa_ptt_default_policy(void)
{
    return (aiqa_ptt_policy_t){
        .debounce_ms = AIQA_PTT_DEFAULT_DEBOUNCE_MS,
        .long_press_ms = AIQA_PTT_DEFAULT_LONG_PRESS_MS,
        .poll_interval_ms = AIQA_PTT_DEFAULT_POLL_MS,
        .max_record_ms = AIQA_PTT_DEFAULT_MAX_RECORD_MS,
    };
}

bool aiqa_ptt_policy_is_safe(const aiqa_ptt_policy_t *policy)
{
    if (policy == 0) {
        return false;
    }
    if (policy->debounce_ms < 10u || policy->debounce_ms > 200u) {
        return false;
    }
    if (policy->long_press_ms < 300u || policy->long_press_ms > 3000u) {
        return false;
    }
    if (policy->long_press_ms <= policy->debounce_ms) {
        return false;
    }
    if (policy->poll_interval_ms == 0 || policy->poll_interval_ms > policy->debounce_ms) {
        return false;
    }
    if (policy->max_record_ms < 1000u || policy->max_record_ms > AIQA_PTT_DEFAULT_MAX_RECORD_MS) {
        return false;
    }
    return policy->max_record_ms > policy->long_press_ms;
}

void aiqa_ptt_button_init(aiqa_ptt_button_t *button)
{
    if (button == 0) {
        return;
    }
    *button = (aiqa_ptt_button_t){0};
}

aiqa_ptt_output_t aiqa_ptt_button_update(
    aiqa_ptt_button_t *button,
    const aiqa_ptt_policy_t *policy,
    bool raw_pressed,
    uint32_t now_ms)
{
    if (button == 0 || !aiqa_ptt_policy_is_safe(policy)) {
        return AIQA_PTT_OUTPUT_NONE;
    }

    if (!button->initialized) {
        button->initialized = true;
        button->raw_pressed = raw_pressed;
        button->stable_pressed = raw_pressed;
        button->raw_changed_at_ms = now_ms;
        button->pressed_since_ms = raw_pressed ? now_ms : 0;
        return AIQA_PTT_OUTPUT_NONE;
    }

    if (raw_pressed != button->raw_pressed) {
        button->raw_pressed = raw_pressed;
        button->raw_changed_at_ms = now_ms;
    }

    if (button->stable_pressed != button->raw_pressed &&
        elapsed_ms(now_ms, button->raw_changed_at_ms) >= policy->debounce_ms) {
        button->stable_pressed = button->raw_pressed;
        if (button->stable_pressed) {
            button->pressed_since_ms = button->raw_changed_at_ms;
            button->timeout_reported = false;
        }
    }

    if (button->stable_pressed && !button->recording && !button->timeout_reported &&
        elapsed_ms(now_ms, button->pressed_since_ms) >= policy->long_press_ms) {
        button->recording = true;
        button->record_started_at_ms = now_ms;
        return AIQA_PTT_OUTPUT_PRESS_START;
    }

    if (button->stable_pressed && button->recording && !button->timeout_reported &&
        elapsed_ms(now_ms, button->record_started_at_ms) >= policy->max_record_ms) {
        button->recording = false;
        button->timeout_reported = true;
        return AIQA_PTT_OUTPUT_AUDIO_TOO_LONG;
    }

    if (!button->stable_pressed) {
        if (button->recording) {
            button->recording = false;
            return AIQA_PTT_OUTPUT_PRESS_END;
        }
        button->timeout_reported = false;
    }

    return AIQA_PTT_OUTPUT_NONE;
}
