#pragma once

#include "aiqa_events.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t aiqa_runtime_start(void);
// Task-context API. Do not call from GPIO/touch ISRs.
esp_err_t aiqa_runtime_post_event(aiqa_event_t event);
esp_err_t aiqa_runtime_post_event_from_isr(aiqa_event_t event, bool *higher_priority_task_woken);

#ifdef __cplusplus
}
#endif
