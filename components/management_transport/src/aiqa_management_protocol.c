#include "aiqa_management_protocol.h"

#include "aiqa_management_wire.h"
#include "cJSON.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define AIQA_MANAGEMENT_JSON_MAX_DEPTH 1U

static bool is_json_whitespace(uint8_t byte)
{
    return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}

static int json_hex_value(uint8_t byte)
{
    if (byte >= '0' && byte <= '9') {
        return byte - '0';
    }
    if (byte >= 'a' && byte <= 'f') {
        return byte - 'a' + 10;
    }
    if (byte >= 'A' && byte <= 'F') {
        return byte - 'A' + 10;
    }
    return -1;
}

static bool json_escape_is_safe(
    const uint8_t *payload,
    size_t payload_length,
    size_t escape_offset)
{
    const uint8_t escape = payload[escape_offset];
    if (escape != 'u') {
        return escape == '"' || escape == '\\' || escape == '/' ||
               escape == 'b' || escape == 'f' || escape == 'n' ||
               escape == 'r' || escape == 't';
    }
    if (escape_offset + 4U >= payload_length) {
        return false;
    }

    uint16_t code_unit = 0;
    for (size_t index = 1; index <= 4; ++index) {
        const int value = json_hex_value(payload[escape_offset + index]);
        if (value < 0) {
            return false;
        }
        code_unit = (uint16_t)((code_unit << 4U) | (uint16_t)value);
    }
    return code_unit != 0;
}

/* Bounds recursive cJSON work and rejects decoded C-string truncation before parsing. */
static bool payload_is_safe_json(const uint8_t *payload, size_t payload_length)
{
    size_t depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (size_t offset = 0; offset < payload_length; ++offset) {
        const uint8_t byte = payload[offset];
        if (in_string) {
            if (escaped) {
                if (!json_escape_is_safe(payload, payload_length, offset)) {
                    return false;
                }
                escaped = false;
            } else if (byte == '\\') {
                escaped = true;
            } else if (byte == '"') {
                in_string = false;
            } else if (byte < 0x20U) {
                return false;
            }
            continue;
        }

        if (byte == '"') {
            in_string = true;
        } else if (byte == '{' || byte == '[') {
            depth += 1U;
            if (depth > AIQA_MANAGEMENT_JSON_MAX_DEPTH) {
                return false;
            }
        } else if (byte == '}' || byte == ']') {
            if (depth == 0) {
                return false;
            }
            depth -= 1U;
        }
    }
    return !in_string && !escaped && depth == 0;
}

static bool write_response(
    char *out_response,
    size_t response_capacity,
    size_t *out_response_length,
    const char *format,
    uint32_t request_id)
{
    const int written = snprintf(
        out_response, response_capacity, format, (unsigned int)request_id);
    if (written < 0 || (size_t)written >= response_capacity) {
        if (response_capacity > 0) {
            out_response[0] = '\0';
        }
        *out_response_length = 0;
        return false;
    }
    *out_response_length = (size_t)written;
    return true;
}

static bool write_invalid_request(
    char *out_response,
    size_t response_capacity,
    size_t *out_response_length)
{
    return write_response(
        out_response,
        response_capacity,
        out_response_length,
        "{\"v\":1,\"id\":%u,\"ok\":false,\"error\":{"
        "\"code\":\"INVALID_REQUEST\",\"message\":\"Request rejected\"}}",
        0);
}

static bool parse_request(
    const uint8_t *payload,
    size_t payload_length,
    cJSON **out_root,
    uint32_t *out_request_id,
    const char **out_method)
{
    if (payload_length == 0 ||
        payload_length > AIQA_MANAGEMENT_WIRE_MAX_PAYLOAD ||
        memchr(payload, '\0', payload_length) != NULL ||
        !payload_is_safe_json(payload, payload_length)) {
        return false;
    }

    const char *parse_end = NULL;
    const char *payload_end = (const char *)payload + payload_length;
    cJSON *root = cJSON_ParseWithLengthOpts(
        (const char *)payload, payload_length, &parse_end, false);
    while (parse_end != NULL && parse_end < payload_end &&
           is_json_whitespace((uint8_t)*parse_end)) {
        ++parse_end;
    }
    if (root == NULL || parse_end != payload_end || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const cJSON *id_item = NULL;
    const cJSON *method_item = NULL;
    size_t field_count = 0;
    const cJSON *field = NULL;
    cJSON_ArrayForEach(field, root)
    {
        ++field_count;
        if (field->string == NULL) {
            cJSON_Delete(root);
            return false;
        }
        if (strcmp(field->string, "id") == 0 && id_item == NULL) {
            id_item = field;
        } else if (strcmp(field->string, "method") == 0 && method_item == NULL) {
            method_item = field;
        } else {
            cJSON_Delete(root);
            return false;
        }
    }

    if (field_count != 2 || !cJSON_IsNumber(id_item) ||
        !cJSON_IsString(method_item) || method_item->valuestring == NULL) {
        cJSON_Delete(root);
        return false;
    }

    const double id_value = id_item->valuedouble;
    if (id_value < 1.0 || id_value > (double)UINT32_MAX) {
        cJSON_Delete(root);
        return false;
    }
    const uint32_t request_id = (uint32_t)id_value;
    if ((double)request_id != id_value) {
        cJSON_Delete(root);
        return false;
    }

    *out_root = root;
    *out_request_id = request_id;
    *out_method = method_item->valuestring;
    return true;
}

bool aiqa_management_protocol_handle_public_request(
    const uint8_t *payload,
    size_t payload_length,
    char *out_response,
    size_t response_capacity,
    size_t *out_response_length)
{
    if (payload == NULL || out_response == NULL || out_response_length == NULL ||
        response_capacity == 0) {
        return false;
    }
    out_response[0] = '\0';
    *out_response_length = 0;

    cJSON *root = NULL;
    uint32_t request_id = 0;
    const char *method = NULL;
    if (!parse_request(
            payload, payload_length, &root, &request_id, &method)) {
        return write_invalid_request(
            out_response, response_capacity, out_response_length);
    }

    bool produced = false;
    if (strcmp(method, "system.hello") == 0) {
        produced = write_response(
            out_response,
            response_capacity,
            out_response_length,
            "{\"v\":1,\"id\":%u,\"ok\":true,\"result\":{"
            "\"protocol\":\"aiqa-management\",\"version\":1,"
            "\"maxFrameBytes\":4096,"
            "\"authentication\":\"authentication_required\"}}",
            request_id);
    } else {
        produced = write_response(
            out_response,
            response_capacity,
            out_response_length,
            "{\"v\":1,\"id\":%u,\"ok\":false,\"error\":{"
            "\"code\":\"AUTHENTICATION_REQUIRED\","
            "\"message\":\"Authenticated session required\"}}",
            request_id);
    }
    cJSON_Delete(root);
    return produced;
}
