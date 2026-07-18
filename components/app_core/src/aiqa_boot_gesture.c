#include "aiqa_boot_gesture.h"

#include <stddef.h>

static uint32_t elapsed_ms(uint32_t now_ms, uint32_t then_ms)
{
    return now_ms - then_ms;
}

void aiqa_boot_gesture_init(aiqa_boot_gesture_t *gesture)
{
    if (gesture == NULL) {
        return;
    }
    *gesture = (aiqa_boot_gesture_t){0};
    aiqa_ptt_button_init(&gesture->ptt);
}

aiqa_boot_gesture_output_t aiqa_boot_gesture_update(
    aiqa_boot_gesture_t *gesture,
    const aiqa_ptt_policy_t *policy,
    bool raw_pressed,
    uint32_t now_ms)
{
    aiqa_boot_gesture_output_t result = {0};
    if (gesture == NULL || !aiqa_ptt_policy_is_safe(policy)) {
        return result;
    }

    const bool was_stable_pressed = gesture->ptt.stable_pressed;
    const bool was_long_press = gesture->ptt.recording || gesture->ptt.timeout_reported;
    result.ptt =
        aiqa_ptt_button_update(&gesture->ptt, policy, raw_pressed, now_ms);

    if (result.ptt == AIQA_PTT_OUTPUT_PRESS_START ||
        result.ptt == AIQA_PTT_OUTPUT_AUDIO_TOO_LONG) {
        gesture->short_tap_count = 0;
        gesture->last_short_release_ms = 0;
    }

    if (was_stable_pressed && !gesture->ptt.stable_pressed &&
        !was_long_press && result.ptt == AIQA_PTT_OUTPUT_NONE) {
        if (gesture->short_tap_count < UINT8_MAX) {
            gesture->short_tap_count += 1U;
        }
        gesture->last_short_release_ms = now_ms;
    }

    if (!gesture->ptt.stable_pressed && gesture->short_tap_count > 0U &&
        elapsed_ms(now_ms, gesture->last_short_release_ms) >=
            AIQA_BOOT_GESTURE_GAP_MS) {
        if (gesture->short_tap_count == 3U) {
            result.local_action = AIQA_BOOT_LOCAL_PAIRING;
        } else if (gesture->short_tap_count == 5U) {
            result.local_action = AIQA_BOOT_LOCAL_RESET;
        }
        gesture->short_tap_count = 0;
        gesture->last_short_release_ms = 0;
    }
    return result;
}
