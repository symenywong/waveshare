#pragma once

#include "aiqa_config.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_ASR_ENDPOINT_MAX_LEN 224
#define AIQA_ASR_REQUEST_MAX_LEN 2048
#define AIQA_ASR_RESPONSE_TEXT_MAX_LEN 512
#define AIQA_ASR_STATIC_SAMPLE_URL "https://dashscope.oss-cn-beijing.aliyuncs.com/audios/welcome.mp3"

typedef enum {
    AIQA_ASR_OK = 0,
    AIQA_ASR_ERR_INVALID_ARG,
    AIQA_ASR_ERR_BUFFER_TOO_SMALL,
    AIQA_ASR_ERR_UNSUPPORTED_PROVIDER,
    AIQA_ASR_ERR_AUTH,
    AIQA_ASR_ERR_RATE_LIMITED,
    AIQA_ASR_ERR_TIMEOUT,
    AIQA_ASR_ERR_PROVIDER,
    AIQA_ASR_ERR_PARSE,
} aiqa_asr_status_t;

typedef struct {
    const char *audio_ref;
    const char *language_hint;
    bool enable_itn;
} aiqa_asr_options_t;

aiqa_asr_status_t aiqa_asr_build_endpoint_url(
    const char *base_url,
    char *out_url,
    size_t out_url_size);

aiqa_asr_status_t aiqa_asr_build_request_json(
    const aiqa_config_t *config,
    const aiqa_asr_options_t *options,
    char *out_json,
    size_t out_json_size);

aiqa_asr_status_t aiqa_asr_parse_transcript_text(
    const char *response_json,
    char *out_text,
    size_t out_text_size);

aiqa_asr_status_t aiqa_asr_status_from_http_status(int http_status);
const char *aiqa_asr_status_name(aiqa_asr_status_t status);

#ifdef __cplusplus
}
#endif
