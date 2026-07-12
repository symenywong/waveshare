#include "aiqa_tts_protocol.h"

#include "aiqa_provider.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *DASHSCOPE_TTS_GENERATION_PATH =
    "/api/v1/services/aigc/multimodal-generation/generation";

static aiqa_tts_status_t append_char(char *out, size_t out_size, size_t *pos, char value)
{
    if (*pos + 1 >= out_size) {
        return AIQA_TTS_ERR_BUFFER_TOO_SMALL;
    }

    out[*pos] = value;
    *pos += 1;
    out[*pos] = '\0';
    return AIQA_TTS_OK;
}

static aiqa_tts_status_t append_raw(char *out, size_t out_size, size_t *pos, const char *value)
{
    if (value == NULL) {
        return AIQA_TTS_ERR_INVALID_ARG;
    }

    const size_t len = strlen(value);
    if (*pos + len >= out_size) {
        return AIQA_TTS_ERR_BUFFER_TOO_SMALL;
    }

    (void)memcpy(out + *pos, value, len);
    *pos += len;
    out[*pos] = '\0';
    return AIQA_TTS_OK;
}

static aiqa_tts_status_t append_escaped_json_string(char *out, size_t out_size, size_t *pos, const char *value)
{
    aiqa_tts_status_t status = append_char(out, out_size, pos, '"');
    if (status != AIQA_TTS_OK) {
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
        if (status != AIQA_TTS_OK) {
            return status;
        }
    }

    return append_char(out, out_size, pos, '"');
}

static aiqa_tts_status_t copy_json_string_value(const char *start, char *out_text, size_t out_text_size)
{
    if (start == NULL || out_text == NULL || out_text_size == 0 || *start != '"') {
        return AIQA_TTS_ERR_PARSE;
    }

    size_t pos = 0;
    for (const char *cursor = start + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '"') {
            out_text[pos] = '\0';
            return AIQA_TTS_OK;
        }
        if (pos + 1 >= out_text_size) {
            out_text[0] = '\0';
            return AIQA_TTS_ERR_BUFFER_TOO_SMALL;
        }

        if (*cursor != '\\') {
            out_text[pos++] = *cursor;
            continue;
        }

        ++cursor;
        if (*cursor == '\0') {
            return AIQA_TTS_ERR_PARSE;
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
            return AIQA_TTS_ERR_PARSE;
        }
    }

    return AIQA_TTS_ERR_PARSE;
}

static aiqa_tts_status_t copy_https_origin(const char *base_url, char *out_origin, size_t out_origin_size)
{
    const char *prefix = "https://";
    const size_t prefix_len = strlen(prefix);
    if (base_url == NULL || out_origin == NULL || out_origin_size == 0 ||
        strncmp(base_url, prefix, prefix_len) != 0) {
        return AIQA_TTS_ERR_INVALID_ARG;
    }

    const char *host_start = base_url + prefix_len;
    if (*host_start == '\0' || *host_start == '/') {
        return AIQA_TTS_ERR_INVALID_ARG;
    }

    const char *path_start = strchr(host_start, '/');
    const size_t origin_len = path_start == NULL ? strlen(base_url) : (size_t)(path_start - base_url);
    if (origin_len >= out_origin_size) {
        return AIQA_TTS_ERR_BUFFER_TOO_SMALL;
    }

    (void)memcpy(out_origin, base_url, origin_len);
    out_origin[origin_len] = '\0';
    return AIQA_TTS_OK;
}

aiqa_tts_status_t aiqa_tts_build_endpoint_url(
    const char *base_url,
    char *out_url,
    size_t out_url_size)
{
    if (base_url == NULL || out_url == NULL || out_url_size == 0) {
        return AIQA_TTS_ERR_INVALID_ARG;
    }

    out_url[0] = '\0';
    char origin[AIQA_MAX_BASE_URL_LEN] = {0};
    aiqa_tts_status_t status = copy_https_origin(base_url, origin, sizeof(origin));
    if (status != AIQA_TTS_OK) {
        return status;
    }

    const int written = snprintf(out_url, out_url_size, "%s%s", origin, DASHSCOPE_TTS_GENERATION_PATH);
    if (written < 0 || (size_t)written >= out_url_size) {
        out_url[0] = '\0';
        return AIQA_TTS_ERR_BUFFER_TOO_SMALL;
    }

    return AIQA_TTS_OK;
}

aiqa_tts_status_t aiqa_tts_build_request_json(
    const aiqa_config_t *config,
    const aiqa_tts_options_t *options,
    const char *text,
    char *out_json,
    size_t out_json_size)
{
    if (config == NULL || options == NULL || text == NULL || out_json == NULL || out_json_size == 0 ||
        options->voice == NULL || options->format == NULL) {
        return AIQA_TTS_ERR_INVALID_ARG;
    }
    if (!aiqa_provider_model_allowed(config->tts_provider, config->tts_model)) {
        return AIQA_TTS_ERR_UNSUPPORTED_PROVIDER;
    }

    out_json[0] = '\0';
    size_t pos = 0;
    aiqa_tts_status_t status = append_raw(out_json, out_json_size, &pos, "{\"model\":");
    if (status != AIQA_TTS_OK) {
        return status;
    }
    status = append_escaped_json_string(out_json, out_json_size, &pos, config->tts_model);
    if (status != AIQA_TTS_OK) {
        return status;
    }
    status = append_raw(out_json, out_json_size, &pos, ",\"input\":{\"text\":");
    if (status != AIQA_TTS_OK) {
        return status;
    }
    status = append_escaped_json_string(out_json, out_json_size, &pos, text);
    if (status != AIQA_TTS_OK) {
        return status;
    }
    status = append_raw(out_json, out_json_size, &pos, ",\"voice\":");
    if (status != AIQA_TTS_OK) {
        return status;
    }
    status = append_escaped_json_string(out_json, out_json_size, &pos, options->voice);
    if (status != AIQA_TTS_OK) {
        return status;
    }
    return append_raw(out_json, out_json_size, &pos, ",\"language_type\":\"Auto\"}}");
}

aiqa_tts_status_t aiqa_tts_parse_stream_audio_data(
    const char *stream_chunk,
    char *out_audio_b64,
    size_t out_audio_b64_size)
{
    if (stream_chunk == NULL || out_audio_b64 == NULL || out_audio_b64_size == 0) {
        return AIQA_TTS_ERR_INVALID_ARG;
    }
    out_audio_b64[0] = '\0';

    if (strstr(stream_chunk, "[DONE]") != NULL) {
        return AIQA_TTS_ERR_PARSE;
    }

    const char *audio_key = strstr(stream_chunk, "\"audio\"");
    if (audio_key == NULL) {
        return AIQA_TTS_ERR_PARSE;
    }
    const char *data_key = strstr(audio_key, "\"data\"");
    if (data_key == NULL) {
        return AIQA_TTS_ERR_PARSE;
    }

    const char *colon = strchr(data_key, ':');
    if (colon == NULL) {
        return AIQA_TTS_ERR_PARSE;
    }
    const char *value_start = colon + 1;
    while (*value_start == ' ' || *value_start == '\t' || *value_start == '\n' || *value_start == '\r') {
        ++value_start;
    }

    return copy_json_string_value(value_start, out_audio_b64, out_audio_b64_size);
}

aiqa_tts_status_t aiqa_tts_status_from_http_status(int http_status)
{
    if (http_status >= 200 && http_status < 300) {
        return AIQA_TTS_OK;
    }
    if (http_status == 401 || http_status == 403) {
        return AIQA_TTS_ERR_AUTH;
    }
    if (http_status == 408 || http_status == 504) {
        return AIQA_TTS_ERR_TIMEOUT;
    }
    if (http_status == 429) {
        return AIQA_TTS_ERR_RATE_LIMITED;
    }
    return AIQA_TTS_ERR_PROVIDER;
}

const char *aiqa_tts_status_name(aiqa_tts_status_t status)
{
    switch (status) {
    case AIQA_TTS_OK:
        return "OK";
    case AIQA_TTS_ERR_INVALID_ARG:
        return "INVALID_ARG";
    case AIQA_TTS_ERR_BUFFER_TOO_SMALL:
        return "BUFFER_TOO_SMALL";
    case AIQA_TTS_ERR_UNSUPPORTED_PROVIDER:
        return "UNSUPPORTED_PROVIDER";
    case AIQA_TTS_ERR_AUTH:
        return "AUTH";
    case AIQA_TTS_ERR_RATE_LIMITED:
        return "RATE_LIMITED";
    case AIQA_TTS_ERR_TIMEOUT:
        return "TIMEOUT";
    case AIQA_TTS_ERR_PROVIDER:
        return "PROVIDER";
    case AIQA_TTS_ERR_PARSE:
        return "PARSE";
    default:
        return "UNKNOWN";
    }
}
