#include "aiqa_pairing_crypto.h"
#include "aiqa_secure_channel.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  uint64_t state;
} test_rng_t;

static int test_random(void *context, unsigned char *output,
                       size_t output_length) {
  test_rng_t *rng = context;
  for (size_t index = 0; index < output_length; ++index) {
    rng->state ^= rng->state << 13U;
    rng->state ^= rng->state >> 7U;
    rng->state ^= rng->state << 17U;
    output[index] = (uint8_t)rng->state;
  }
  return 0;
}

static int failing_random(void *context, unsigned char *output,
                          size_t output_length) {
  (void)context;
  (void)output;
  (void)output_length;
  return -1;
}

typedef struct {
  aiqa_pairing_context_t *client;
  aiqa_pairing_context_t *device;
  aiqa_pairing_keys_t *client_keys;
  aiqa_pairing_keys_t *device_keys;
} pair_fixture_t;

static aiqa_pairing_result_t make_pair_with_device_ids(
    pair_fixture_t *fixture, const uint8_t client_code[AIQA_PAIRING_CODE_SIZE],
    const uint8_t device_code[AIQA_PAIRING_CODE_SIZE],
    const uint8_t client_device_id[AIQA_PAIRING_DEVICE_ID_MAX],
    const uint8_t device_device_id[AIQA_PAIRING_DEVICE_ID_MAX]) {
  uint8_t client_nonce[AIQA_PAIRING_NONCE_SIZE];
  uint8_t device_nonce[AIQA_PAIRING_NONCE_SIZE];
  for (size_t index = 0; index < sizeof(client_nonce); ++index) {
    client_nonce[index] = (uint8_t)(index + 1U);
    device_nonce[index] = (uint8_t)(0x80U + index);
  }
  test_rng_t client_rng = {.state = UINT64_C(0x123456789abcdef0)};
  test_rng_t device_rng = {.state = UINT64_C(0x0fedcba987654321)};
  assert(aiqa_pairing_create(
             &fixture->client, AIQA_PAIRING_CLIENT, client_code,
             AIQA_PAIRING_CODE_SIZE, 7, UINT64_C(0x0102030405060708),
             client_device_id, AIQA_PAIRING_DEVICE_ID_MAX, client_nonce,
             device_nonce, test_random, &client_rng) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_create(
             &fixture->device, AIQA_PAIRING_DEVICE, device_code,
             AIQA_PAIRING_CODE_SIZE, 7, UINT64_C(0x0102030405060708),
             device_device_id, AIQA_PAIRING_DEVICE_ID_MAX, client_nonce,
             device_nonce, test_random, &device_rng) == AIQA_PAIRING_OK);

  uint8_t client_r1[AIQA_PAIRING_ROUND_MAX];
  uint8_t device_r1[AIQA_PAIRING_ROUND_MAX];
  uint8_t client_r2[AIQA_PAIRING_ROUND_MAX];
  uint8_t device_r2[AIQA_PAIRING_ROUND_MAX];
  size_t client_r1_length = 0;
  size_t device_r1_length = 0;
  size_t client_r2_length = 0;
  size_t device_r2_length = 0;
  assert(aiqa_pairing_write_round_one(fixture->client, client_r1,
                                      sizeof(client_r1),
                                      &client_r1_length) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_read_round_one(fixture->device, client_r1,
                                     client_r1_length) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_write_round_one(fixture->device, device_r1,
                                      sizeof(device_r1),
                                      &device_r1_length) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_read_round_one(fixture->client, device_r1,
                                     device_r1_length) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_write_round_two(fixture->client, client_r2,
                                      sizeof(client_r2),
                                      &client_r2_length) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_read_round_two(fixture->device, client_r2,
                                     client_r2_length) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_write_round_two(fixture->device, device_r2,
                                      sizeof(device_r2),
                                      &device_r2_length) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_read_round_two(fixture->client, device_r2,
                                     device_r2_length) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_derive_keys(fixture->client, &fixture->client_keys) ==
         AIQA_PAIRING_OK);
  return aiqa_pairing_derive_keys(fixture->device, &fixture->device_keys);
}

static aiqa_pairing_result_t
make_pair(pair_fixture_t *fixture,
          const uint8_t client_code[AIQA_PAIRING_CODE_SIZE],
          const uint8_t device_code[AIQA_PAIRING_CODE_SIZE]) {
  uint8_t device_id[AIQA_PAIRING_DEVICE_ID_MAX];
  for (size_t index = 0; index < sizeof(device_id); ++index) {
    device_id[index] = (uint8_t)(0x40U + index);
  }
  return make_pair_with_device_ids(fixture, client_code, device_code, device_id,
                                   device_id);
}

static void clear_fixture(pair_fixture_t *fixture) {
  aiqa_pairing_keys_destroy(&fixture->client_keys);
  aiqa_pairing_keys_destroy(&fixture->device_keys);
  aiqa_pairing_destroy(&fixture->client);
  aiqa_pairing_destroy(&fixture->device);
}

static void run_roundtrip(void) {
  const uint8_t code[AIQA_PAIRING_CODE_SIZE] = {'3', '1', '4', '1',
                                                '5', '9', '2', '6'};
  pair_fixture_t fixture = {0};
  assert(make_pair(&fixture, code, code) == AIQA_PAIRING_OK);

  uint8_t client_tag[AIQA_PAIRING_FINISHED_SIZE];
  uint8_t device_tag[AIQA_PAIRING_FINISHED_SIZE];
  (void)memset(client_tag, 0xa5, sizeof(client_tag));
  assert(aiqa_pairing_create_finished(fixture.client_keys, AIQA_PAIRING_DEVICE,
                                      client_tag) ==
         AIQA_PAIRING_INVALID_STATE);
  for (size_t index = 0; index < sizeof(client_tag); ++index) {
    assert(client_tag[index] == 0);
  }
  assert(aiqa_pairing_create_finished(fixture.client_keys, AIQA_PAIRING_CLIENT,
                                      client_tag) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_create_finished(fixture.device_keys, AIQA_PAIRING_DEVICE,
                                      device_tag) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_verify_finished(fixture.device_keys, AIQA_PAIRING_CLIENT,
                                      client_tag,
                                      sizeof(client_tag)) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_verify_finished(fixture.client_keys, AIQA_PAIRING_DEVICE,
                                      device_tag,
                                      sizeof(device_tag)) == AIQA_PAIRING_OK);
  clear_fixture(&fixture);
  assert(fixture.client == NULL && fixture.device == NULL);
}

static void run_wrong_code(void) {
  const uint8_t client_code[AIQA_PAIRING_CODE_SIZE] = {'3', '1', '4', '1',
                                                       '5', '9', '2', '6'};
  const uint8_t device_code[AIQA_PAIRING_CODE_SIZE] = {'2', '7', '1', '8',
                                                       '2', '8', '1', '8'};
  pair_fixture_t fixture = {0};
  assert(make_pair(&fixture, client_code, device_code) == AIQA_PAIRING_OK);
  uint8_t client_tag[AIQA_PAIRING_FINISHED_SIZE];
  assert(aiqa_pairing_create_finished(fixture.client_keys, AIQA_PAIRING_CLIENT,
                                      client_tag) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_verify_finished(fixture.device_keys, AIQA_PAIRING_CLIENT,
                                      client_tag, sizeof(client_tag)) ==
         AIQA_PAIRING_AUTH_FAILED);
  clear_fixture(&fixture);
}

static void run_transcript_binding(void) {
  const uint8_t code[AIQA_PAIRING_CODE_SIZE] = {'1', '6', '1', '8',
                                                '0', '3', '3', '9'};
  uint8_t client_device_id[AIQA_PAIRING_DEVICE_ID_MAX];
  uint8_t device_device_id[AIQA_PAIRING_DEVICE_ID_MAX];
  for (size_t index = 0; index < sizeof(client_device_id); ++index) {
    client_device_id[index] = (uint8_t)(0x40U + index);
    device_device_id[index] = client_device_id[index];
  }
  device_device_id[0] ^= 0x01U;

  pair_fixture_t fixture = {0};
  assert(make_pair_with_device_ids(&fixture, code, code, client_device_id,
                                   device_device_id) == AIQA_PAIRING_OK);
  uint8_t client_tag[AIQA_PAIRING_FINISHED_SIZE];
  assert(aiqa_pairing_create_finished(fixture.client_keys, AIQA_PAIRING_CLIENT,
                                      client_tag) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_verify_finished(fixture.device_keys, AIQA_PAIRING_CLIENT,
                                      client_tag, sizeof(client_tag)) ==
         AIQA_PAIRING_AUTH_FAILED);
  clear_fixture(&fixture);
}

static void run_state(void) {
  const uint8_t code[AIQA_PAIRING_CODE_SIZE] = {'1', '2', '3', '4',
                                                '5', '6', '7', '8'};
  uint8_t nonce[AIQA_PAIRING_NONCE_SIZE] = {0};
  uint8_t device_id[1] = {1};
  test_rng_t rng = {.state = 1};
  aiqa_pairing_context_t *context = NULL;
  assert(aiqa_pairing_create(&context, AIQA_PAIRING_CLIENT, code, sizeof(code),
                             1, 1, device_id, sizeof(device_id), nonce, nonce,
                             test_random, &rng) == AIQA_PAIRING_OK);
  uint8_t output[AIQA_PAIRING_ROUND_MAX] = {0};
  size_t output_length = 99;
  assert(aiqa_pairing_write_round_two(context, output, sizeof(output),
                                      &output_length) ==
         AIQA_PAIRING_INVALID_STATE);
  assert(output_length == 0);
  assert(aiqa_pairing_write_round_one(context, output, 1, &output_length) ==
         AIQA_PAIRING_BUFFER_TOO_SMALL);
  assert(output_length == 0);
  aiqa_pairing_destroy(&context);
  aiqa_pairing_destroy(&context);
}

static void run_failure_paths(void) {
  const uint8_t code[AIQA_PAIRING_CODE_SIZE] = {'0', '1', '2', '3',
                                                '4', '5', '6', '7'};
  const uint8_t invalid_code[AIQA_PAIRING_CODE_SIZE] = {'0', '1', '2', '3',
                                                        '4', '5', '6', 'x'};
  uint8_t nonce[AIQA_PAIRING_NONCE_SIZE] = {0};
  uint8_t device_id[AIQA_PAIRING_DEVICE_ID_MAX] = {1};
  aiqa_pairing_context_t *context = NULL;
  assert(aiqa_pairing_create(&context, AIQA_PAIRING_CLIENT, invalid_code,
                             sizeof(invalid_code), 1, 1, device_id,
                             sizeof(device_id), nonce, nonce, failing_random,
                             NULL) == AIQA_PAIRING_INVALID_ARGUMENT);
  assert(context == NULL);
  assert(aiqa_pairing_create(&context, AIQA_PAIRING_CLIENT, code, sizeof(code),
                             1, 1, device_id, sizeof(device_id), nonce, nonce,
                             failing_random, NULL) == AIQA_PAIRING_OK);

  uint8_t output[AIQA_PAIRING_ROUND_MAX];
  (void)memset(output, 0xa5, sizeof(output));
  size_t output_length = 99;
  assert(aiqa_pairing_write_round_one(context, output, sizeof(output),
                                      &output_length) ==
         AIQA_PAIRING_CRYPTO_ERROR);
  assert(output_length == 0);
  for (size_t index = 0; index < sizeof(output); ++index) {
    assert(output[index] == 0);
  }
  assert(aiqa_pairing_write_round_one(context, output, sizeof(output),
                                      &output_length) ==
         AIQA_PAIRING_INVALID_STATE);
  aiqa_pairing_destroy(&context);
}

static void run_tampered_round(void) {
  const uint8_t code[AIQA_PAIRING_CODE_SIZE] = {'9', '8', '7', '6',
                                                '5', '4', '3', '2'};
  uint8_t nonce[AIQA_PAIRING_NONCE_SIZE] = {0};
  uint8_t device_id[AIQA_PAIRING_DEVICE_ID_MAX] = {1};
  test_rng_t client_rng = {.state = UINT64_C(0x1111222233334444)};
  test_rng_t device_rng = {.state = UINT64_C(0x5555666677778888)};
  aiqa_pairing_context_t *client = NULL;
  aiqa_pairing_context_t *device = NULL;
  assert(aiqa_pairing_create(&client, AIQA_PAIRING_CLIENT, code, sizeof(code),
                             1, 1, device_id, sizeof(device_id), nonce, nonce,
                             test_random, &client_rng) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_create(&device, AIQA_PAIRING_DEVICE, code, sizeof(code),
                             1, 1, device_id, sizeof(device_id), nonce, nonce,
                             test_random, &device_rng) == AIQA_PAIRING_OK);

  uint8_t client_round[AIQA_PAIRING_ROUND_MAX];
  uint8_t device_round[AIQA_PAIRING_ROUND_MAX];
  size_t client_round_length = 0;
  size_t device_round_length = 0;
  assert(aiqa_pairing_write_round_one(client, client_round,
                                      sizeof(client_round),
                                      &client_round_length) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_write_round_one(device, device_round,
                                      sizeof(device_round),
                                      &device_round_length) == AIQA_PAIRING_OK);
  assert(client_round_length > 0);
  client_round[client_round_length - 1U] ^= 0x01U;
  assert(
      aiqa_pairing_read_round_one(device, client_round, client_round_length) ==
      AIQA_PAIRING_CRYPTO_ERROR);
  assert(aiqa_pairing_write_round_two(
             device, device_round, sizeof(device_round),
             &device_round_length) == AIQA_PAIRING_INVALID_STATE);
  aiqa_pairing_destroy(&client);
  aiqa_pairing_destroy(&device);
}

static void run_channel(void) {
  const uint8_t code[AIQA_PAIRING_CODE_SIZE] = {'8', '6', '7', '5',
                                                '3', '0', '9', '1'};
  pair_fixture_t fixture = {0};
  assert(make_pair(&fixture, code, code) == AIQA_PAIRING_OK);
  aiqa_secure_channel_t *client = NULL;
  aiqa_secure_channel_t *device = NULL;
  assert(aiqa_secure_channel_create(&client, &fixture.client_keys) ==
         AIQA_PAIRING_INVALID_STATE);
  uint8_t client_finished[AIQA_PAIRING_FINISHED_SIZE];
  uint8_t device_finished[AIQA_PAIRING_FINISHED_SIZE];
  assert(aiqa_pairing_create_finished(fixture.client_keys, AIQA_PAIRING_CLIENT,
                                      client_finished) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_create_finished(fixture.device_keys, AIQA_PAIRING_DEVICE,
                                      device_finished) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_verify_finished(
             fixture.device_keys, AIQA_PAIRING_CLIENT, client_finished,
             sizeof(client_finished)) == AIQA_PAIRING_OK);
  assert(aiqa_pairing_verify_finished(
             fixture.client_keys, AIQA_PAIRING_DEVICE, device_finished,
             sizeof(device_finished)) == AIQA_PAIRING_OK);
  assert(aiqa_secure_channel_create(&client, &fixture.client_keys) ==
         AIQA_PAIRING_OK);
  assert(aiqa_secure_channel_create(&device, &fixture.device_keys) ==
         AIQA_PAIRING_OK);
  assert(fixture.client_keys == NULL && fixture.device_keys == NULL);

  assert(aiqa_secure_channel_encrypt(client, AIQA_SECURE_FRAME_RESPONSE,
                                     (const uint8_t *)"x", 1, device_finished,
                                     sizeof(device_finished), &(size_t){0}) ==
         AIQA_PAIRING_INVALID_ARGUMENT);
  assert(aiqa_secure_channel_encrypt(device, AIQA_SECURE_FRAME_REQUEST,
                                     (const uint8_t *)"x", 1, device_finished,
                                     sizeof(device_finished), &(size_t){0}) ==
         AIQA_PAIRING_INVALID_ARGUMENT);

  const uint8_t message[] = {'h', 'e', 'l', 'l', 'o'};
  uint8_t overlapping_output[AIQA_SECURE_RECORD_MAX] = {0};
  (void)memcpy(overlapping_output, message, sizeof(message));
  size_t overlapping_length = 99;
  assert(aiqa_secure_channel_encrypt(
             client, AIQA_SECURE_FRAME_REQUEST, overlapping_output,
             sizeof(message), overlapping_output, sizeof(overlapping_output),
             &overlapping_length) == AIQA_PAIRING_INVALID_ARGUMENT);
  assert(overlapping_length == 0);

  uint8_t record[AIQA_SECURE_RECORD_MAX] = {0};
  size_t record_length = 0;
  assert(aiqa_secure_channel_encrypt(client, AIQA_SECURE_FRAME_REQUEST, message,
                                     sizeof(message), record, sizeof(record),
                                     &record_length) == AIQA_PAIRING_OK);
  uint8_t plaintext[32] = {0};
  size_t plaintext_length = 0;
  uint8_t overlapping_record[AIQA_SECURE_RECORD_MAX] = {0};
  (void)memcpy(overlapping_record, record, record_length);
  assert(aiqa_secure_channel_decrypt(
             device, AIQA_SECURE_FRAME_REQUEST, overlapping_record,
             record_length, overlapping_record + AIQA_SECURE_RECORD_HEADER_SIZE,
             sizeof(message),
             &plaintext_length) == AIQA_PAIRING_INVALID_ARGUMENT);
  assert(plaintext_length == 0);
  assert(aiqa_secure_channel_decrypt(device, AIQA_SECURE_FRAME_REQUEST, record,
                                     record_length, plaintext,
                                     sizeof(plaintext),
                                     &plaintext_length) == AIQA_PAIRING_OK);
  assert(plaintext_length == sizeof(message));
  assert(memcmp(plaintext, message, sizeof(message)) == 0);
  assert(aiqa_secure_channel_decrypt(
             device, 0, record, record_length, plaintext, sizeof(plaintext),
             &plaintext_length) == AIQA_PAIRING_INVALID_ARGUMENT);
  assert(plaintext_length == 0);
  for (size_t index = 0; index < sizeof(plaintext); ++index) {
    assert(plaintext[index] == 0);
  }
  assert(aiqa_secure_channel_decrypt(device, AIQA_SECURE_FRAME_REQUEST, record,
                                     record_length, plaintext,
                                     sizeof(plaintext),
                                     &plaintext_length) == AIQA_PAIRING_REPLAY);
  assert(plaintext_length == 0);
  for (size_t index = 0; index < sizeof(plaintext); ++index) {
    assert(plaintext[index] == 0);
  }

  uint8_t second_record[AIQA_SECURE_RECORD_MAX] = {0};
  size_t second_length = 0;
  assert(aiqa_secure_channel_encrypt(client, AIQA_SECURE_FRAME_REQUEST, message,
                                     sizeof(message), second_record,
                                     sizeof(second_record),
                                     &second_length) == AIQA_PAIRING_OK);
  second_record[AIQA_SECURE_RECORD_HEADER_SIZE] ^= 0x01U;
  assert(aiqa_secure_channel_decrypt(device, AIQA_SECURE_FRAME_REQUEST,
                                     second_record, second_length, plaintext,
                                     sizeof(plaintext), &plaintext_length) ==
         AIQA_PAIRING_AUTH_FAILED);
  assert(plaintext_length == 0);
  for (size_t index = 0; index < sizeof(plaintext); ++index) {
    assert(plaintext[index] == 0);
  }
  second_record[AIQA_SECURE_RECORD_HEADER_SIZE] ^= 0x01U;

  assert(aiqa_secure_channel_decrypt(device, AIQA_SECURE_FRAME_RESPONSE,
                                     second_record, second_length, plaintext,
                                     sizeof(plaintext), &plaintext_length) ==
         AIQA_PAIRING_AUTH_FAILED);
  assert(plaintext_length == 0);

  second_record[5] = AIQA_PAIRING_DEVICE;
  assert(aiqa_secure_channel_decrypt(device, AIQA_SECURE_FRAME_REQUEST,
                                     second_record, second_length, plaintext,
                                     sizeof(plaintext), &plaintext_length) ==
         AIQA_PAIRING_AUTH_FAILED);
  second_record[5] = AIQA_PAIRING_CLIENT;

  second_record[8] ^= 0x01U;
  assert(aiqa_secure_channel_decrypt(device, AIQA_SECURE_FRAME_REQUEST,
                                     second_record, second_length, plaintext,
                                     sizeof(plaintext), &plaintext_length) ==
         AIQA_PAIRING_AUTH_FAILED);
  second_record[8] ^= 0x01U;

  assert(aiqa_secure_channel_decrypt(device, AIQA_SECURE_FRAME_REQUEST,
                                     second_record, second_length, plaintext,
                                     sizeof(plaintext),
                                     &plaintext_length) == AIQA_PAIRING_OK);

  uint8_t response_record[AIQA_SECURE_RECORD_MAX] = {0};
  size_t response_length = 0;
  assert(aiqa_secure_channel_encrypt(device, AIQA_SECURE_FRAME_RESPONSE,
                                     message, sizeof(message), response_record,
                                     sizeof(response_record),
                                     &response_length) == AIQA_PAIRING_OK);
  assert(aiqa_secure_channel_decrypt(client, AIQA_SECURE_FRAME_RESPONSE,
                                     response_record, response_length,
                                     plaintext, sizeof(plaintext),
                                     &plaintext_length) == AIQA_PAIRING_OK);
  assert(plaintext_length == sizeof(message));
  assert(memcmp(plaintext, message, sizeof(message)) == 0);

  aiqa_secure_channel_destroy(&client);
  aiqa_secure_channel_destroy(&device);
  clear_fixture(&fixture);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  if (strcmp(argv[1], "roundtrip") == 0) {
    run_roundtrip();
  } else if (strcmp(argv[1], "wrong-code") == 0) {
    run_wrong_code();
  } else if (strcmp(argv[1], "transcript-binding") == 0) {
    run_transcript_binding();
  } else if (strcmp(argv[1], "state") == 0) {
    run_state();
  } else if (strcmp(argv[1], "failure-paths") == 0) {
    run_failure_paths();
  } else if (strcmp(argv[1], "tampered-round") == 0) {
    run_tampered_round();
  } else if (strcmp(argv[1], "channel") == 0) {
    run_channel();
  } else {
    return 2;
  }
  return 0;
}
