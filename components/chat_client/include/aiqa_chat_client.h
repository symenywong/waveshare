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

esp_err_t aiqa_chat_send_once_with_language(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    const char *response_language,
    aiqa_chat_result_t *result);

esp_err_t aiqa_chat_send_once_with_context(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    const char *response_language,
    const char *conversation_context,
    aiqa_chat_result_t *result);

esp_err_t aiqa_chat_send_once_with_contexts(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    const char *response_language,
    const char *conversation_context,
    const char *assistant_profile_context,
    aiqa_chat_result_t *result);

esp_err_t aiqa_chat_send_streaming(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    aiqa_chat_event_cb_t on_delta,
    void *user_ctx,
    aiqa_chat_result_t *result);

esp_err_t aiqa_chat_send_streaming_with_language(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    const char *response_language,
    aiqa_chat_event_cb_t on_delta,
    void *user_ctx,
    aiqa_chat_result_t *result);

esp_err_t aiqa_chat_send_streaming_with_context(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    const char *response_language,
    const char *conversation_context,
    aiqa_chat_event_cb_t on_delta,
    void *user_ctx,
    aiqa_chat_result_t *result);

esp_err_t aiqa_chat_send_streaming_with_contexts(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    const char *response_language,
    const char *conversation_context,
    const char *assistant_profile_context,
    aiqa_chat_event_cb_t on_delta,
    void *user_ctx,
    aiqa_chat_result_t *result);

void aiqa_chat_cancel_active_request(void);

#ifdef __cplusplus
}
#endif
