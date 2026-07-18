#include "aiqa_boot_gesture.h"

#include <assert.h>
#include <string.h>

static aiqa_boot_gesture_output_t update(
    aiqa_boot_gesture_t *gesture,
    bool pressed,
    uint32_t now_ms)
{
    const aiqa_ptt_policy_t policy = aiqa_ptt_default_policy();
    return aiqa_boot_gesture_update(gesture, &policy, pressed, now_ms);
}

static aiqa_boot_gesture_output_t tap(
    aiqa_boot_gesture_t *gesture,
    uint32_t start_ms)
{
    (void)update(gesture, true, start_ms);
    (void)update(gesture, true, start_ms + 50U);
    (void)update(gesture, false, start_ms + 100U);
    return update(gesture, false, start_ms + 150U);
}

static void run_pairing(void)
{
    aiqa_boot_gesture_t gesture;
    aiqa_boot_gesture_init(&gesture);
    (void)update(&gesture, false, 0);
    (void)tap(&gesture, 100);
    (void)tap(&gesture, 300);
    const aiqa_boot_gesture_output_t third = tap(&gesture, 500);
    assert(third.local_action == AIQA_BOOT_LOCAL_NONE);
    assert(update(&gesture, false, 1200).local_action == AIQA_BOOT_LOCAL_PAIRING);
    assert(update(&gesture, false, 1300).local_action == AIQA_BOOT_LOCAL_NONE);
}

static void run_reset(void)
{
    aiqa_boot_gesture_t gesture;
    aiqa_boot_gesture_init(&gesture);
    (void)update(&gesture, false, 0);
    for (uint32_t index = 0; index < 5U; ++index) {
        (void)tap(&gesture, 100U + index * 200U);
    }
    assert(update(&gesture, false, 1800).local_action == AIQA_BOOT_LOCAL_RESET);
}

static void run_long_press(void)
{
    aiqa_boot_gesture_t gesture;
    aiqa_boot_gesture_init(&gesture);
    (void)update(&gesture, false, 0);
    (void)tap(&gesture, 100);
    (void)update(&gesture, true, 300);
    (void)update(&gesture, true, 350);
    const aiqa_boot_gesture_output_t started = update(&gesture, true, 850);
    assert(started.ptt == AIQA_PTT_OUTPUT_PRESS_START);
    assert(started.local_action == AIQA_BOOT_LOCAL_NONE);
    const aiqa_boot_gesture_output_t ended = update(&gesture, false, 900);
    assert(ended.ptt == AIQA_PTT_OUTPUT_NONE);
    const aiqa_boot_gesture_output_t stable_end = update(&gesture, false, 950);
    assert(stable_end.ptt == AIQA_PTT_OUTPUT_PRESS_END);
    assert(update(&gesture, false, 1800).local_action == AIQA_BOOT_LOCAL_NONE);
}

static void run_wrap(void)
{
    aiqa_ptt_button_t button;
    aiqa_ptt_button_init(&button);
    const aiqa_ptt_policy_t policy = aiqa_ptt_default_policy();
    (void)aiqa_ptt_button_update(&button, &policy, false, UINT32_MAX - 100U);
    (void)aiqa_ptt_button_update(&button, &policy, true, UINT32_MAX - 80U);
    (void)aiqa_ptt_button_update(&button, &policy, true, UINT32_MAX - 20U);
    assert(aiqa_ptt_button_update(&button, &policy, true, 450U) ==
           AIQA_PTT_OUTPUT_PRESS_START);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "pairing") == 0) run_pairing();
    else if (strcmp(argv[1], "reset") == 0) run_reset();
    else if (strcmp(argv[1], "long") == 0) run_long_press();
    else if (strcmp(argv[1], "wrap") == 0) run_wrap();
    else return 2;
    return 0;
}
