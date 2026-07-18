#include "aiqa_pairing_rpc.h"

#include "aiqa_management_wire.h"
#include "cJSON.h"
#include "mbedtls/platform_util.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AIQA_PAIRING_CREDENTIAL_ID 1U

struct aiqa_pairing_rpc {
    aiqa_pairing_lifecycle_t *lifecycle;
    aiqa_pairing_random_fn random;
    void *random_context;
    uint8_t device_id[AIQA_PAIRING_DEVICE_ID_MAX];
    size_t device_id_length;
    uint8_t client_nonce[AIQA_PAIRING_NONCE_SIZE];
    uint8_t device_nonce[AIQA_PAIRING_NONCE_SIZE];
    uint64_t connection_id;
    uint64_t handshake_id;
    aiqa_pairing_device_session_t *session;
    bool prepared;
    bool round_two_complete;
    bool finished_pending;
};

static void clear_attempt(aiqa_pairing_rpc_t *rpc)
{
    aiqa_pairing_device_session_destroy(&rpc->session);
    mbedtls_platform_zeroize(rpc->client_nonce, sizeof(rpc->client_nonce));
    mbedtls_platform_zeroize(rpc->device_nonce, sizeof(rpc->device_nonce));
    rpc->connection_id = 0;
    rpc->handshake_id = 0;
    rpc->prepared = false;
    rpc->round_two_complete = false;
    rpc->finished_pending = false;
}

static uint64_t read_u64_be(const uint8_t input[8])
{
    uint64_t value = 0;
    for (size_t index = 0; index < 8U; ++index) {
        value = (value << 8U) | input[index];
    }
    return value;
}

static bool random_nonzero_u64(aiqa_pairing_rpc_t *rpc, uint64_t *out_value)
{
    uint8_t bytes[8] = {0};
    if (rpc->random(rpc->random_context, bytes, sizeof(bytes)) != 0) {
        return false;
    }
    const uint64_t value = read_u64_be(bytes);
    mbedtls_platform_zeroize(bytes, sizeof(bytes));
    if (value == 0U) {
        return false;
    }
    *out_value = value;
    return true;
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool decode_hex(
    const char *input,
    size_t expected_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *out_length)
{
    if (out_length != NULL) *out_length = 0;
    if (input == NULL || output == NULL || out_length == NULL) return false;
    const size_t hex_length = strlen(input);
    if ((hex_length & 1U) != 0U || hex_length == 0U ||
        hex_length / 2U > output_capacity ||
        (expected_length != 0U && hex_length != expected_length * 2U)) {
        return false;
    }
    for (size_t index = 0; index < hex_length / 2U; ++index) {
        const int high = hex_value(input[index * 2U]);
        const int low = hex_value(input[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            mbedtls_platform_zeroize(output, output_capacity);
            return false;
        }
        output[index] = (uint8_t)((high << 4U) | low);
    }
    *out_length = hex_length / 2U;
    return true;
}

static void encode_hex(const uint8_t *input, size_t input_length, char *output)
{
    static const char DIGITS[] = "0123456789abcdef";
    for (size_t index = 0; index < input_length; ++index) {
        output[index * 2U] = DIGITS[input[index] >> 4U];
        output[index * 2U + 1U] = DIGITS[input[index] & 0x0fU];
    }
    output[input_length * 2U] = '\0';
}

static bool object_fields(
    const cJSON *object,
    const char *const *names,
    size_t name_count)
{
    if (!cJSON_IsObject(object)) return false;
    bool seen[4] = {false};
    if (name_count > 4U) return false;
    size_t count = 0;
    const cJSON *field = NULL;
    cJSON_ArrayForEach(field, object)
    {
        size_t index = 0;
        while (index < name_count && field->string != NULL &&
               strcmp(field->string, names[index]) != 0) {
            index += 1U;
        }
        if (field->string == NULL || index == name_count || seen[index]) return false;
        seen[index] = true;
        count += 1U;
    }
    if (count != name_count) return false;
    for (size_t index = 0; index < name_count; ++index) {
        if (!seen[index]) return false;
    }
    return true;
}

static bool json_depth_is_safe(const uint8_t *payload, size_t payload_length)
{
    size_t depth = 0;
    bool string = false;
    bool escaped = false;
    for (size_t index = 0; index < payload_length; ++index) {
        const uint8_t byte = payload[index];
        if (string) {
            if (escaped) {
                if (byte == 'u') {
                    if (index + 4U >= payload_length) return false;
                    uint16_t code_unit = 0;
                    for (size_t digit = 1U; digit <= 4U; ++digit) {
                        const int value = hex_value((char)payload[index + digit]);
                        if (value < 0) return false;
                        code_unit = (uint16_t)((code_unit << 4U) | (uint16_t)value);
                    }
                    if (code_unit == 0U) return false;
                } else if (byte != '"' && byte != '\\' && byte != '/' &&
                           byte != 'b' && byte != 'f' && byte != 'n' &&
                           byte != 'r' && byte != 't') {
                    return false;
                }
                escaped = false;
            }
            else if (byte == '\\') escaped = true;
            else if (byte == '"') string = false;
            else if (byte < 0x20U) return false;
        } else if (byte == '"') string = true;
        else if (byte == '{' || byte == '[') {
            depth += 1U;
            if (depth > 2U) return false;
        } else if (byte == '}' || byte == ']') {
            if (depth == 0U) return false;
            depth -= 1U;
        }
    }
    return !string && !escaped && depth == 0U;
}

static bool parse_root(
    const uint8_t *payload,
    size_t payload_length,
    cJSON **out_root,
    uint32_t *out_id,
    const char **out_method,
    const cJSON **out_params)
{
    if (payload == NULL || payload_length == 0U ||
        payload_length > AIQA_MANAGEMENT_WIRE_MAX_PAYLOAD ||
        memchr(payload, 0, payload_length) != NULL ||
        !json_depth_is_safe(payload, payload_length)) return false;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(
        (const char *)payload, payload_length, &end, false);
    const char *payload_end = (const char *)payload + payload_length;
    while (end != NULL && end < payload_end &&
           (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    static const char *const without_params[] = {"id", "method"};
    static const char *const with_params[] = {"id", "method", "params"};
    const cJSON *params = root == NULL
                              ? NULL
                              : cJSON_GetObjectItemCaseSensitive(root, "params");
    if (root == NULL || end != payload_end ||
        !(params == NULL ? object_fields(root, without_params, 2U)
                         : object_fields(root, with_params, 3U))) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    if (!cJSON_IsNumber(id) || !cJSON_IsString(method) ||
        method->valuestring == NULL || (params != NULL && !cJSON_IsObject(params)) ||
        id->valuedouble < 1.0 || id->valuedouble > (double)UINT32_MAX ||
        (double)(uint32_t)id->valuedouble != id->valuedouble) {
        cJSON_Delete(root);
        return false;
    }
    *out_root = root;
    *out_id = (uint32_t)id->valuedouble;
    *out_method = method->valuestring;
    *out_params = params;
    return true;
}

static bool write_error(
    char *output,
    size_t capacity,
    size_t *out_length,
    uint32_t id,
    const char *code)
{
    const int written = snprintf(
        output,
        capacity,
        "{\"v\":1,\"id\":%u,\"ok\":false,\"error\":{\"code\":\"%s\","
        "\"message\":\"Pairing request rejected\"}}",
        (unsigned)id,
        code);
    if (written < 0 || (size_t)written >= capacity) {
        if (capacity > 0U) output[0] = '\0';
        *out_length = 0;
        return false;
    }
    *out_length = (size_t)written;
    return true;
}

static bool consume_code(
    void *context,
    const uint8_t code[AIQA_PAIRING_CODE_SIZE],
    uint64_t connection_id,
    uint64_t handshake_id)
{
    aiqa_pairing_rpc_t *rpc = context;
    if (!rpc->prepared || rpc->session != NULL ||
        rpc->connection_id != connection_id || rpc->handshake_id != handshake_id) {
        return false;
    }
    return aiqa_pairing_device_session_create(
               &rpc->session,
               code,
               AIQA_PAIRING_CREDENTIAL_ID,
               handshake_id,
               rpc->device_id,
               rpc->device_id_length,
               rpc->client_nonce,
               rpc->device_nonce,
               rpc->random,
               rpc->random_context) == AIQA_PAIRING_OK;
}

static void protocol_failed(aiqa_pairing_rpc_t *rpc)
{
    if (rpc->session != NULL && rpc->connection_id != 0U &&
        rpc->handshake_id != 0U) {
        (void)aiqa_pairing_lifecycle_protocol_failed(
            rpc->lifecycle, rpc->connection_id, rpc->handshake_id);
    }
    clear_attempt(rpc);
}

aiqa_pairing_result_t aiqa_pairing_rpc_create(
    aiqa_pairing_rpc_t **out_rpc,
    aiqa_pairing_lifecycle_t *lifecycle,
    aiqa_pairing_random_fn random,
    void *random_context,
    const uint8_t *device_id,
    size_t device_id_length)
{
    if (out_rpc == NULL || *out_rpc != NULL || lifecycle == NULL || random == NULL ||
        device_id == NULL || device_id_length == 0U ||
        device_id_length > AIQA_PAIRING_DEVICE_ID_MAX) {
        return AIQA_PAIRING_INVALID_ARGUMENT;
    }
    aiqa_pairing_rpc_t *rpc = calloc(1, sizeof(*rpc));
    if (rpc == NULL) return AIQA_PAIRING_CRYPTO_ERROR;
    rpc->lifecycle = lifecycle;
    rpc->random = random;
    rpc->random_context = random_context;
    rpc->device_id_length = device_id_length;
    (void)memcpy(rpc->device_id, device_id, device_id_length);
    *out_rpc = rpc;
    return AIQA_PAIRING_OK;
}

void aiqa_pairing_rpc_destroy(aiqa_pairing_rpc_t **rpc)
{
    if (rpc == NULL || *rpc == NULL) return;
    clear_attempt(*rpc);
    mbedtls_platform_zeroize(*rpc, sizeof(**rpc));
    free(*rpc);
    *rpc = NULL;
}

static bool handle_status(
    aiqa_pairing_rpc_t *rpc,
    uint32_t id,
    const cJSON *params,
    char *output,
    size_t capacity,
    size_t *out_length)
{
    if (params != NULL) return write_error(output, capacity, out_length, id, "INVALID_REQUEST");
    aiqa_pairing_lifecycle_status_t status = {0};
    if (aiqa_pairing_lifecycle_get_status(rpc->lifecycle, &status) !=
        AIQA_PAIRING_LIFECYCLE_OK) {
        return write_error(output, capacity, out_length, id, "PAIRING_UNAVAILABLE");
    }
    static const char *const names[] = {
        "INACTIVE", "AVAILABLE", "HANDSHAKE", "WAIT_FINISHED_SENT",
        "ACTIVE", "LOCKED", "FAULT",
    };
    const unsigned state = (unsigned)status.state;
    const char *name = state < sizeof(names) / sizeof(names[0]) ? names[state] : "FAULT";
    const int written = snprintf(
        output,
        capacity,
        "{\"v\":1,\"id\":%u,\"ok\":true,\"result\":{"
        "\"state\":\"%s\",\"attemptsRemaining\":%u,\"remainingMs\":%llu}}",
        (unsigned)id,
        name,
        (unsigned)status.attempts_remaining,
        (unsigned long long)status.remaining_ms);
    if (written < 0 || (size_t)written >= capacity) return false;
    *out_length = (size_t)written;
    return true;
}

static bool handle_prepare(
    aiqa_pairing_rpc_t *rpc,
    uint64_t connection_id,
    uint32_t id,
    const cJSON *params,
    char *output,
    size_t capacity,
    size_t *out_length)
{
    static const char *const fields[] = {"clientNonce"};
    const cJSON *nonce = params == NULL
                             ? NULL
                             : cJSON_GetObjectItemCaseSensitive(params, "clientNonce");
    size_t nonce_length = 0;
    aiqa_pairing_lifecycle_status_t status = {0};
    if (!object_fields(params, fields, 1U) || !cJSON_IsString(nonce) ||
        nonce->valuestring == NULL || rpc->prepared || connection_id == 0U ||
        aiqa_pairing_lifecycle_get_status(rpc->lifecycle, &status) !=
            AIQA_PAIRING_LIFECYCLE_OK ||
        status.state != AIQA_PAIRING_LIFECYCLE_AVAILABLE ||
        !decode_hex(
            nonce->valuestring,
            AIQA_PAIRING_NONCE_SIZE,
            rpc->client_nonce,
            sizeof(rpc->client_nonce),
            &nonce_length) ||
        rpc->random(
            rpc->random_context,
            rpc->device_nonce,
            sizeof(rpc->device_nonce)) != 0 ||
        !random_nonzero_u64(rpc, &rpc->handshake_id)) {
        clear_attempt(rpc);
        return write_error(output, capacity, out_length, id, "PAIRING_UNAVAILABLE");
    }
    rpc->connection_id = connection_id;
    rpc->prepared = true;
    char device_id_hex[AIQA_PAIRING_DEVICE_ID_MAX * 2U + 1U];
    char device_nonce_hex[AIQA_PAIRING_NONCE_SIZE * 2U + 1U];
    char handshake_hex[17];
    uint8_t handshake_bytes[8];
    for (size_t index = 0; index < 8U; ++index) {
        handshake_bytes[7U - index] =
            (uint8_t)(rpc->handshake_id >> (index * 8U));
    }
    encode_hex(rpc->device_id, rpc->device_id_length, device_id_hex);
    encode_hex(rpc->device_nonce, sizeof(rpc->device_nonce), device_nonce_hex);
    encode_hex(handshake_bytes, sizeof(handshake_bytes), handshake_hex);
    const int written = snprintf(
        output,
        capacity,
        "{\"v\":1,\"id\":%u,\"ok\":true,\"result\":{"
        "\"credentialId\":1,\"handshakeId\":\"%s\","
        "\"deviceId\":\"%s\",\"deviceNonce\":\"%s\"}}",
        (unsigned)id,
        handshake_hex,
        device_id_hex,
        device_nonce_hex);
    mbedtls_platform_zeroize(handshake_bytes, sizeof(handshake_bytes));
    return written >= 0 && (size_t)written < capacity
               ? (*out_length = (size_t)written, true)
               : false;
}

static bool parse_handshake(
    const cJSON *params,
    const char *message_name,
    uint64_t *out_handshake,
    uint8_t *message,
    size_t message_capacity,
    size_t *out_message_length)
{
    const char *const fields[] = {"handshakeId", message_name};
    if (!object_fields(params, fields, 2U)) return false;
    const cJSON *handshake = cJSON_GetObjectItemCaseSensitive(params, "handshakeId");
    const cJSON *payload = cJSON_GetObjectItemCaseSensitive(params, message_name);
    uint8_t handshake_bytes[8] = {0};
    size_t handshake_length = 0;
    const bool valid = cJSON_IsString(handshake) && handshake->valuestring != NULL &&
                       cJSON_IsString(payload) && payload->valuestring != NULL &&
                       decode_hex(
                           handshake->valuestring,
                           8U,
                           handshake_bytes,
                           sizeof(handshake_bytes),
                           &handshake_length) &&
                       decode_hex(
                           payload->valuestring,
                           0U,
                           message,
                           message_capacity,
                           out_message_length);
    if (valid) *out_handshake = read_u64_be(handshake_bytes);
    mbedtls_platform_zeroize(handshake_bytes, sizeof(handshake_bytes));
    return valid && *out_handshake != 0U;
}

static bool write_binary_result(
    char *output,
    size_t capacity,
    size_t *out_length,
    uint32_t id,
    const char *field,
    const uint8_t *value,
    size_t value_length)
{
    char hex[AIQA_PAIRING_ROUND_MAX * 2U + 1U];
    if (value_length > AIQA_PAIRING_ROUND_MAX) return false;
    encode_hex(value, value_length, hex);
    const int written = snprintf(
        output,
        capacity,
        "{\"v\":1,\"id\":%u,\"ok\":true,\"result\":{\"%s\":\"%s\"}}",
        (unsigned)id,
        field,
        hex);
    mbedtls_platform_zeroize(hex, sizeof(hex));
    if (written < 0 || (size_t)written >= capacity) return false;
    *out_length = (size_t)written;
    return true;
}

static bool handle_begin(
    aiqa_pairing_rpc_t *rpc,
    uint64_t connection_id,
    uint32_t id,
    const cJSON *params,
    char *output,
    size_t capacity,
    size_t *out_length)
{
    uint8_t client_round[AIQA_PAIRING_ROUND_MAX] = {0};
    uint8_t device_round[AIQA_PAIRING_ROUND_MAX] = {0};
    size_t client_length = 0;
    size_t device_length = 0;
    uint64_t handshake = 0;
    bool ok = parse_handshake(
                  params,
                  "clientRoundOne",
                  &handshake,
                  client_round,
                  sizeof(client_round),
                  &client_length) &&
              rpc->prepared && rpc->session == NULL &&
              connection_id == rpc->connection_id &&
              handshake == rpc->handshake_id &&
              aiqa_pairing_lifecycle_begin(
                  rpc->lifecycle,
                  connection_id,
                  handshake,
                  consume_code,
                  rpc) == AIQA_PAIRING_LIFECYCLE_OK &&
              aiqa_pairing_device_session_write_round_one(
                  rpc->session,
                  device_round,
                  sizeof(device_round),
                  &device_length) == AIQA_PAIRING_OK &&
              aiqa_pairing_device_session_read_round_one(
                  rpc->session, client_round, client_length) == AIQA_PAIRING_OK &&
              aiqa_pairing_lifecycle_handshake_progress(
                  rpc->lifecycle, connection_id, handshake) ==
                  AIQA_PAIRING_LIFECYCLE_OK;
    if (ok) {
        ok = write_binary_result(
            output,
            capacity,
            out_length,
            id,
            "deviceRoundOne",
            device_round,
            device_length);
    }
    mbedtls_platform_zeroize(client_round, sizeof(client_round));
    mbedtls_platform_zeroize(device_round, sizeof(device_round));
    if (!ok) {
        protocol_failed(rpc);
        return write_error(output, capacity, out_length, id, "PAIRING_FAILED");
    }
    return true;
}

static bool handle_round_two(
    aiqa_pairing_rpc_t *rpc,
    uint64_t connection_id,
    uint32_t id,
    const cJSON *params,
    char *output,
    size_t capacity,
    size_t *out_length)
{
    uint8_t client_round[AIQA_PAIRING_ROUND_MAX] = {0};
    uint8_t device_round[AIQA_PAIRING_ROUND_MAX] = {0};
    size_t client_length = 0;
    size_t device_length = 0;
    uint64_t handshake = 0;
    bool ok = parse_handshake(
                  params,
                  "clientRoundTwo",
                  &handshake,
                  client_round,
                  sizeof(client_round),
                  &client_length) &&
              rpc->session != NULL && !rpc->round_two_complete &&
              connection_id == rpc->connection_id &&
              handshake == rpc->handshake_id &&
              aiqa_pairing_device_session_complete_round_two(
                  rpc->session,
                  client_round,
                  client_length,
                  device_round,
                  sizeof(device_round),
                  &device_length) == AIQA_PAIRING_OK &&
              aiqa_pairing_lifecycle_handshake_progress(
                  rpc->lifecycle, connection_id, handshake) ==
                  AIQA_PAIRING_LIFECYCLE_OK;
    if (ok) {
        rpc->round_two_complete = true;
        ok = write_binary_result(
            output,
            capacity,
            out_length,
            id,
            "deviceRoundTwo",
            device_round,
            device_length);
    }
    mbedtls_platform_zeroize(client_round, sizeof(client_round));
    mbedtls_platform_zeroize(device_round, sizeof(device_round));
    if (!ok) {
        protocol_failed(rpc);
        return write_error(output, capacity, out_length, id, "PAIRING_FAILED");
    }
    return true;
}

static bool handle_finish(
    aiqa_pairing_rpc_t *rpc,
    uint64_t connection_id,
    uint32_t id,
    const cJSON *params,
    char *output,
    size_t capacity,
    size_t *out_length,
    bool *out_requires_tx_confirmation)
{
    uint8_t client_finished[AIQA_PAIRING_FINISHED_SIZE] = {0};
    uint8_t device_finished[AIQA_PAIRING_FINISHED_SIZE] = {0};
    size_t client_length = 0;
    uint64_t handshake = 0;
    bool ok = parse_handshake(
                  params,
                  "clientFinished",
                  &handshake,
                  client_finished,
                  sizeof(client_finished),
                  &client_length) &&
              client_length == AIQA_PAIRING_FINISHED_SIZE &&
              rpc->session != NULL && rpc->round_two_complete &&
              !rpc->finished_pending && connection_id == rpc->connection_id &&
              handshake == rpc->handshake_id &&
              aiqa_pairing_device_session_confirm(
                  rpc->session, client_finished, device_finished) ==
                  AIQA_PAIRING_OK &&
              aiqa_pairing_lifecycle_client_finished_verified(
                  rpc->lifecycle, connection_id, handshake) ==
                  AIQA_PAIRING_LIFECYCLE_OK;
    if (ok) {
        rpc->finished_pending = true;
        ok = write_binary_result(
            output,
            capacity,
            out_length,
            id,
            "deviceFinished",
            device_finished,
            sizeof(device_finished));
    }
    mbedtls_platform_zeroize(client_finished, sizeof(client_finished));
    mbedtls_platform_zeroize(device_finished, sizeof(device_finished));
    if (!ok) {
        protocol_failed(rpc);
        return write_error(output, capacity, out_length, id, "PAIRING_FAILED");
    }
    *out_requires_tx_confirmation = true;
    return true;
}

bool aiqa_pairing_rpc_handle(
    aiqa_pairing_rpc_t *rpc,
    uint64_t connection_id,
    const uint8_t *payload,
    size_t payload_length,
    char *out_response,
    size_t response_capacity,
    size_t *out_response_length,
    bool *out_requires_tx_confirmation)
{
    if (out_response_length != NULL) *out_response_length = 0;
    if (out_requires_tx_confirmation != NULL) *out_requires_tx_confirmation = false;
    if (rpc == NULL || payload == NULL || out_response == NULL ||
        response_capacity == 0U || out_response_length == NULL ||
        out_requires_tx_confirmation == NULL) return false;
    cJSON *root = NULL;
    uint32_t id = 0;
    const char *method = NULL;
    const cJSON *params = NULL;
    if (!parse_root(payload, payload_length, &root, &id, &method, &params)) {
        return false;
    }
    bool handled = true;
    if (strcmp(method, "pairing.status") == 0) {
        handled = handle_status(
            rpc, id, params, out_response, response_capacity, out_response_length);
    } else if (strcmp(method, "pairing.prepare") == 0) {
        handled = handle_prepare(
            rpc,
            connection_id,
            id,
            params,
            out_response,
            response_capacity,
            out_response_length);
    } else if (strcmp(method, "pairing.begin") == 0) {
        handled = handle_begin(
            rpc,
            connection_id,
            id,
            params,
            out_response,
            response_capacity,
            out_response_length);
    } else if (strcmp(method, "pairing.round2") == 0) {
        handled = handle_round_two(
            rpc,
            connection_id,
            id,
            params,
            out_response,
            response_capacity,
            out_response_length);
    } else if (strcmp(method, "pairing.finish") == 0) {
        handled = handle_finish(
            rpc,
            connection_id,
            id,
            params,
            out_response,
            response_capacity,
            out_response_length,
            out_requires_tx_confirmation);
    } else {
        handled = false;
    }
    cJSON_Delete(root);
    return handled;
}

aiqa_pairing_result_t aiqa_pairing_rpc_commit_finished(
    aiqa_pairing_rpc_t *rpc,
    uint64_t connection_id,
    aiqa_secure_channel_t **out_channel,
    aiqa_management_security_context_t *out_access)
{
    if (out_access != NULL) mbedtls_platform_zeroize(out_access, sizeof(*out_access));
    if (rpc == NULL || out_channel == NULL || *out_channel != NULL ||
        out_access == NULL || !rpc->finished_pending || rpc->session == NULL ||
        connection_id == 0U || connection_id != rpc->connection_id) {
        return AIQA_PAIRING_INVALID_ARGUMENT;
    }
    aiqa_management_security_context_t access = {
        .connection_generation = connection_id,
    };
    if (!random_nonzero_u64(rpc, &access.authorization_generation)) {
        protocol_failed(rpc);
        return AIQA_PAIRING_CRYPTO_ERROR;
    }
    aiqa_secure_channel_t *channel = NULL;
    aiqa_pairing_result_t result = aiqa_pairing_device_session_take_channel(
        rpc->session, &channel, access.session_id);
    if (result != AIQA_PAIRING_OK ||
        !aiqa_management_access_global_prepare(&access) ||
        aiqa_pairing_lifecycle_device_finished_sent(
            rpc->lifecycle, connection_id, rpc->handshake_id) !=
            AIQA_PAIRING_LIFECYCLE_OK ||
        !aiqa_management_access_global_activate(
            access.connection_generation, access.authorization_generation)) {
        aiqa_management_access_global_revoke();
        aiqa_secure_channel_destroy(&channel);
        protocol_failed(rpc);
        mbedtls_platform_zeroize(&access, sizeof(access));
        return AIQA_PAIRING_CRYPTO_ERROR;
    }
    *out_channel = channel;
    *out_access = access;
    clear_attempt(rpc);
    return AIQA_PAIRING_OK;
}

void aiqa_pairing_rpc_abort_connection(
    aiqa_pairing_rpc_t *rpc,
    uint64_t connection_id)
{
    if (rpc == NULL || connection_id == 0U) return;
    aiqa_management_access_global_revoke();
    if (rpc->connection_id == connection_id) clear_attempt(rpc);
    (void)aiqa_pairing_lifecycle_disconnect(rpc->lifecycle, connection_id);
}
