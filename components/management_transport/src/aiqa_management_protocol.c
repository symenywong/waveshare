#include "aiqa_management_protocol.h"

#include "aiqa_management_wire.h"
#include "cJSON.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define AIQA_MANAGEMENT_PUBLIC_JSON_MAX_DEPTH 1U
#define AIQA_MANAGEMENT_AUTH_JSON_MAX_DEPTH 2U

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
static bool payload_is_safe_json(
    const uint8_t *payload,
    size_t payload_length,
    size_t maximum_depth)
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
            if (depth > maximum_depth) {
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
        !payload_is_safe_json(
            payload, payload_length, AIQA_MANAGEMENT_PUBLIC_JSON_MAX_DEPTH)) {
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

static void secure_zero(void *value, size_t value_size)
{
    volatile uint8_t *bytes = value;
    while (value != NULL && value_size > 0U) {
        *bytes++ = 0;
        value_size -= 1U;
    }
}

static const char *state_name(aiqa_state_t state)
{
    static const char *const names[] = {
        "BOOT", "CONFIG_CHECK", "NETWORK_CONNECTING", "IDLE", "RECORDING",
        "TRANSCRIBING", "ASR_JOB_PENDING", "THINKING", "IDLE_WITH_RESULT", "ERROR",
    };
    const unsigned index = (unsigned)state;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "ERROR";
}

static const char *error_name(aiqa_error_code_t error)
{
    static const char *const names[] = {
        "NONE", "CONFIG_MISSING", "CONFIG_CORRUPT", "NETWORK_FAILED",
        "AUTH_FAILED", "TLS_FAILED", "CERT_TIME_INVALID", "RATE_LIMITED",
        "PROVIDER_UNSUPPORTED", "AUDIO_TOO_LONG", "ASR_FAILED", "CHAT_FAILED",
        "TIMEOUT", "CANCELLED",
    };
    const unsigned index = (unsigned)error;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "CANCELLED";
}

static const char *charging_name(aiqa_management_charging_state_t state)
{
    static const char *const names[] = {"UNKNOWN", "DISCHARGING", "ACTIVE", "DONE"};
    const unsigned index = (unsigned)state;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "UNKNOWN";
}

static const char *operation_state_name(aiqa_management_operation_state_t state)
{
    static const char *const names[] = {"NONE", "PENDING", "SUCCEEDED", "FAILED"};
    const unsigned index = (unsigned)state;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "FAILED";
}

static const char *result_name(aiqa_management_result_t result)
{
    static const char *const names[] = {
        "OK", "INVALID_REQUEST", "FORBIDDEN", "NOT_READY", "REVISION_CONFLICT",
        "REVISION_EXHAUSTED", "BUSY", "WIFI_UNREACHABLE_ROLLED_BACK",
        "PERSISTENCE_FAILED_ROLLED_BACK", "RECOVERY_REQUIRED", "CANCELLED",
        "INTERNAL_ERROR",
    };
    const unsigned index = (unsigned)result;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "INTERNAL_ERROR";
}

static const char *gender_name(aiqa_assistant_gender_t gender)
{
    static const char *const names[] = {"neutral", "female", "male"};
    const unsigned index = (unsigned)gender;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "neutral";
}

static bool add_string(cJSON *object, const char *name, const char *value)
{
    return cJSON_AddStringToObject(object, name, value != NULL ? value : "") != NULL;
}

static bool add_nullable_string(
    cJSON *object,
    const char *name,
    const char *value,
    bool available)
{
    if (available && value != NULL && value[0] != '\0') {
        return add_string(object, name, value);
    }
    return cJSON_AddNullToObject(object, name) != NULL;
}

static bool print_envelope(
    cJSON *result,
    uint32_t request_id,
    char *out_response,
    size_t response_capacity,
    size_t *out_response_length)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL || cJSON_AddNumberToObject(root, "v", 1) == NULL ||
        cJSON_AddNumberToObject(root, "id", request_id) == NULL ||
        cJSON_AddBoolToObject(root, "ok", true) == NULL ||
        !cJSON_AddItemToObject(root, "result", result)) {
        cJSON_Delete(root);
        if (root == NULL) {
            cJSON_Delete(result);
        }
        return false;
    }
    const bool printed = response_capacity <= INT_MAX &&
                         cJSON_PrintPreallocated(
                             root, out_response, (int)response_capacity, false);
    if (printed) {
        *out_response_length = strlen(out_response);
    } else {
        out_response[0] = '\0';
        *out_response_length = 0;
    }
    cJSON_Delete(root);
    return printed;
}

static bool write_error(
    char *out_response,
    size_t response_capacity,
    size_t *out_response_length,
    uint32_t request_id,
    const char *code,
    const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *error = cJSON_CreateObject();
    if (root == NULL || error == NULL ||
        cJSON_AddNumberToObject(root, "v", 1) == NULL ||
        cJSON_AddNumberToObject(root, "id", request_id) == NULL ||
        cJSON_AddBoolToObject(root, "ok", false) == NULL ||
        !cJSON_AddItemToObject(root, "error", error) ||
        !add_string(error, "code", code) ||
        !add_string(error, "message", message)) {
        cJSON_Delete(root);
        if (root == NULL) {
            cJSON_Delete(error);
        }
        return false;
    }
    const bool printed = response_capacity <= INT_MAX &&
                         cJSON_PrintPreallocated(
                             root, out_response, (int)response_capacity, false);
    if (printed) {
        *out_response_length = strlen(out_response);
    } else {
        out_response[0] = '\0';
        *out_response_length = 0;
    }
    cJSON_Delete(root);
    return printed;
}

static bool object_has_exact_unique_fields(
    const cJSON *object,
    const char *const *allowed,
    size_t allowed_count,
    size_t required_count)
{
    if (!cJSON_IsObject(object)) {
        return false;
    }
    bool seen[8] = {false};
    if (allowed_count > sizeof(seen) / sizeof(seen[0])) {
        return false;
    }
    size_t count = 0;
    const cJSON *field = NULL;
    cJSON_ArrayForEach(field, object)
    {
        if (field->string == NULL) {
            return false;
        }
        size_t index = 0;
        while (index < allowed_count && strcmp(field->string, allowed[index]) != 0) {
            index += 1U;
        }
        if (index == allowed_count || seen[index]) {
            return false;
        }
        seen[index] = true;
        count += 1U;
    }
    if (count < required_count || count > allowed_count) {
        return false;
    }
    for (size_t index = 0; index < required_count; ++index) {
        if (!seen[index]) {
            return false;
        }
    }
    return true;
}

static bool parse_authenticated_root(
    const uint8_t *payload,
    size_t payload_length,
    cJSON **out_root,
    uint32_t *out_request_id,
    const char **out_method,
    const cJSON **out_params)
{
    if (payload == NULL || payload_length == 0U ||
        payload_length > AIQA_MANAGEMENT_WIRE_MAX_PAYLOAD ||
        memchr(payload, '\0', payload_length) != NULL ||
        !payload_is_safe_json(
            payload, payload_length, AIQA_MANAGEMENT_AUTH_JSON_MAX_DEPTH)) {
        return false;
    }
    const char *parse_end = NULL;
    const char *payload_end = (const char *)payload + payload_length;
    cJSON *root = cJSON_ParseWithLengthOpts(
        (const char *)payload, payload_length, &parse_end, false);
    while (parse_end != NULL && parse_end < payload_end &&
           is_json_whitespace((uint8_t)*parse_end)) {
        parse_end += 1;
    }
    static const char *const fields[] = {"id", "method", "params"};
    if (root == NULL || parse_end != payload_end ||
        !object_has_exact_unique_fields(root, fields, 3U, 2U)) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (!cJSON_IsNumber(id) || !cJSON_IsString(method) ||
        method->valuestring == NULL ||
        (params != NULL && !cJSON_IsObject(params))) {
        cJSON_Delete(root);
        return false;
    }
    const double id_value = id->valuedouble;
    if (id_value < 1.0 || id_value > (double)UINT32_MAX ||
        (double)(uint32_t)id_value != id_value) {
        cJSON_Delete(root);
        return false;
    }
    *out_root = root;
    *out_request_id = (uint32_t)id_value;
    *out_method = method->valuestring;
    *out_params = params;
    return true;
}

static cJSON *serialize_status(const aiqa_management_device_status_t *status)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();
    cJSON *power = cJSON_CreateObject();
    cJSON *ui = cJSON_CreateObject();
    cJSON *config = cJSON_CreateObject();
    cJSON *operation = cJSON_CreateObject();
    if (result == NULL || wifi == NULL || power == NULL || ui == NULL ||
        config == NULL || operation == NULL) {
        cJSON_Delete(result);
        cJSON_Delete(wifi);
        cJSON_Delete(power);
        cJSON_Delete(ui);
        cJSON_Delete(config);
        cJSON_Delete(operation);
        return NULL;
    }
    const bool ok =
        cJSON_AddNumberToObject(result, "sequence", status->sequence) != NULL &&
        add_string(result, "state", state_name(status->state)) &&
        add_string(result, "error", error_name(status->error)) &&
        cJSON_AddNumberToObject(result, "uptimeMs", (double)status->uptime_ms) != NULL &&
        cJSON_AddNumberToObject(result, "freeHeapBytes", (double)status->free_heap_bytes) != NULL &&
        cJSON_AddItemToObject(result, "wifi", wifi) &&
        cJSON_AddBoolToObject(wifi, "connected", status->wifi.connected) != NULL &&
        (status->wifi.rssi_available
             ? cJSON_AddNumberToObject(wifi, "rssiDbm", status->wifi.rssi_dbm) != NULL
             : cJSON_AddNullToObject(wifi, "rssiDbm") != NULL) &&
        cJSON_AddItemToObject(result, "power", power) &&
        cJSON_AddBoolToObject(power, "batteryPresent", status->power.battery_present) != NULL &&
        (status->power.percent_available
             ? cJSON_AddNumberToObject(power, "percent", status->power.percent) != NULL
             : cJSON_AddNullToObject(power, "percent") != NULL) &&
        add_string(power, "chargingState", charging_name(status->power.charging_state)) &&
        cJSON_AddItemToObject(result, "ui", ui) &&
        add_string(ui, "status", status->ui.status) &&
        add_nullable_string(ui, "detail", status->ui.detail, status->ui.detail[0] != '\0') &&
        add_nullable_string(ui, "hint", status->ui.hint, status->ui.hint[0] != '\0') &&
        add_string(ui, "expression", status->ui.expression) &&
        cJSON_AddItemToObject(result, "config", config) &&
        cJSON_AddBoolToObject(config, "available", status->config.available) != NULL &&
        cJSON_AddNumberToObject(config, "revision", status->config.revision) != NULL &&
        add_nullable_string(config, "chatProvider", status->config.chat_provider, status->config.available) &&
        add_nullable_string(config, "chatModel", status->config.chat_model, status->config.available) &&
        cJSON_AddBoolToObject(config, "hasChatApiKey", status->config.has_chat_api_key) != NULL &&
        cJSON_AddBoolToObject(config, "hasAsrApiKey", status->config.has_asr_api_key) != NULL &&
        cJSON_AddItemToObject(result, "latestOperation", operation) &&
        cJSON_AddNumberToObject(operation, "id", status->latest_operation.id) != NULL &&
        add_string(operation, "state", operation_state_name(status->latest_operation.state)) &&
        add_string(operation, "result", result_name(status->latest_operation.result));
    if (!ok) {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}

static cJSON *serialize_public_config(const aiqa_management_public_config_t *config)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();
    cJSON *prefs = cJSON_CreateObject();
    cJSON *profile = cJSON_CreateObject();
    if (result == NULL || wifi == NULL || prefs == NULL || profile == NULL) {
        cJSON_Delete(result);
        cJSON_Delete(wifi);
        cJSON_Delete(prefs);
        cJSON_Delete(profile);
        return NULL;
    }
    const bool ok =
        cJSON_AddNumberToObject(result, "revision", config->revision) != NULL &&
        cJSON_AddItemToObject(result, "wifi", wifi) &&
        add_string(wifi, "ssid", config->wifi.ssid) &&
        cJSON_AddBoolToObject(wifi, "hasPassword", config->wifi.has_password) != NULL &&
        add_string(result, "chatProvider", config->chat_provider) &&
        add_string(result, "chatModel", config->chat_model) &&
        add_string(result, "asrProvider", config->asr_provider) &&
        add_string(result, "asrModel", config->asr_model) &&
        add_string(result, "ttsProvider", config->tts_provider) &&
        add_string(result, "ttsModel", config->tts_model) &&
        add_string(result, "ttsVoice", config->tts_voice) &&
        cJSON_AddBoolToObject(result, "stream", config->stream) != NULL &&
        cJSON_AddBoolToObject(result, "hideReasoning", config->hide_reasoning) != NULL &&
        cJSON_AddNumberToObject(result, "maxCompletionTokens", config->max_completion_tokens) != NULL &&
        cJSON_AddBoolToObject(result, "hasChatApiKey", config->has_chat_api_key) != NULL &&
        cJSON_AddBoolToObject(result, "hasAsrApiKey", config->has_asr_api_key) != NULL &&
        cJSON_AddItemToObject(result, "userPrefs", prefs) &&
        cJSON_AddNumberToObject(prefs, "volumePercent", config->user_prefs.volume_percent) != NULL &&
        cJSON_AddItemToObject(prefs, "assistantProfile", profile) &&
        add_string(profile, "name", config->user_prefs.assistant_profile.name) &&
        add_string(profile, "gender", gender_name(config->user_prefs.assistant_profile.gender));
    if (!ok) {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}

static bool utf8_bounded_string(
    const cJSON *item,
    size_t minimum_length,
    size_t maximum_length)
{
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    const uint8_t *value = (const uint8_t *)item->valuestring;
    const size_t length = strlen(item->valuestring);
    if (length < minimum_length || length > maximum_length) return false;
    for (size_t offset = 0; offset < length;) {
        const uint8_t first = value[offset];
        if (first < 0x80U) {
            if (first < 0x20U || first == 0x7fU) return false;
            offset += 1U;
            continue;
        }
        size_t continuation_count = 0U;
        if (first >= 0xc2U && first <= 0xdfU) {
            continuation_count = 1U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            continuation_count = 2U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuation_count = 3U;
        } else {
            return false;
        }
        if (offset + continuation_count >= length) return false;
        const uint8_t second = value[offset + 1U];
        if ((second & 0xc0U) != 0x80U ||
            (first == 0xe0U && second < 0xa0U) ||
            (first == 0xedU && second >= 0xa0U) ||
            (first == 0xf0U && second < 0x90U) ||
            (first == 0xf4U && second >= 0x90U)) {
            return false;
        }
        for (size_t index = 2U; index <= continuation_count; ++index) {
            if ((value[offset + index] & 0xc0U) != 0x80U) return false;
        }
        offset += continuation_count + 1U;
    }
    return true;
}

static bool parse_wifi_update(
    const cJSON *params,
    aiqa_management_owned_wifi_update_t *out_update,
    cJSON **out_password_item)
{
    static const char *const fields[] = {
        "baseRevision", "ssid", "passwordAction", "password",
    };
    if (!object_has_exact_unique_fields(params, fields, 4U, 3U)) {
        return false;
    }
    const cJSON *revision = cJSON_GetObjectItemCaseSensitive(params, "baseRevision");
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(params, "ssid");
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(params, "passwordAction");
    cJSON *password = cJSON_GetObjectItemCaseSensitive(params, "password");
    if (cJSON_IsString(password) && password->valuestring != NULL) {
        *out_password_item = password;
    }
    if (!cJSON_IsNumber(revision) || revision->valuedouble < 1.0 ||
        revision->valuedouble > (double)UINT32_MAX ||
        (double)(uint32_t)revision->valuedouble != revision->valuedouble ||
        !utf8_bounded_string(ssid, 1U, AIQA_MAX_WIFI_SSID_LEN - 1U) ||
        !cJSON_IsString(action) || action->valuestring == NULL) {
        return false;
    }
    aiqa_wifi_password_action_t parsed_action;
    if (strcmp(action->valuestring, "keep") == 0) {
        parsed_action = AIQA_WIFI_PASSWORD_KEEP;
        if (password != NULL) {
            return false;
        }
    } else if (strcmp(action->valuestring, "clear") == 0) {
        parsed_action = AIQA_WIFI_PASSWORD_CLEAR;
        if (password != NULL) {
            return false;
        }
    } else if (strcmp(action->valuestring, "replace") == 0) {
        parsed_action = AIQA_WIFI_PASSWORD_REPLACE;
        if (!utf8_bounded_string(password, 8U, AIQA_MAX_WIFI_PASSWORD_LEN - 2U)) {
            return false;
        }
    } else {
        return false;
    }
    *out_update = (aiqa_management_owned_wifi_update_t){
        .base_revision = (uint32_t)revision->valuedouble,
        .password_action = parsed_action,
    };
    (void)snprintf(out_update->ssid, sizeof(out_update->ssid), "%s", ssid->valuestring);
    if (password != NULL) {
        (void)snprintf(
            out_update->password, sizeof(out_update->password), "%s", password->valuestring);
    }
    return true;
}

bool aiqa_management_protocol_handle_public_request_with_ports(
    const uint8_t *payload,
    size_t payload_length,
    const aiqa_management_public_protocol_ports_t *ports,
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
        aiqa_management_public_diagnostics_t diagnostics = {0};
        const bool has_diagnostics = ports != NULL &&
            ports->copy_diagnostics != NULL &&
            ports->copy_diagnostics(ports->context, &diagnostics);
        if (has_diagnostics) {
            const uint32_t compatible_asr_phase =
                diagnostics.asr.phase <= 4U ? diagnostics.asr.phase : 2U;
            const int written = snprintf(
                out_response,
                response_capacity,
                "{\"v\":1,\"id\":%u,\"ok\":true,\"result\":{"
                "\"protocol\":\"aiqa-management\",\"version\":1,"
                "\"maxFrameBytes\":4096,"
                "\"authentication\":\"authentication_required\","
                "\"diagnostics\":{\"snapshotVersion\":1,"
                "\"runtimeReady\":%s,\"runtimeStartPhase\":%u,"
                "\"runtimeStartStatus\":%d,\"boardInitPhase\":%u,"
                "\"generation\":%u,\"state\":\"%s\",\"error\":\"%s\","
                "\"stateSequence\":%u,\"asr\":{"
                "\"requestEpoch\":%u,\"phase\":%u,\"status\":%d,"
                "\"httpStatus\":%d,\"transportStatus\":%d,"
                "\"contentLength\":%lld,\"pcmBytes\":%u,"
                "\"postBytes\":%u,\"uploadedBytes\":%u,"
                "\"responseBytes\":%u,\"responseLimit\":%u,"
                "\"headerWaitMs\":%u,\"elapsedMs\":%u,"
                "\"responseComplete\":%s}}}}",
                (unsigned)request_id,
                diagnostics.runtime_ready ? "true" : "false",
                (unsigned)diagnostics.runtime_start_phase,
                (int)diagnostics.runtime_start_status,
                (unsigned)diagnostics.board_init_phase,
                (unsigned)diagnostics.generation,
                state_name(diagnostics.state),
                error_name(diagnostics.error),
                (unsigned)diagnostics.state_sequence,
                (unsigned)diagnostics.asr.request_epoch,
                (unsigned)compatible_asr_phase,
                (int)diagnostics.asr.status,
                (int)diagnostics.asr.http_status,
                (int)diagnostics.asr.transport_status,
                (long long)diagnostics.asr.content_length,
                (unsigned)diagnostics.asr.pcm_bytes,
                (unsigned)diagnostics.asr.post_bytes,
                (unsigned)diagnostics.asr.uploaded_bytes,
                (unsigned)diagnostics.asr.response_bytes,
                (unsigned)diagnostics.asr.response_limit,
                (unsigned)diagnostics.asr.header_wait_ms,
                (unsigned)diagnostics.asr.elapsed_ms,
                diagnostics.asr.response_complete ? "true" : "false");
            produced = written >= 0 && (size_t)written < response_capacity;
            *out_response_length = produced ? (size_t)written : 0U;
            if (!produced) {
                out_response[0] = '\0';
            }
            secure_zero(&diagnostics, sizeof(diagnostics));
        } else {
            produced = write_response(
                out_response,
                response_capacity,
                out_response_length,
                "{\"v\":1,\"id\":%u,\"ok\":true,\"result\":{"
                "\"protocol\":\"aiqa-management\",\"version\":1,"
                "\"maxFrameBytes\":4096,"
                "\"authentication\":\"authentication_required\","
                "\"diagnostics\":null}}",
                request_id);
        }
    } else if (strcmp(method, "system.diagnostics") == 0) {
        aiqa_management_public_diagnostics_t diagnostics = {0};
        const bool has_diagnostics = ports != NULL &&
            ports->copy_diagnostics != NULL &&
            ports->copy_diagnostics(ports->context, &diagnostics);
        if (has_diagnostics) {
            const int written = snprintf(
                out_response,
                response_capacity,
                "{\"v\":1,\"id\":%u,\"ok\":true,\"result\":{"
                "\"snapshotVersion\":2,"
                "\"runtimeReady\":%s,\"runtimeStartPhase\":%u,"
                "\"runtimeStartStatus\":%d,\"boardInitPhase\":%u,"
                "\"generation\":%u,\"state\":\"%s\",\"error\":\"%s\","
                "\"stateSequence\":%u,\"asr\":{"
                "\"requestEpoch\":%u,\"phase\":%u,\"status\":%d,"
                "\"httpStatus\":%d,\"transportStatus\":%d,"
                "\"socketErrno\":%d,\"contentLength\":%lld,\"pcmBytes\":%u,"
                "\"postBytes\":%u,\"uploadedBytes\":%u,"
                "\"uploadWriteCalls\":%u,"
                "\"responseBytes\":%u,\"responseLimit\":%u,"
                "\"headerWaitMs\":%u,\"elapsedMs\":%u,"
                "\"responseComplete\":%s}}}",
                (unsigned)request_id,
                diagnostics.runtime_ready ? "true" : "false",
                (unsigned)diagnostics.runtime_start_phase,
                (int)diagnostics.runtime_start_status,
                (unsigned)diagnostics.board_init_phase,
                (unsigned)diagnostics.generation,
                state_name(diagnostics.state),
                error_name(diagnostics.error),
                (unsigned)diagnostics.state_sequence,
                (unsigned)diagnostics.asr.request_epoch,
                (unsigned)diagnostics.asr.phase,
                (int)diagnostics.asr.status,
                (int)diagnostics.asr.http_status,
                (int)diagnostics.asr.transport_status,
                (int)diagnostics.asr.socket_errno,
                (long long)diagnostics.asr.content_length,
                (unsigned)diagnostics.asr.pcm_bytes,
                (unsigned)diagnostics.asr.post_bytes,
                (unsigned)diagnostics.asr.uploaded_bytes,
                (unsigned)diagnostics.asr.upload_write_calls,
                (unsigned)diagnostics.asr.response_bytes,
                (unsigned)diagnostics.asr.response_limit,
                (unsigned)diagnostics.asr.header_wait_ms,
                (unsigned)diagnostics.asr.elapsed_ms,
                diagnostics.asr.response_complete ? "true" : "false");
            produced = written >= 0 && (size_t)written < response_capacity;
            *out_response_length = produced ? (size_t)written : 0U;
            if (!produced) {
                out_response[0] = '\0';
            }
            secure_zero(&diagnostics, sizeof(diagnostics));
        } else {
            produced = write_response(
                out_response,
                response_capacity,
                out_response_length,
                "{\"v\":1,\"id\":%u,\"ok\":true,\"result\":null}",
                request_id);
        }
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

bool aiqa_management_protocol_handle_public_request(
    const uint8_t *payload,
    size_t payload_length,
    char *out_response,
    size_t response_capacity,
    size_t *out_response_length)
{
    return aiqa_management_protocol_handle_public_request_with_ports(
        payload,
        payload_length,
        NULL,
        out_response,
        response_capacity,
        out_response_length);
}

bool aiqa_management_protocol_handle_authenticated_request(
    const uint8_t *payload,
    size_t payload_length,
    const aiqa_management_security_context_t *access,
    const aiqa_management_protocol_ports_t *ports,
    char *out_response,
    size_t response_capacity,
    size_t *out_response_length)
{
    if (out_response == NULL || out_response_length == NULL ||
        response_capacity == 0U) {
        return false;
    }
    out_response[0] = '\0';
    *out_response_length = 0;
    if (payload == NULL || access == NULL ||
        access->connection_generation == 0U ||
        access->authorization_generation == 0U || ports == NULL ||
        ports->get_status == NULL || ports->get_public_config == NULL ||
        ports->submit_wifi_update == NULL) {
        return false;
    }

    cJSON *root = NULL;
    uint32_t request_id = 0;
    const char *method = NULL;
    const cJSON *params = NULL;
    if (!parse_authenticated_root(
            payload,
            payload_length,
            &root,
            &request_id,
            &method,
            &params)) {
        return write_error(
            out_response,
            response_capacity,
            out_response_length,
            0,
            "INVALID_REQUEST",
            "Request rejected");
    }

    bool produced = false;
    if (strcmp(method, "device.status.get") == 0 && params == NULL) {
        aiqa_management_device_status_t status = {0};
        const aiqa_management_result_t result =
            ports->get_status(ports->context, access, &status);
        if (result == AIQA_MANAGEMENT_OK) {
            cJSON *serialized = serialize_status(&status);
            produced = serialized != NULL &&
                       print_envelope(
                           serialized,
                           request_id,
                           out_response,
                           response_capacity,
                           out_response_length);
        } else {
            produced = write_error(
                out_response,
                response_capacity,
                out_response_length,
                request_id,
                result_name(result),
                "Operation rejected");
        }
        secure_zero(&status, sizeof(status));
    } else if (strcmp(method, "config.public.get") == 0 && params == NULL) {
        aiqa_management_public_config_t config = {0};
        const aiqa_management_result_t result =
            ports->get_public_config(ports->context, access, &config);
        if (result == AIQA_MANAGEMENT_OK) {
            cJSON *serialized = serialize_public_config(&config);
            produced = serialized != NULL &&
                       print_envelope(
                           serialized,
                           request_id,
                           out_response,
                           response_capacity,
                           out_response_length);
        } else {
            produced = write_error(
                out_response,
                response_capacity,
                out_response_length,
                request_id,
                result_name(result),
                "Operation rejected");
        }
        secure_zero(&config, sizeof(config));
    } else if (strcmp(method, "wifi.update") == 0 && params != NULL) {
        aiqa_management_owned_wifi_update_t update = {0};
        cJSON *password_item = NULL;
        if (!parse_wifi_update(params, &update, &password_item)) {
            produced = write_error(
                out_response,
                response_capacity,
                out_response_length,
                request_id,
                "INVALID_REQUEST",
                "Request rejected");
        } else {
            uint32_t operation_id = 0;
            const aiqa_management_result_t result = ports->submit_wifi_update(
                ports->context, access, &update, &operation_id);
            if (result == AIQA_MANAGEMENT_OK && operation_id != 0U) {
                cJSON *accepted = cJSON_CreateObject();
                if (accepted != NULL &&
                    cJSON_AddNumberToObject(
                        accepted, "operationId", operation_id) != NULL &&
                    add_string(accepted, "state", "PENDING")) {
                    produced = print_envelope(
                        accepted,
                        request_id,
                        out_response,
                        response_capacity,
                        out_response_length);
                } else {
                    cJSON_Delete(accepted);
                }
            } else {
                const aiqa_management_result_t stable_result =
                    result == AIQA_MANAGEMENT_OK
                        ? AIQA_MANAGEMENT_INTERNAL_ERROR
                        : result;
                produced = write_error(
                    out_response,
                    response_capacity,
                    out_response_length,
                    request_id,
                    result_name(stable_result),
                    "Operation rejected");
            }
        }
        if (password_item != NULL && password_item->valuestring != NULL) {
            secure_zero(password_item->valuestring, strlen(password_item->valuestring));
        }
        secure_zero(&update, sizeof(update));
    } else if ((strcmp(method, "device.status.get") == 0 ||
                strcmp(method, "config.public.get") == 0) &&
               params != NULL) {
        produced = write_error(
            out_response,
            response_capacity,
            out_response_length,
            request_id,
            "INVALID_REQUEST",
            "Request rejected");
    } else {
        produced = write_error(
            out_response,
            response_capacity,
            out_response_length,
            request_id,
            "METHOD_NOT_FOUND",
            "Method unavailable");
    }

    cJSON_Delete(root);
    return produced;
}
