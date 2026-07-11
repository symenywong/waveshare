#pragma once

#include "aiqa_asr_protocol.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_ASR_DEFAULT_TIMEOUT_MS 30000u

typedef struct {
    aiqa_asr_status_t status;
    int http_status;
    char text[AIQA_ASR_RESPONSE_TEXT_MAX_LEN];
} aiqa_asr_result_t;

esp_err_t aiqa_asr_transcribe_once(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *audio_ref,
    aiqa_asr_result_t *result);

#ifdef __cplusplus
}
#endif
