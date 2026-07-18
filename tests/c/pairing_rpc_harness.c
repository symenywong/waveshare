#include "aiqa_management_access.h"
#include "aiqa_pairing_client_session.h"
#include "aiqa_pairing_lifecycle.h"
#include "aiqa_pairing_rpc.h"
#include "aiqa_secure_channel.h"
#include "cJSON.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CONNECTION_ID UINT64_C(41)

typedef struct {
    uint64_t now_ms;
    uint64_t random_state;
    aiqa_pairing_lock_record_t record;
    bool has_record;
    unsigned start_presence;
    uint8_t shown_code[AIQA_PAIRING_CODE_SIZE];
    unsigned revoke_calls;
} fixture_t;

static int random_fill(void *context, unsigned char *output, size_t length)
{
    fixture_t *fixture = context;
    for (size_t index = 0; index < length; ++index) {
        fixture->random_state ^= fixture->random_state << 13U;
        fixture->random_state ^= fixture->random_state >> 7U;
        fixture->random_state ^= fixture->random_state << 17U;
        output[index] = (uint8_t)fixture->random_state;
    }
    return 0;
}

static bool monotonic_ms(void *context, uint64_t *out_now_ms)
{
    *out_now_ms = ((fixture_t *)context)->now_ms;
    return true;
}

static aiqa_pairing_persistence_result_t load_record(
    void *context,
    aiqa_pairing_lock_record_t *out_record)
{
    fixture_t *fixture = context;
    if (!fixture->has_record) return AIQA_PAIRING_PERSISTENCE_NOT_FOUND;
    *out_record = fixture->record;
    return AIQA_PAIRING_PERSISTENCE_OK;
}

static bool store_record(
    void *context,
    const aiqa_pairing_lock_record_t *record)
{
    fixture_t *fixture = context;
    fixture->record = *record;
    fixture->has_record = true;
    return true;
}

static bool show_code(
    void *context,
    const uint8_t code[AIQA_PAIRING_CODE_SIZE])
{
    (void)memcpy(
        ((fixture_t *)context)->shown_code, code, AIQA_PAIRING_CODE_SIZE);
    return true;
}

static bool clear_code(void *context)
{
    (void)memset(
        ((fixture_t *)context)->shown_code, 0, AIQA_PAIRING_CODE_SIZE);
    return true;
}

static bool consume_presence(
    void *context,
    aiqa_pairing_local_action_t action)
{
    fixture_t *fixture = context;
    if (action != AIQA_PAIRING_LOCAL_START || fixture->start_presence == 0U) {
        return false;
    }
    fixture->start_presence -= 1U;
    return true;
}

static void revoke_connection(void *context, uint64_t connection_id)
{
    assert(connection_id == CONNECTION_ID);
    ((fixture_t *)context)->revoke_calls += 1U;
}

static aiqa_pairing_lifecycle_t *create_lifecycle(fixture_t *fixture)
{
    const aiqa_pairing_lifecycle_ports_t ports = {
        .context = fixture,
        .monotonic_ms = monotonic_ms,
        .random_fill = random_fill,
        .load_lock_record = load_record,
        .store_lock_record = store_record,
        .show_code = show_code,
        .clear_code = clear_code,
        .consume_local_presence = consume_presence,
        .revoke_connection = revoke_connection,
    };
    const aiqa_pairing_lifecycle_policy_t policy =
        aiqa_pairing_lifecycle_default_policy();
    aiqa_pairing_lifecycle_t *lifecycle = NULL;
    assert(aiqa_pairing_lifecycle_create(&lifecycle, &policy, &ports) ==
           AIQA_PAIRING_LIFECYCLE_OK);
    return lifecycle;
}

static void hex_encode(const uint8_t *input, size_t length, char *output)
{
    static const char DIGITS[] = "0123456789abcdef";
    for (size_t index = 0; index < length; ++index) {
        output[index * 2U] = DIGITS[input[index] >> 4U];
        output[index * 2U + 1U] = DIGITS[input[index] & 0x0fU];
    }
    output[length * 2U] = '\0';
}

static int hex_digit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static size_t hex_decode(const char *input, uint8_t *output, size_t capacity)
{
    const size_t length = strlen(input);
    assert((length & 1U) == 0U && length / 2U <= capacity);
    for (size_t index = 0; index < length / 2U; ++index) {
        const int high = hex_digit(input[index * 2U]);
        const int low = hex_digit(input[index * 2U + 1U]);
        assert(high >= 0 && low >= 0);
        output[index] = (uint8_t)((high << 4U) | low);
    }
    return length / 2U;
}

static cJSON *call_rpc(
    aiqa_pairing_rpc_t *rpc,
    const char *request,
    bool *out_requires_confirmation)
{
    char response[2048];
    size_t response_length = 0;
    bool requires_confirmation = false;
    assert(aiqa_pairing_rpc_handle(
        rpc,
        CONNECTION_ID,
        (const uint8_t *)request,
        strlen(request),
        response,
        sizeof(response),
        &response_length,
        &requires_confirmation));
    assert(response_length > 0U && response_length < sizeof(response));
    cJSON *root = cJSON_ParseWithLength(response, response_length);
    assert(cJSON_IsObject(root));
    if (out_requires_confirmation != NULL) {
        *out_requires_confirmation = requires_confirmation;
    }
    return root;
}

static const cJSON *successful_result(cJSON *root, uint32_t expected_id)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    assert(cJSON_IsNumber(id) && id->valuedouble == (double)expected_id);
    assert(cJSON_IsTrue(ok) && cJSON_IsObject(result));
    return result;
}

static void copy_hex_field(
    const cJSON *result,
    const char *name,
    char *output,
    size_t capacity)
{
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(result, name);
    assert(cJSON_IsString(field) && field->valuestring != NULL);
    assert(strlen(field->valuestring) < capacity);
    (void)snprintf(output, capacity, "%s", field->valuestring);
}

static void run_roundtrip(void)
{
    fixture_t fixture = {
        .random_state = UINT64_C(0x0123456789abcdef),
        .start_presence = 1U,
    };
    aiqa_management_access_global_init();
    aiqa_pairing_lifecycle_t *lifecycle = create_lifecycle(&fixture);
    assert(aiqa_pairing_lifecycle_local_open(lifecycle, CONNECTION_ID) ==
           AIQA_PAIRING_LIFECYCLE_OK);

    uint8_t device_id[16];
    uint8_t client_nonce[AIQA_PAIRING_NONCE_SIZE];
    for (size_t index = 0; index < sizeof(device_id); ++index) {
        device_id[index] = (uint8_t)(0x20U + index);
    }
    for (size_t index = 0; index < sizeof(client_nonce); ++index) {
        client_nonce[index] = (uint8_t)(index + 1U);
    }
    aiqa_pairing_rpc_t *rpc = NULL;
    assert(aiqa_pairing_rpc_create(
               &rpc,
               lifecycle,
               random_fill,
               &fixture,
               device_id,
               sizeof(device_id)) == AIQA_PAIRING_OK);

    char client_nonce_hex[AIQA_PAIRING_NONCE_SIZE * 2U + 1U];
    char request[1400];
    hex_encode(client_nonce, sizeof(client_nonce), client_nonce_hex);
    (void)snprintf(
        request,
        sizeof(request),
        "{\"id\":1,\"method\":\"pairing.prepare\",\"params\":{"
        "\"clientNonce\":\"%s\"}}",
        client_nonce_hex);
    cJSON *root = call_rpc(rpc, request, NULL);
    const cJSON *result = successful_result(root, 1U);
    char handshake_hex[17];
    char device_id_hex[AIQA_PAIRING_DEVICE_ID_MAX * 2U + 1U];
    char device_nonce_hex[AIQA_PAIRING_NONCE_SIZE * 2U + 1U];
    copy_hex_field(result, "handshakeId", handshake_hex, sizeof(handshake_hex));
    copy_hex_field(result, "deviceId", device_id_hex, sizeof(device_id_hex));
    copy_hex_field(result, "deviceNonce", device_nonce_hex, sizeof(device_nonce_hex));
    cJSON_Delete(root);

    uint8_t handshake_bytes[8];
    uint8_t returned_device_id[AIQA_PAIRING_DEVICE_ID_MAX];
    uint8_t device_nonce[AIQA_PAIRING_NONCE_SIZE];
    assert(hex_decode(handshake_hex, handshake_bytes, sizeof(handshake_bytes)) == 8U);
    assert(hex_decode(device_id_hex, returned_device_id, sizeof(returned_device_id)) ==
           sizeof(device_id));
    assert(memcmp(returned_device_id, device_id, sizeof(device_id)) == 0);
    assert(hex_decode(device_nonce_hex, device_nonce, sizeof(device_nonce)) ==
           sizeof(device_nonce));
    uint64_t handshake_id = 0;
    for (size_t index = 0; index < sizeof(handshake_bytes); ++index) {
        handshake_id = (handshake_id << 8U) | handshake_bytes[index];
    }

    fixture_t client_rng = {.random_state = UINT64_C(0xfedcba9876543210)};
    aiqa_pairing_client_session_t *client = NULL;
    assert(aiqa_pairing_client_session_create(
               &client,
               fixture.shown_code,
               sizeof(fixture.shown_code),
               1U,
               handshake_id,
               device_id,
               sizeof(device_id),
               client_nonce,
               device_nonce,
               random_fill,
               &client_rng) == AIQA_PAIRING_OK);

    uint8_t binary[AIQA_PAIRING_ROUND_MAX];
    size_t binary_length = 0;
    char binary_hex[AIQA_PAIRING_ROUND_MAX * 2U + 1U];
    assert(aiqa_pairing_client_session_write_round_one(
               client, binary, sizeof(binary), &binary_length) == AIQA_PAIRING_OK);
    hex_encode(binary, binary_length, binary_hex);
    (void)snprintf(
        request,
        sizeof(request),
        "{\"id\":2,\"method\":\"pairing.begin\",\"params\":{"
        "\"handshakeId\":\"%s\",\"clientRoundOne\":\"%s\"}}",
        handshake_hex,
        binary_hex);
    root = call_rpc(rpc, request, NULL);
    result = successful_result(root, 2U);
    copy_hex_field(result, "deviceRoundOne", binary_hex, sizeof(binary_hex));
    cJSON_Delete(root);
    binary_length = hex_decode(binary_hex, binary, sizeof(binary));
    assert(aiqa_pairing_client_session_read_round_one(client, binary, binary_length) ==
           AIQA_PAIRING_OK);

    assert(aiqa_pairing_client_session_write_round_two(
               client, binary, sizeof(binary), &binary_length) == AIQA_PAIRING_OK);
    hex_encode(binary, binary_length, binary_hex);
    (void)snprintf(
        request,
        sizeof(request),
        "{\"id\":3,\"method\":\"pairing.round2\",\"params\":{"
        "\"handshakeId\":\"%s\",\"clientRoundTwo\":\"%s\"}}",
        handshake_hex,
        binary_hex);
    root = call_rpc(rpc, request, NULL);
    result = successful_result(root, 3U);
    copy_hex_field(result, "deviceRoundTwo", binary_hex, sizeof(binary_hex));
    cJSON_Delete(root);
    binary_length = hex_decode(binary_hex, binary, sizeof(binary));
    assert(aiqa_pairing_client_session_read_round_two(client, binary, binary_length) ==
           AIQA_PAIRING_OK);

    assert(aiqa_pairing_client_session_create_finished(client, binary) ==
           AIQA_PAIRING_OK);
    hex_encode(binary, AIQA_PAIRING_FINISHED_SIZE, binary_hex);
    (void)snprintf(
        request,
        sizeof(request),
        "{\"id\":4,\"method\":\"pairing.finish\",\"params\":{"
        "\"handshakeId\":\"%s\",\"clientFinished\":\"%s\"}}",
        handshake_hex,
        binary_hex);
    bool requires_confirmation = false;
    root = call_rpc(rpc, request, &requires_confirmation);
    assert(requires_confirmation);
    result = successful_result(root, 4U);
    copy_hex_field(result, "deviceFinished", binary_hex, sizeof(binary_hex));
    cJSON_Delete(root);
    binary_length = hex_decode(binary_hex, binary, sizeof(binary));
    assert(aiqa_pairing_client_session_verify_finished(client, binary, binary_length) ==
           AIQA_PAIRING_OK);

    aiqa_secure_channel_t *device_channel = NULL;
    aiqa_management_security_context_t access = {0};
    assert(aiqa_pairing_rpc_commit_finished(
               rpc, CONNECTION_ID, &device_channel, &access) == AIQA_PAIRING_OK);
    assert(aiqa_management_access_global_authorize(&access));

    const uint8_t plaintext_request[] = "{\"id\":5,\"method\":\"device.status.get\"}";
    uint8_t record[AIQA_SECURE_RECORD_MAX];
    uint8_t plaintext[256];
    size_t record_length = 0;
    size_t plaintext_length = 0;
    assert(aiqa_pairing_client_session_encrypt_request(
               client,
               plaintext_request,
               sizeof(plaintext_request) - 1U,
               record,
               sizeof(record),
               &record_length) == AIQA_PAIRING_OK);
    assert(aiqa_secure_channel_decrypt(
               device_channel,
               AIQA_SECURE_FRAME_REQUEST,
               record,
               record_length,
               plaintext,
               sizeof(plaintext),
               &plaintext_length) == AIQA_PAIRING_OK);
    assert(plaintext_length == sizeof(plaintext_request) - 1U);
    assert(memcmp(plaintext, plaintext_request, plaintext_length) == 0);

    const uint8_t plaintext_response[] = "{\"v\":1,\"id\":5,\"ok\":true}";
    assert(aiqa_secure_channel_encrypt(
               device_channel,
               AIQA_SECURE_FRAME_RESPONSE,
               plaintext_response,
               sizeof(plaintext_response) - 1U,
               record,
               sizeof(record),
               &record_length) == AIQA_PAIRING_OK);
    assert(aiqa_pairing_client_session_decrypt_response(
               client,
               AIQA_SECURE_FRAME_RESPONSE,
               record,
               record_length,
               plaintext,
               sizeof(plaintext),
               &plaintext_length) == AIQA_PAIRING_OK);
    assert(plaintext_length == sizeof(plaintext_response) - 1U);
    assert(memcmp(plaintext, plaintext_response, plaintext_length) == 0);

    aiqa_pairing_rpc_abort_connection(rpc, CONNECTION_ID);
    assert(!aiqa_management_access_global_authorize(&access));
    aiqa_secure_channel_destroy(&device_channel);
    aiqa_pairing_client_session_destroy(&client);
    aiqa_pairing_rpc_destroy(&rpc);
    aiqa_pairing_lifecycle_destroy(&lifecycle);
}

static void run_reject(void)
{
    fixture_t fixture = {
        .random_state = UINT64_C(0x1111222233334444),
        .start_presence = 1U,
    };
    aiqa_management_access_global_init();
    aiqa_pairing_lifecycle_t *lifecycle = create_lifecycle(&fixture);
    assert(aiqa_pairing_lifecycle_local_open(lifecycle, CONNECTION_ID) ==
           AIQA_PAIRING_LIFECYCLE_OK);
    const uint8_t device_id[] = {1, 2, 3, 4};
    aiqa_pairing_rpc_t *rpc = NULL;
    assert(aiqa_pairing_rpc_create(
               &rpc,
               lifecycle,
               random_fill,
               &fixture,
               device_id,
               sizeof(device_id)) == AIQA_PAIRING_OK);
    char response[256];
    size_t response_length = 123U;
    bool confirmation = true;
    const char embedded_nul[] = "{\"id\":1,\"method\":\"pairing.status\"}\0{}";
    assert(!aiqa_pairing_rpc_handle(
        rpc,
        CONNECTION_ID,
        (const uint8_t *)embedded_nul,
        sizeof(embedded_nul) - 1U,
        response,
        sizeof(response),
        &response_length,
        &confirmation));
    assert(response_length == 0U && !confirmation);
    const char bad_escape[] = "{\"id\":1,\"method\":\"pairing\\xstatus\"}";
    assert(!aiqa_pairing_rpc_handle(
        rpc,
        CONNECTION_ID,
        (const uint8_t *)bad_escape,
        sizeof(bad_escape) - 1U,
        response,
        sizeof(response),
        &response_length,
        &confirmation));
    aiqa_pairing_rpc_destroy(&rpc);
    aiqa_pairing_lifecycle_destroy(&lifecycle);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "roundtrip") == 0) {
        run_roundtrip();
    } else if (strcmp(argv[1], "reject") == 0) {
        run_reject();
    } else {
        return 2;
    }
    return 0;
}
