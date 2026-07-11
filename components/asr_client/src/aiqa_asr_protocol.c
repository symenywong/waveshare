#include "aiqa_asr_protocol.h"

#include "aiqa_provider.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *CHAT_COMPLETIONS_PATH = "/chat/completions";

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

    const aiqa_provider_caps_t *caps = aiqa_provider_caps_for(config->asr_provider);
    if (caps == NULL || !aiqa_provider_model_allowed(config->asr_provider, config->asr_model) ||
        (caps->max_audio_bytes == 0 && !caps->supports_data_uri_audio && !caps->requires_public_audio_url)) {
        return AIQA_ASR_ERR_UNSUPPORTED_PROVIDER;
    }

    out_json[0] = '\0';
    size_t pos = 0;
    aiqa_asr_status_t status = append_raw(out_json, out_json_size, &pos, "{\"model\":");
    if (status != AIQA_ASR_OK) {
        return status;
    }
    status = append_escaped_json_string(out_json, out_json_size, &pos, config->asr_model);
    if (status != AIQA_ASR_OK) {
        return status;
    }
    status = append_raw(out_json,
                        out_json_size,
                        &pos,
                        ",\"stream\":false,\"messages\":[{\"role\":\"user\",\"content\":["
                        "{\"type\":\"input_audio\",\"input_audio\":{\"data\":");
    if (status != AIQA_ASR_OK) {
        return status;
    }
    status = append_escaped_json_string(out_json, out_json_size, &pos, options->audio_ref);
    if (status != AIQA_ASR_OK) {
        return status;
    }
    status = append_raw(out_json, out_json_size, &pos, "}}]}],\"asr_options\":{");
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
