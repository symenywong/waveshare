#pragma once

#include "aiqa_events.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_ASR_PHASE_TIMEOUT_MS 35000u

typedef enum {
    AIQA_STATE_BOOT = 0,
    AIQA_STATE_CONFIG_CHECK,
    AIQA_STATE_NETWORK_CONNECTING,
    AIQA_STATE_IDLE,
    AIQA_STATE_RECORDING,
    AIQA_STATE_TRANSCRIBING,
    AIQA_STATE_ASR_JOB_PENDING,
    AIQA_STATE_THINKING,
    AIQA_STATE_IDLE_WITH_RESULT,
    AIQA_STATE_ERROR,
} aiqa_state_t;

typedef struct {
    aiqa_state_t state;
    aiqa_error_code_t last_error;
    uint32_t transition_count;
} aiqa_state_machine_t;

typedef struct {
    aiqa_state_t previous_state;
    aiqa_state_t next_state;
    aiqa_error_code_t error;
    bool changed;
    bool accepted;
} aiqa_transition_t;

void aiqa_state_machine_init(aiqa_state_machine_t *machine);
aiqa_transition_t aiqa_state_machine_dispatch(aiqa_state_machine_t *machine, aiqa_event_t event);
bool aiqa_state_machine_asr_deadline_expired(
    aiqa_state_t state,
    uint32_t state_entered_ms,
    uint32_t now_ms);
const char *aiqa_state_name(aiqa_state_t state);

#ifdef __cplusplus
}
#endif
