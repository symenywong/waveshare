#pragma once

#include "aiqa_chat_protocol.h"
#include "aiqa_config.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_CHAT_DEFAULT_TIMEOUT_MS 30000u

typedef struct {
    aiqa_chat_status_t status;
    int http_status;
    char text[AIQA_CHAT_RESPONSE_TEXT_MAX_LEN];
} aiqa_chat_result_t;

esp_err_t aiqa_chat_send_once(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    aiqa_chat_result_t *result);

#ifdef __cplusplus
}
#endif
