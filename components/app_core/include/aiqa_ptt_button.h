#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_PTT_DEFAULT_DEBOUNCE_MS 40u
#define AIQA_PTT_DEFAULT_LONG_PRESS_MS 500u
#define AIQA_PTT_DEFAULT_POLL_MS 20u
#define AIQA_PTT_DEFAULT_MAX_RECORD_MS 20000u

typedef enum {
    AIQA_PTT_OUTPUT_NONE = 0,
    AIQA_PTT_OUTPUT_PRESS_START,
    AIQA_PTT_OUTPUT_PRESS_END,
    AIQA_PTT_OUTPUT_AUDIO_TOO_LONG,
} aiqa_ptt_output_t;

typedef struct {
    uint32_t debounce_ms;
    uint32_t long_press_ms;
    uint32_t poll_interval_ms;
    uint32_t max_record_ms;
} aiqa_ptt_policy_t;

typedef struct {
    bool initialized;
    bool raw_pressed;
    bool stable_pressed;
    bool recording;
    bool timeout_reported;
    uint32_t raw_changed_at_ms;
    uint32_t pressed_since_ms;
    uint32_t record_started_at_ms;
} aiqa_ptt_button_t;

aiqa_ptt_policy_t aiqa_ptt_default_policy(void);
bool aiqa_ptt_policy_is_safe(const aiqa_ptt_policy_t *policy);
void aiqa_ptt_button_init(aiqa_ptt_button_t *button);
aiqa_ptt_output_t aiqa_ptt_button_update(
    aiqa_ptt_button_t *button,
    const aiqa_ptt_policy_t *policy,
    bool raw_pressed,
    uint32_t now_ms);

#ifdef __cplusplus
}
#endif
