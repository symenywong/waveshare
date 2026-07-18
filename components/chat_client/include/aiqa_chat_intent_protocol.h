#pragma once

#include "aiqa_chat_protocol.h"
#include "aiqa_device_intent.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

aiqa_chat_status_t aiqa_chat_build_intent_request_json(
    const aiqa_config_t *config,
    const char *transcript,
    char *out_json,
    size_t out_json_size);

aiqa_chat_status_t aiqa_chat_parse_intent_response_json(
    const char *response_json,
    size_t response_length,
    const char *transcript,
    aiqa_device_intent_t *out_intent);

#ifdef __cplusplus
}
#endif
