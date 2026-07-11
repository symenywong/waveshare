#include "aiqa_chat_protocol.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *CHAT_COMPLETIONS_PATH = "/chat/completions";
static const char *PET_SYSTEM_PROMPT =
    "You are an AI electronic pet on a tiny round screen. "
    "Answer warmly, briefly, and use playful companion language.";

static aiqa_chat_status_t append_char(char *out, size_t out_size, size_t *pos, char value)
{
    if (*pos + 1 >= out_size) {
        return AIQA_CHAT_ERR_BUFFER_TOO_SMALL;
    }

    out[*pos] = value;
    *pos += 1;
    out[*pos] = '\0';
    return AIQA_CHAT_OK;
}

static aiqa_chat_status_t append_raw(char *out, size_t out_size, size_t *pos, const char *value)
{
    if (value == NULL) {
        return AIQA_CHAT_ERR_INVALID_ARG;
    }

    const size_t len = strlen(value);
    if (*pos + len >= out_size) {
        return AIQA_CHAT_ERR_BUFFER_TOO_SMALL;
    }

    (void)memcpy(out + *pos, value, len);
    *pos += len;
    out[*pos] = '\0';
    return AIQA_CHAT_OK;
}

static aiqa_chat_status_t append_escaped_json_string(char *out, size_t out_size, size_t *pos, const char *value)
{
    aiqa_chat_status_t status = append_char(out, out_size, pos, '"');
    if (status != AIQA_CHAT_OK) {
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
        if (status != AIQA_CHAT_OK) {
            return status;
        }
    }

    return append_char(out, out_size, pos, '"');
}

aiqa_chat_status_t aiqa_chat_build_endpoint_url(
    const char *base_url,
    char *out_url,
    size_t out_url_size)
{
    if (base_url == NULL || out_url == NULL || out_url_size == 0) {
        return AIQA_CHAT_ERR_INVALID_ARG;
    }

    out_url[0] = '\0';
    const size_t base_len = strlen(base_url);
    const bool has_trailing_slash = base_len > 0 && base_url[base_len - 1] == '/';
    const size_t path_offset = has_trailing_slash ? 1 : 0;
    const char *path = CHAT_COMPLETIONS_PATH + path_offset;
    const int written = snprintf(out_url, out_url_size, "%s%s", base_url, path);
    if (written < 0 || (size_t)written >= out_url_size) {
        out_url[0] = '\0';
        return AIQA_CHAT_ERR_BUFFER_TOO_SMALL;
    }

    return AIQA_CHAT_OK;
}

aiqa_chat_status_t aiqa_chat_build_request_json(
    const aiqa_config_t *config,
    const aiqa_chat_options_t *options,
    const char *user_text,
    char *out_json,
    size_t out_json_size)
{
    if (config == NULL || options == NULL || user_text == NULL || out_json == NULL || out_json_size == 0) {
        return AIQA_CHAT_ERR_INVALID_ARG;
    }
    if (!aiqa_provider_model_allowed(config->active_provider, config->model)) {
        return AIQA_CHAT_ERR_UNSUPPORTED_PROVIDER;
    }

    out_json[0] = '\0';
    size_t pos = 0;
    aiqa_chat_status_t status = append_raw(out_json, out_json_size, &pos, "{\"model\":");
    if (status != AIQA_CHAT_OK) {
        return status;
    }
    status = append_escaped_json_string(out_json, out_json_size, &pos, config->model);
    if (status != AIQA_CHAT_OK) {
        return status;
    }

    char options_json[160];
    int option_written = -1;
    if (strcmp(config->active_provider, AIQA_PROVIDER_DASHSCOPE_CHAT) == 0) {
        option_written = snprintf(options_json,
                                  sizeof(options_json),
                                  ",\"stream\":%s,\"max_tokens\":%d,\"enable_thinking\":%s,\"messages\":[",
                                  options->stream ? "true" : "false",
                                  options->max_completion_tokens,
                                  options->hide_reasoning ? "false" : "true");
    } else if (strcmp(config->active_provider, AIQA_PROVIDER_MINIMAX_CHAT) == 0) {
        option_written = snprintf(options_json,
                                  sizeof(options_json),
                                  ",\"stream\":%s,\"max_completion_tokens\":%d,\"reasoning_split\":%s,\"messages\":[",
                                  options->stream ? "true" : "false",
                                  options->max_completion_tokens,
                                  options->hide_reasoning ? "true" : "false");
    } else {
        return AIQA_CHAT_ERR_UNSUPPORTED_PROVIDER;
    }
    if (option_written < 0 || (size_t)option_written >= sizeof(options_json)) {
        return AIQA_CHAT_ERR_BUFFER_TOO_SMALL;
    }
    status = append_raw(out_json, out_json_size, &pos, options_json);
    if (status != AIQA_CHAT_OK) {
        return status;
    }

    status = append_raw(out_json, out_json_size, &pos, "{\"role\":\"system\",\"content\":");
    if (status != AIQA_CHAT_OK) {
        return status;
    }
    status = append_escaped_json_string(out_json, out_json_size, &pos, PET_SYSTEM_PROMPT);
    if (status != AIQA_CHAT_OK) {
        return status;
    }
    status = append_raw(out_json, out_json_size, &pos, "},{\"role\":\"user\",\"content\":");
    if (status != AIQA_CHAT_OK) {
        return status;
    }
    status = append_escaped_json_string(out_json, out_json_size, &pos, user_text);
    if (status != AIQA_CHAT_OK) {
        return status;
    }
    return append_raw(out_json, out_json_size, &pos, "}]}");
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

aiqa_chat_status_t aiqa_chat_parse_response_text(
    const char *response_json,
    char *out_text,
    size_t out_text_size)
{
    if (response_json == NULL || out_text == NULL || out_text_size == 0) {
        return AIQA_CHAT_ERR_INVALID_ARG;
    }
    out_text[0] = '\0';

    const char *content_key = strstr(response_json, "\"content\"");
    if (content_key == NULL) {
        return AIQA_CHAT_ERR_PARSE;
    }

    const char *colon = strchr(content_key, ':');
    if (colon == NULL) {
        return AIQA_CHAT_ERR_PARSE;
    }
    const char *value_start = colon + 1;
    while (*value_start == ' ' || *value_start == '\t' || *value_start == '\n' || *value_start == '\r') {
        ++value_start;
    }

    return copy_json_string_value(value_start, out_text, out_text_size) ? AIQA_CHAT_OK : AIQA_CHAT_ERR_PARSE;
}

aiqa_chat_status_t aiqa_chat_status_from_http_status(int http_status)
{
    if (http_status >= 200 && http_status < 300) {
        return AIQA_CHAT_OK;
    }
    if (http_status == 401 || http_status == 403) {
        return AIQA_CHAT_ERR_AUTH;
    }
    if (http_status == 408 || http_status == 504) {
        return AIQA_CHAT_ERR_TIMEOUT;
    }
    if (http_status == 429) {
        return AIQA_CHAT_ERR_RATE_LIMITED;
    }
    return AIQA_CHAT_ERR_PROVIDER;
}

const char *aiqa_chat_status_name(aiqa_chat_status_t status)
{
    switch (status) {
    case AIQA_CHAT_OK:
        return "OK";
    case AIQA_CHAT_ERR_INVALID_ARG:
        return "INVALID_ARG";
    case AIQA_CHAT_ERR_BUFFER_TOO_SMALL:
        return "BUFFER_TOO_SMALL";
    case AIQA_CHAT_ERR_UNSUPPORTED_PROVIDER:
        return "UNSUPPORTED_PROVIDER";
    case AIQA_CHAT_ERR_AUTH:
        return "AUTH";
    case AIQA_CHAT_ERR_RATE_LIMITED:
        return "RATE_LIMITED";
    case AIQA_CHAT_ERR_TIMEOUT:
        return "TIMEOUT";
    case AIQA_CHAT_ERR_PROVIDER:
        return "PROVIDER";
    case AIQA_CHAT_ERR_PARSE:
        return "PARSE";
    default:
        return "UNKNOWN";
    }
}
