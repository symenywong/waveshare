#include "aiqa_runtime_events.h"

bool aiqa_runtime_ptt_output_to_event(aiqa_ptt_output_t output, aiqa_event_t *event)
{
    if (event == NULL) {
        return false;
    }

    switch (output) {
    case AIQA_PTT_OUTPUT_PRESS_START:
        *event = (aiqa_event_t){
            .type = AIQA_EVENT_PRESS_START,
            .error = AIQA_ERROR_NONE,
            .value = 0,
        };
        return true;
    case AIQA_PTT_OUTPUT_PRESS_END:
        *event = (aiqa_event_t){
            .type = AIQA_EVENT_PRESS_END,
            .error = AIQA_ERROR_NONE,
            .value = 0,
        };
        return true;
    case AIQA_PTT_OUTPUT_AUDIO_TOO_LONG:
        *event = (aiqa_event_t){
            .type = AIQA_EVENT_AUDIO_TOO_LONG,
            .error = AIQA_ERROR_AUDIO_TOO_LONG,
            .value = 0,
        };
        return true;
    case AIQA_PTT_OUTPUT_NONE:
    default:
        return false;
    }
}

aiqa_event_t aiqa_runtime_chat_result_to_event(const aiqa_chat_result_t *result, esp_err_t transport_ret)
{
    aiqa_chat_status_t status = result != NULL ? result->status : AIQA_CHAT_ERR_PROVIDER;
    if (transport_ret == ESP_ERR_TIMEOUT) {
        status = AIQA_CHAT_ERR_TIMEOUT;
    }

    switch (status) {
    case AIQA_CHAT_OK:
        return (aiqa_event_t){.type = AIQA_EVENT_CHAT_DONE, .error = AIQA_ERROR_NONE, .value = 0};
    case AIQA_CHAT_ERR_AUTH:
        return (aiqa_event_t){.type = AIQA_EVENT_AUTH_FAILED, .error = AIQA_ERROR_AUTH_FAILED, .value = status};
    case AIQA_CHAT_ERR_RATE_LIMITED:
        return (aiqa_event_t){.type = AIQA_EVENT_RATE_LIMITED, .error = AIQA_ERROR_RATE_LIMITED, .value = status};
    case AIQA_CHAT_ERR_TIMEOUT:
        return (aiqa_event_t){.type = AIQA_EVENT_TIMEOUT, .error = AIQA_ERROR_TIMEOUT, .value = status};
    case AIQA_CHAT_ERR_UNSUPPORTED_PROVIDER:
        return (aiqa_event_t){
            .type = AIQA_EVENT_PROVIDER_UNSUPPORTED,
            .error = AIQA_ERROR_PROVIDER_UNSUPPORTED,
            .value = status,
        };
    default:
        return (aiqa_event_t){.type = AIQA_EVENT_CHAT_FAILED, .error = AIQA_ERROR_CHAT_FAILED, .value = status};
    }
}

aiqa_event_t aiqa_runtime_asr_result_to_event(const aiqa_asr_result_t *result, esp_err_t transport_ret)
{
    aiqa_asr_status_t status = result != NULL ? result->status : AIQA_ASR_ERR_PROVIDER;
    if (transport_ret == ESP_ERR_TIMEOUT) {
        status = AIQA_ASR_ERR_TIMEOUT;
    }

    switch (status) {
    case AIQA_ASR_OK:
        return (aiqa_event_t){.type = AIQA_EVENT_ASR_DONE, .error = AIQA_ERROR_NONE, .value = 0};
    case AIQA_ASR_ERR_AUTH:
        return (aiqa_event_t){.type = AIQA_EVENT_AUTH_FAILED, .error = AIQA_ERROR_AUTH_FAILED, .value = status};
    case AIQA_ASR_ERR_RATE_LIMITED:
        return (aiqa_event_t){.type = AIQA_EVENT_RATE_LIMITED, .error = AIQA_ERROR_RATE_LIMITED, .value = status};
    case AIQA_ASR_ERR_TIMEOUT:
        return (aiqa_event_t){.type = AIQA_EVENT_TIMEOUT, .error = AIQA_ERROR_TIMEOUT, .value = status};
    case AIQA_ASR_ERR_UNSUPPORTED_PROVIDER:
        return (aiqa_event_t){
            .type = AIQA_EVENT_PROVIDER_UNSUPPORTED,
            .error = AIQA_ERROR_PROVIDER_UNSUPPORTED,
            .value = status,
        };
    default:
        return (aiqa_event_t){.type = AIQA_EVENT_ASR_FAILED, .error = AIQA_ERROR_ASR_FAILED, .value = status};
    }
}

aiqa_event_t aiqa_runtime_asr_failure_event(uint32_t generation)
{
    return (aiqa_event_t){
        .type = AIQA_EVENT_ASR_FAILED,
        .error = AIQA_ERROR_ASR_FAILED,
        .value = generation,
    };
}
