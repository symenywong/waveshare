#pragma once

#include "aiqa_asr_client.h"
#include "aiqa_chat_client.h"
#include "aiqa_events.h"
#include "aiqa_ptt_button.h"

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool aiqa_runtime_ptt_output_to_event(aiqa_ptt_output_t output, aiqa_event_t *event);
aiqa_event_t aiqa_runtime_chat_result_to_event(const aiqa_chat_result_t *result, esp_err_t transport_ret);
aiqa_event_t aiqa_runtime_asr_result_to_event(const aiqa_asr_result_t *result, esp_err_t transport_ret);

#ifdef __cplusplus
}
#endif
