#include "aiqa_asr_protocol.h"

#include "aiqa_provider.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *CHAT_COMPLETIONS_PATH = "/chat/completions";
static const char *REQUEST_AUDIO_PREFIX =
    ",\"stream\":false,\"messages\":[{\"role\":\"user\",\"content\":["
    "{\"type\":\"input_audio\",\"input_audio\":{\"data\":";

static aiqa_asr_status_t append_char(char *out, size_t out_size, size_t *pos, char value)
{
    if (*pos + 1 >= out_size) {
        return AIQA_ASR_ERR_BUFFER_TOO_SMALL;
    }
    out[*pos] = value;
    *pos += 1;
    out[*pos] = '\0';
    return AIQA_ASR_OK;
}

static aiqa_asr_status_t append_raw(char *out, size_t out_size, size_t *pos, const char *value)
{
    if (value == NULL) {
        return AIQA_ASR_ERR_INVALID_ARG;
    }

    const size_t len = strlen(value);
    if (*pos + len >= out_size) {
        return AIQA_ASR_ERR_BUFFER_TOO_SMALL;
    }

    (void)memcpy(out + *pos, value, len);
    *pos += len;
    out[*pos] = '\0';
    return AIQA_ASR_OK;
}

static aiqa_asr_status_t append_escaped_json_string(char *out, size_t out_size, size_t *pos, const char *value)
{
    aiqa_asr_status_t status = append_char(out, out_size, pos, '"');
    if (status != AIQA_ASR_OK) {
        return status;
    }

    for (const char *cursor = value; cursor != NULL && *cursor != '\0'; ++cursor) {
        switch (*cursor) {
        case '"':
            status = append_raw(out, out_size, pos, "\\\"");
            break;
        case '\\':
            status = append_raw(out, out_size, pos, "\\\\");
            break;
        case '\n':
            status = append_raw(out, out_size, pos, "\\n");
            break;
        case '\r':
            status = append_raw(out, out_size, pos, "\\r");
            break;
        case '\t':
            status = append_raw(out, out_size, pos, "\\t");
            break;
        default:
            status = append_char(out, out_size, pos, *cursor);
            break;
        }
        if (status != AIQA_ASR_OK) {
            return status;
        }
    }

    return append_char(out, out_size, pos, '"');
}

static aiqa_audio_source_kind_t infer_audio_source_kind(const aiqa_asr_options_t *options)
{
    if (options == NULL) {
        return AIQA_AUDIO_SOURCE_MEMORY_STREAM;
    }
    if (options->audio_source_kind != AIQA_AUDIO_SOURCE_MEMORY_STREAM) {
        return options->audio_source_kind;
    }
    if (options->audio_ref == NULL) {
        return AIQA_AUDIO_SOURCE_MEMORY_STREAM;
    }
    if (strncmp(options->audio_ref, "data:", 5) == 0) {
        return AIQA_AUDIO_SOURCE_DATA_URI;
    }
    return AIQA_AUDIO_SOURCE_PUBLIC_URL;
}

static aiqa_asr_status_t append_request_open_json(
    const aiqa_config_t *config,
    char *out_json,
    size_t out_json_size,
    size_t *pos)
{
    aiqa_asr_status_t status = append_raw(out_json, out_json_size, pos, "{\"model\":");
    if (status != AIQA_ASR_OK) {
        return status;
    }
    status = append_escaped_json_string(out_json, out_json_size, pos, config->asr_model);
    if (status != AIQA_ASR_OK) {
        return status;
    }
    return append_raw(out_json, out_json_size, pos, REQUEST_AUDIO_PREFIX);
}

aiqa_asr_status_t aiqa_asr_build_endpoint_url(
    const char *base_url,
    char *out_url,
    size_t out_url_size)
{
    if (base_url == NULL || out_url == NULL || out_url_size == 0) {
        return AIQA_ASR_ERR_INVALID_ARG;
    }

    out_url[0] = '\0';
    const size_t base_len = strlen(base_url);
    const bool has_trailing_slash = base_len > 0 && base_url[base_len - 1] == '/';
    const size_t path_offset = has_trailing_slash ? 1 : 0;
    const int written = snprintf(out_url, out_url_size, "%s%s", base_url, CHAT_COMPLETIONS_PATH + path_offset);
    if (written < 0 || (size_t)written >= out_url_size) {
        out_url[0] = '\0';
        return AIQA_ASR_ERR_BUFFER_TOO_SMALL;
    }
    return AIQA_ASR_OK;
}

aiqa_asr_status_t aiqa_asr_build_request_json(
    const aiqa_config_t *config,
    const aiqa_asr_options_t *options,
    char *out_json,
    size_t out_json_size)
{
    if (config == NULL || options == NULL || options->audio_ref == NULL ||
        out_json == NULL || out_json_size == 0) {
        return AIQA_ASR_ERR_INVALID_ARG;
    }

    aiqa_asr_status_t status = aiqa_asr_validate_audio_source(config, options);
    if (status != AIQA_ASR_OK) {
        return status;
    }

    out_json[0] = '\0';
    size_t pos = 0;
    status = append_request_open_json(config, out_json, out_json_size, &pos);
    if (status != AIQA_ASR_OK) {
        return status;
    }
    status = append_escaped_json_string(out_json, out_json_size, &pos, options->audio_ref);
    if (status != AIQA_ASR_OK) {
        return status;
    }
    char suffix[AIQA_ASR_REQUEST_PART_MAX_LEN] = {0};
    status = aiqa_asr_build_request_suffix_json(options, suffix, sizeof(suffix));
    if (status != AIQA_ASR_OK) {
        return status;
    }
    return append_raw(out_json, out_json_size, &pos, suffix);
}

aiqa_asr_status_t aiqa_asr_build_data_uri_request_prefix_json(
    const aiqa_config_t *config,
    char *out_json,
    size_t out_json_size)
{
    if (config == NULL || out_json == NULL || out_json_size == 0) {
        return AIQA_ASR_ERR_INVALID_ARG;
    }

    out_json[0] = '\0';
    size_t pos = 0;
    aiqa_asr_status_t status = append_request_open_json(config, out_json, out_json_size, &pos);
    if (status != AIQA_ASR_OK) {
        return status;
    }
    return append_raw(out_json, out_json_size, &pos, "\"" AIQA_ASR_DATA_URI_PREFIX);
}

aiqa_asr_status_t aiqa_asr_build_request_suffix_json(
    const aiqa_asr_options_t *options,
    char *out_json,
    size_t out_json_size)
{
    if (options == NULL || out_json == NULL || out_json_size == 0) {
        return AIQA_ASR_ERR_INVALID_ARG;
    }

    out_json[0] = '\0';
    size_t pos = 0;
    aiqa_asr_status_t status = append_raw(out_json, out_json_size, &pos, "}}]}],\"asr_options\":{");
    if (status != AIQA_ASR_OK) {
        return status;
    }
    if (options->language_hint != NULL && options->language_hint[0] != '\0') {
        status = append_raw(out_json, out_json_size, &pos, "\"language\":");
        if (status != AIQA_ASR_OK) {
            return status;
        }
        status = append_escaped_json_string(out_json, out_json_size, &pos, options->language_hint);
        if (status != AIQA_ASR_OK) {
            return status;
        }
        status = append_raw(out_json, out_json_size, &pos, ",");
        if (status != AIQA_ASR_OK) {
            return status;
        }
    }

    char options_json[48];
    const int written = snprintf(options_json,
                                 sizeof(options_json),
                                 "\"enable_itn\":%s}}",
                                 options->enable_itn ? "true" : "false");
    if (written < 0 || (size_t)written >= sizeof(options_json)) {
        return AIQA_ASR_ERR_BUFFER_TOO_SMALL;
    }
    return append_raw(out_json, out_json_size, &pos, options_json);
}

aiqa_asr_status_t aiqa_asr_validate_audio_source(
    const aiqa_config_t *config,
    const aiqa_asr_options_t *options)
{
    if (config == NULL || options == NULL) {
        return AIQA_ASR_ERR_INVALID_ARG;
    }

    const aiqa_provider_caps_t *caps = aiqa_provider_caps_for(config->asr_provider);
    if (caps == NULL || !aiqa_provider_model_allowed(config->asr_provider, config->asr_model) ||
        (caps->max_audio_bytes == 0 && !caps->supports_data_uri_audio && !caps->requires_public_audio_url)) {
        return AIQA_ASR_ERR_UNSUPPORTED_PROVIDER;
    }

    const aiqa_audio_source_kind_t source_kind = infer_audio_source_kind(options);
    if (source_kind == AIQA_AUDIO_SOURCE_DATA_URI && !caps->supports_data_uri_audio) {
        return AIQA_ASR_ERR_UNSUPPORTED_PROVIDER;
    }
    if (source_kind == AIQA_AUDIO_SOURCE_PUBLIC_URL && options->audio_ref == NULL) {
        return AIQA_ASR_ERR_INVALID_ARG;
    }
    if (source_kind == AIQA_AUDIO_SOURCE_MEMORY_STREAM && options->audio_ref != NULL) {
        return AIQA_ASR_ERR_INVALID_ARG;
    }
    if (options->audio_bytes > 0 && caps->max_audio_bytes > 0 && options->audio_bytes > caps->max_audio_bytes) {
        return AIQA_ASR_ERR_UNSUPPORTED_PROVIDER;
    }
    return AIQA_ASR_OK;
}

size_t aiqa_asr_base64_encoded_len(size_t raw_bytes)
{
    if (raw_bytes > (SIZE_MAX / 4u) * 3u) {
        return 0;
    }
    return ((raw_bytes + 2u) / 3u) * 4u;
}

size_t aiqa_asr_wav_total_bytes(size_t pcm_bytes)
{
    if (pcm_bytes > SIZE_MAX - AIQA_ASR_WAV_HEADER_BYTES) {
        return 0;
    }
    return pcm_bytes + AIQA_ASR_WAV_HEADER_BYTES;
}

static void write_le16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void write_le32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)((value >> 8) & 0xffu);
    out[2] = (uint8_t)((value >> 16) & 0xffu);
    out[3] = (uint8_t)((value >> 24) & 0xffu);
}

aiqa_asr_status_t aiqa_asr_write_wav_header(
    uint8_t out_header[AIQA_ASR_WAV_HEADER_BYTES],
    uint32_t sample_rate_hz,
    uint16_t bits_per_sample,
    uint16_t channels,
    size_t pcm_bytes)
{
    if (out_header == NULL || sample_rate_hz == 0 || bits_per_sample == 0 || channels == 0 ||
        (bits_per_sample % 8u) != 0u || pcm_bytes == 0 || pcm_bytes > UINT32_MAX - 36u) {
        return AIQA_ASR_ERR_INVALID_ARG;
    }

    const uint32_t bytes_per_sample = (uint32_t)bits_per_sample / 8u;
    const uint32_t byte_rate = sample_rate_hz * (uint32_t)channels * bytes_per_sample;
    const uint16_t block_align = (uint16_t)((uint32_t)channels * bytes_per_sample);
    if (block_align == 0 || (pcm_bytes % block_align) != 0) {
        return AIQA_ASR_ERR_INVALID_ARG;
    }

    (void)memset(out_header, 0, AIQA_ASR_WAV_HEADER_BYTES);
    (void)memcpy(out_header + 0, "RIFF", 4);
    write_le32(out_header + 4, (uint32_t)(36u + pcm_bytes));
    (void)memcpy(out_header + 8, "WAVE", 4);
    (void)memcpy(out_header + 12, "fmt ", 4);
    write_le32(out_header + 16, 16u);
    write_le16(out_header + 20, 1u);
    write_le16(out_header + 22, channels);
    write_le32(out_header + 24, sample_rate_hz);
    write_le32(out_header + 28, byte_rate);
    write_le16(out_header + 32, block_align);
    write_le16(out_header + 34, bits_per_sample);
    (void)memcpy(out_header + 36, "data", 4);
    write_le32(out_header + 40, (uint32_t)pcm_bytes);
    return AIQA_ASR_OK;
}

static bool copy_json_string_value(const char *start, char *out_text, size_t out_text_size)
{
    if (start == NULL || out_text == NULL || out_text_size == 0 || *start != '"') {
        return false;
    }

    size_t pos = 0;
    for (const char *cursor = start + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '"') {
            out_text[pos] = '\0';
            return true;
        }
        if (pos + 1 >= out_text_size) {
            return false;
        }

        if (*cursor != '\\') {
            out_text[pos++] = *cursor;
            continue;
        }

        ++cursor;
        if (*cursor == '\0') {
            return false;
        }
        switch (*cursor) {
        case 'n':
            out_text[pos++] = '\n';
            break;
        case 'r':
            out_text[pos++] = '\r';
            break;
        case 't':
            out_text[pos++] = '\t';
            break;
        case '"':
        case '\\':
        case '/':
            out_text[pos++] = *cursor;
            break;
        default:
            return false;
        }
    }

    return false;
}

aiqa_asr_status_t aiqa_asr_parse_transcript_text(
    const char *response_json,
    char *out_text,
    size_t out_text_size)
{
    if (response_json == NULL || out_text == NULL || out_text_size == 0) {
        return AIQA_ASR_ERR_INVALID_ARG;
    }
    out_text[0] = '\0';

    const char *content_key = strstr(response_json, "\"content\"");
    if (content_key == NULL) {
        content_key = strstr(response_json, "\"text\"");
    }
    if (content_key == NULL) {
        return AIQA_ASR_ERR_PARSE;
    }

    const char *colon = strchr(content_key, ':');
    if (colon == NULL) {
        return AIQA_ASR_ERR_PARSE;
    }
    const char *value_start = colon + 1;
    while (*value_start == ' ' || *value_start == '\t' || *value_start == '\n' || *value_start == '\r') {
        ++value_start;
    }

    return copy_json_string_value(value_start, out_text, out_text_size) ? AIQA_ASR_OK : AIQA_ASR_ERR_PARSE;
}

aiqa_asr_status_t aiqa_asr_status_from_http_status(int http_status)
{
    if (http_status >= 200 && http_status < 300) {
        return AIQA_ASR_OK;
    }
    if (http_status == 401 || http_status == 403) {
        return AIQA_ASR_ERR_AUTH;
    }
    if (http_status == 408 || http_status == 504) {
        return AIQA_ASR_ERR_TIMEOUT;
    }
    if (http_status == 429) {
        return AIQA_ASR_ERR_RATE_LIMITED;
    }
    return AIQA_ASR_ERR_PROVIDER;
}

const char *aiqa_asr_status_name(aiqa_asr_status_t status)
{
    switch (status) {
    case AIQA_ASR_OK:
        return "OK";
    case AIQA_ASR_ERR_INVALID_ARG:
        return "INVALID_ARG";
    case AIQA_ASR_ERR_BUFFER_TOO_SMALL:
        return "BUFFER_TOO_SMALL";
    case AIQA_ASR_ERR_UNSUPPORTED_PROVIDER:
        return "UNSUPPORTED_PROVIDER";
    case AIQA_ASR_ERR_AUTH:
        return "AUTH";
    case AIQA_ASR_ERR_RATE_LIMITED:
        return "RATE_LIMITED";
    case AIQA_ASR_ERR_TIMEOUT:
        return "TIMEOUT";
    case AIQA_ASR_ERR_PROVIDER:
        return "PROVIDER";
    case AIQA_ASR_ERR_PARSE:
        return "PARSE";
    default:
        return "UNKNOWN";
    }
}
