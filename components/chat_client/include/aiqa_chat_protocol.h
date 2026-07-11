#pragma once

#include "aiqa_config.h"
#include "aiqa_provider.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_CHAT_ENDPOINT_MAX_LEN 224
#define AIQA_CHAT_REQUEST_MAX_LEN 2048
#define AIQA_CHAT_RESPONSE_TEXT_MAX_LEN 512

typedef enum {
    AIQA_CHAT_OK = 0,
    AIQA_CHAT_ERR_INVALID_ARG,
    AIQA_CHAT_ERR_BUFFER_TOO_SMALL,
    AIQA_CHAT_ERR_UNSUPPORTED_PROVIDER,
    AIQA_CHAT_ERR_AUTH,
    AIQA_CHAT_ERR_RATE_LIMITED,
    AIQA_CHAT_ERR_TIMEOUT,
    AIQA_CHAT_ERR_PROVIDER,
    AIQA_CHAT_ERR_PARSE,
} aiqa_chat_status_t;

aiqa_chat_status_t aiqa_chat_build_endpoint_url(
    const char *base_url,
    char *out_url,
    size_t out_url_size);

aiqa_chat_status_t aiqa_chat_build_request_json(
    const aiqa_config_t *config,
    const aiqa_chat_options_t *options,
    const char *user_text,
    char *out_json,
    size_t out_json_size);

aiqa_chat_status_t aiqa_chat_parse_response_text(
    const char *response_json,
    char *out_text,
    size_t out_text_size);

aiqa_chat_status_t aiqa_chat_status_from_http_status(int http_status);
const char *aiqa_chat_status_name(aiqa_chat_status_t status);

#ifdef __cplusplus
}
#endif
