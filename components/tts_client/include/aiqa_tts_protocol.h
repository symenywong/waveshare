#pragma once

#include "aiqa_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_TTS_ENDPOINT_MAX_LEN 224
#define AIQA_TTS_REQUEST_MAX_LEN 2048
#define AIQA_TTS_AUDIO_BASE64_MAX_LEN 16384
#define AIQA_TTS_DEFAULT_SAMPLE_RATE_HZ 24000u

typedef enum {
    AIQA_TTS_OK = 0,
    AIQA_TTS_ERR_INVALID_ARG,
    AIQA_TTS_ERR_BUFFER_TOO_SMALL,
    AIQA_TTS_ERR_UNSUPPORTED_PROVIDER,
    AIQA_TTS_ERR_AUTH,
    AIQA_TTS_ERR_RATE_LIMITED,
    AIQA_TTS_ERR_TIMEOUT,
    AIQA_TTS_ERR_PROVIDER,
    AIQA_TTS_ERR_PARSE,
} aiqa_tts_status_t;

typedef struct {
    const char *voice;
    const char *format;
    uint32_t sample_rate_hz;
    bool stream;
} aiqa_tts_options_t;

aiqa_tts_status_t aiqa_tts_build_endpoint_url(
    const char *base_url,
    char *out_url,
    size_t out_url_size);

aiqa_tts_status_t aiqa_tts_build_request_json(
    const aiqa_config_t *config,
    const aiqa_tts_options_t *options,
    const char *text,
    char *out_json,
    size_t out_json_size);

aiqa_tts_status_t aiqa_tts_parse_stream_audio_data(
    const char *stream_chunk,
    char *out_audio_b64,
    size_t out_audio_b64_size);

aiqa_tts_status_t aiqa_tts_status_from_http_status(int http_status);
const char *aiqa_tts_status_name(aiqa_tts_status_t status);

#ifdef __cplusplus
}
#endif
