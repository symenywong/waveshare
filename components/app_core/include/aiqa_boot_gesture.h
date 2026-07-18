#pragma once

#include "aiqa_ptt_button.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_BOOT_GESTURE_GAP_MS 500U

typedef enum {
    AIQA_BOOT_LOCAL_NONE = 0,
    AIQA_BOOT_LOCAL_PAIRING,
    AIQA_BOOT_LOCAL_RESET,
} aiqa_boot_local_action_t;

typedef struct {
    aiqa_ptt_output_t ptt;
    aiqa_boot_local_action_t local_action;
} aiqa_boot_gesture_output_t;

typedef struct {
    aiqa_ptt_button_t ptt;
    uint8_t short_tap_count;
    uint32_t last_short_release_ms;
} aiqa_boot_gesture_t;

void aiqa_boot_gesture_init(aiqa_boot_gesture_t *gesture);
aiqa_boot_gesture_output_t aiqa_boot_gesture_update(
    aiqa_boot_gesture_t *gesture,
    const aiqa_ptt_policy_t *policy,
    bool raw_pressed,
    uint32_t now_ms);

#ifdef __cplusplus
}
#endif
