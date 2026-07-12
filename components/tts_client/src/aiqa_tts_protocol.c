#include "aiqa_tts_protocol.h"

#include "aiqa_provider.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *CHAT_COMPLETIONS_PATH = "/chat/completions";

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

aiqa_tts_status_t aiqa_tts_build_endpoint_url(
    const char *base_url,
    char *out_url,
    size_t out_url_size)
{
    if (base_url == NULL || out_url == NULL || out_url_size == 0) {
        return AIQA_TTS_ERR_INVALID_ARG;
    }

    out_url[0] = '\0';
    const size_t base_len = strlen(base_url);
    const bool has_trailing_slash = base_len > 0 && base_url[base_len - 1] == '/';
    const size_t path_offset = has_trailing_slash ? 1 : 0;
    const char *path = CHAT_COMPLETIONS_PATH + path_offset;
    const int written = snprintf(out_url, out_url_size, "%s%s", base_url, path);
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

    char options_json[192];
    const int written = snprintf(options_json,
                                 sizeof(options_json),
                                 ",\"stream\":%s,\"modalities\":[\"audio\"],\"audio\":{\"voice\":",
                                 options->stream ? "true" : "false");
    if (written < 0 || (size_t)written >= sizeof(options_json)) {
        return AIQA_TTS_ERR_BUFFER_TOO_SMALL;
    }
    status = append_raw(out_json, out_json_size, &pos, options_json);
    if (status != AIQA_TTS_OK) {
        return status;
    }
    status = append_escaped_json_string(out_json, out_json_size, &pos, options->voice);
    if (status != AIQA_TTS_OK) {
        return status;
    }
    status = append_raw(out_json, out_json_size, &pos, ",\"format\":");
    if (status != AIQA_TTS_OK) {
        return status;
    }
    status = append_escaped_json_string(out_json, out_json_size, &pos, options->format);
    if (status != AIQA_TTS_OK) {
        return status;
    }
    status = append_raw(out_json, out_json_size, &pos, "},\"messages\":[{\"role\":\"user\",\"content\":");
    if (status != AIQA_TTS_OK) {
        return status;
    }
    status = append_escaped_json_string(out_json, out_json_size, &pos, text);
    if (status != AIQA_TTS_OK) {
        return status;
    }
    return append_raw(out_json, out_json_size, &pos, "}]}");
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

    return copy_json_string_value(value_start, out_audio_b64, out_audio_b64_size)
               ? AIQA_TTS_OK
               : AIQA_TTS_ERR_PARSE;
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
