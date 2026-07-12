#pragma once

#include "aiqa_config.h"
#include "aiqa_provider.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_ASR_ENDPOINT_MAX_LEN 224
#define AIQA_ASR_REQUEST_MAX_LEN 2048
#define AIQA_ASR_REQUEST_PART_MAX_LEN 512
#define AIQA_ASR_RESPONSE_TEXT_MAX_LEN 512
#define AIQA_ASR_STATIC_SAMPLE_URL "https://dashscope.oss-cn-beijing.aliyuncs.com/audios/welcome.mp3"
#define AIQA_ASR_WAV_HEADER_BYTES 44u
#define AIQA_ASR_DATA_URI_PREFIX "data:audio/wav;base64,"

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
    aiqa_audio_source_kind_t audio_source_kind;
    size_t audio_bytes;
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

aiqa_asr_status_t aiqa_asr_build_data_uri_request_prefix_json(
    const aiqa_config_t *config,
    char *out_json,
    size_t out_json_size);

aiqa_asr_status_t aiqa_asr_build_request_suffix_json(
    const aiqa_asr_options_t *options,
    char *out_json,
    size_t out_json_size);

aiqa_asr_status_t aiqa_asr_validate_audio_source(
    const aiqa_config_t *config,
    const aiqa_asr_options_t *options);

size_t aiqa_asr_base64_encoded_len(size_t raw_bytes);
size_t aiqa_asr_wav_total_bytes(size_t pcm_bytes);

aiqa_asr_status_t aiqa_asr_write_wav_header(
    uint8_t out_header[AIQA_ASR_WAV_HEADER_BYTES],
    uint32_t sample_rate_hz,
    uint16_t bits_per_sample,
    uint16_t channels,
    size_t pcm_bytes);

aiqa_asr_status_t aiqa_asr_parse_transcript_text(
    const char *response_json,
    char *out_text,
    size_t out_text_size);

aiqa_asr_status_t aiqa_asr_status_from_http_status(int http_status);
const char *aiqa_asr_status_name(aiqa_asr_status_t status);

#ifdef __cplusplus
}
#endif
