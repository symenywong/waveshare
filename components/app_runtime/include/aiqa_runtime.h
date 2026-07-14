#pragma once

#include "aiqa_events.h"
#include "aiqa_management_service.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t aiqa_runtime_start(void);
// Task-context API. Do not call from GPIO/touch ISRs.
esp_err_t aiqa_runtime_post_event(aiqa_event_t event);
esp_err_t aiqa_runtime_post_event_from_isr(aiqa_event_t event, bool *higher_priority_task_woken);

/* Transport adapters must authenticate callers before constructing this context. */
aiqa_management_result_t aiqa_runtime_management_get_status(
    const aiqa_management_security_context_t *access,
    aiqa_management_device_status_t *out_status);
aiqa_management_result_t aiqa_runtime_management_get_public_config(
    const aiqa_management_security_context_t *access,
    aiqa_management_public_config_t *out_config);
aiqa_management_result_t aiqa_runtime_management_submit_wifi_update(
    const aiqa_management_security_context_t *access,
    const aiqa_management_owned_wifi_update_t *update,
    uint32_t *out_operation_id);

#ifdef __cplusplus
}
#endif
