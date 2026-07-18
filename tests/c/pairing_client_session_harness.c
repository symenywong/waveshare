#include "aiqa_pairing_client_session.h"
#include "aiqa_pairing_crypto.h"
#include "aiqa_secure_channel.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t state;
} rng_t;

static int random_fill(void *context, unsigned char *output, size_t length)
{
    rng_t *rng = context;
    for (size_t index = 0; index < length; ++index) {
        rng->state ^= rng->state << 13U;
        rng->state ^= rng->state >> 7U;
        rng->state ^= rng->state << 17U;
        output[index] = (uint8_t)rng->state;
    }
    return 0;
}

static void make_metadata(
    uint8_t device_id[AIQA_PAIRING_DEVICE_ID_MAX],
    uint8_t client_nonce[AIQA_PAIRING_NONCE_SIZE],
    uint8_t device_nonce[AIQA_PAIRING_NONCE_SIZE])
{
    for (size_t index = 0; index < AIQA_PAIRING_DEVICE_ID_MAX; ++index) {
        device_id[index] = (uint8_t)(0x40U + index);
    }
    for (size_t index = 0; index < AIQA_PAIRING_NONCE_SIZE; ++index) {
        client_nonce[index] = (uint8_t)(index + 1U);
        device_nonce[index] = (uint8_t)(0x80U + index);
    }
}

static void make_connected_pair(
    aiqa_pairing_client_session_t **out_client,
    aiqa_secure_channel_t **out_device_channel)
{
    const uint8_t code[AIQA_PAIRING_CODE_SIZE] = {'3', '1', '4', '1', '5', '9', '2', '6'};
    uint8_t device_id[AIQA_PAIRING_DEVICE_ID_MAX];
    uint8_t client_nonce[AIQA_PAIRING_NONCE_SIZE];
    uint8_t device_nonce[AIQA_PAIRING_NONCE_SIZE];
    make_metadata(device_id, client_nonce, device_nonce);
    rng_t client_rng = {.state = UINT64_C(0x123456789abcdef0)};
    rng_t device_rng = {.state = UINT64_C(0x0fedcba987654321)};

    assert(aiqa_pairing_client_session_create(
               out_client, code, sizeof(code), 7,
               UINT64_C(0x0102030405060708), device_id, sizeof(device_id),
               client_nonce, device_nonce, random_fill, &client_rng) ==
           AIQA_PAIRING_OK);
    aiqa_pairing_context_t *device = NULL;
    assert(aiqa_pairing_create(
               &device, AIQA_PAIRING_DEVICE, code, sizeof(code), 7,
               UINT64_C(0x0102030405060708), device_id, sizeof(device_id),
               client_nonce, device_nonce, random_fill, &device_rng) ==
           AIQA_PAIRING_OK);

    uint8_t client_r1[AIQA_PAIRING_ROUND_MAX];
    uint8_t device_r1[AIQA_PAIRING_ROUND_MAX];
    uint8_t client_r2[AIQA_PAIRING_ROUND_MAX];
    uint8_t device_r2[AIQA_PAIRING_ROUND_MAX];
    size_t client_r1_length = 0;
    size_t device_r1_length = 0;
    size_t client_r2_length = 0;
    size_t device_r2_length = 0;
    assert(aiqa_pairing_client_session_write_round_one(
               *out_client, client_r1, sizeof(client_r1), &client_r1_length) ==
           AIQA_PAIRING_OK);
    assert(aiqa_pairing_read_round_one(device, client_r1, client_r1_length) ==
           AIQA_PAIRING_OK);
    assert(aiqa_pairing_write_round_one(
               device, device_r1, sizeof(device_r1), &device_r1_length) ==
           AIQA_PAIRING_OK);
    assert(aiqa_pairing_client_session_read_round_one(
               *out_client, device_r1, device_r1_length) == AIQA_PAIRING_OK);
    assert(aiqa_pairing_client_session_write_round_two(
               *out_client, client_r2, sizeof(client_r2), &client_r2_length) ==
           AIQA_PAIRING_OK);
    assert(aiqa_pairing_read_round_two(device, client_r2, client_r2_length) ==
           AIQA_PAIRING_OK);
    assert(aiqa_pairing_write_round_two(
               device, device_r2, sizeof(device_r2), &device_r2_length) ==
           AIQA_PAIRING_OK);
    assert(aiqa_pairing_client_session_read_round_two(
               *out_client, device_r2, device_r2_length) == AIQA_PAIRING_OK);

    aiqa_pairing_keys_t *device_keys = NULL;
    assert(aiqa_pairing_derive_keys(device, &device_keys) == AIQA_PAIRING_OK);
    uint8_t client_finished[AIQA_PAIRING_FINISHED_SIZE];
    uint8_t device_finished[AIQA_PAIRING_FINISHED_SIZE];
    assert(aiqa_pairing_client_session_create_finished(
               *out_client, client_finished) == AIQA_PAIRING_OK);
    assert(aiqa_pairing_verify_finished(
               device_keys, AIQA_PAIRING_CLIENT, client_finished,
               sizeof(client_finished)) == AIQA_PAIRING_OK);
    assert(aiqa_pairing_create_finished(
               device_keys, AIQA_PAIRING_DEVICE, device_finished) ==
           AIQA_PAIRING_OK);
    assert(aiqa_pairing_client_session_verify_finished(
               *out_client, device_finished, sizeof(device_finished)) ==
           AIQA_PAIRING_OK);
    assert(aiqa_secure_channel_create(out_device_channel, &device_keys) ==
           AIQA_PAIRING_OK);
    aiqa_pairing_destroy(&device);
}

static void run_roundtrip(void)
{
    aiqa_pairing_client_session_t *client = NULL;
    aiqa_secure_channel_t *device = NULL;
    make_connected_pair(&client, &device);
    const uint8_t request[] = "{\"id\":1}";
    uint8_t record[AIQA_SECURE_RECORD_MAX];
    uint8_t plaintext[64];
    size_t record_length = 0;
    size_t plaintext_length = 0;
    assert(aiqa_pairing_client_session_encrypt_request(
               client, request, sizeof(request) - 1U, record, sizeof(record),
               &record_length) == AIQA_PAIRING_OK);
    assert(aiqa_secure_channel_decrypt(
               device, AIQA_SECURE_FRAME_REQUEST, record, record_length,
               plaintext, sizeof(plaintext), &plaintext_length) ==
           AIQA_PAIRING_OK);
    assert(plaintext_length == sizeof(request) - 1U);
    assert(memcmp(plaintext, request, plaintext_length) == 0);

    const uint8_t response[] = "{\"ok\":true}";
    assert(aiqa_secure_channel_encrypt(
               device, AIQA_SECURE_FRAME_RESPONSE, response,
               sizeof(response) - 1U, record, sizeof(record), &record_length) ==
           AIQA_PAIRING_OK);
    assert(aiqa_pairing_client_session_decrypt_response(
               client, AIQA_SECURE_FRAME_RESPONSE, record, record_length,
               plaintext, sizeof(plaintext), &plaintext_length) ==
           AIQA_PAIRING_OK);
    assert(plaintext_length == sizeof(response) - 1U);
    assert(memcmp(plaintext, response, plaintext_length) == 0);
    aiqa_pairing_client_session_destroy(&client);
    aiqa_secure_channel_destroy(&device);
    assert(client == NULL && device == NULL);
}

static void run_state(void)
{
    const uint8_t code[AIQA_PAIRING_CODE_SIZE] = {'1', '2', '3', '4', '5', '6', '7', '8'};
    uint8_t device_id[AIQA_PAIRING_DEVICE_ID_MAX];
    uint8_t client_nonce[AIQA_PAIRING_NONCE_SIZE];
    uint8_t device_nonce[AIQA_PAIRING_NONCE_SIZE];
    make_metadata(device_id, client_nonce, device_nonce);
    rng_t rng = {.state = 1};
    aiqa_pairing_client_session_t *session = NULL;
    assert(aiqa_pairing_client_session_create(
               &session, code, sizeof(code), 1, 1, device_id, sizeof(device_id),
               client_nonce, device_nonce, random_fill, &rng) == AIQA_PAIRING_OK);
    uint8_t output[AIQA_PAIRING_ROUND_MAX];
    size_t length = 0;
    assert(aiqa_pairing_client_session_write_round_two(
               session, output, sizeof(output), &length) ==
           AIQA_PAIRING_INVALID_STATE);
    assert(aiqa_pairing_client_session_create_finished(session, output) ==
           AIQA_PAIRING_INVALID_STATE);
    assert(aiqa_pairing_client_session_encrypt_request(
               session, output, 1, output, sizeof(output), &length) ==
           AIQA_PAIRING_INVALID_STATE);
    aiqa_pairing_client_session_destroy(&session);
    aiqa_pairing_client_session_destroy(&session);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "roundtrip") == 0) {
        run_roundtrip();
    } else if (strcmp(argv[1], "state") == 0) {
        run_state();
    } else {
        return 2;
    }
    return 0;
}
