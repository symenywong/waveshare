#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_PAIRING_CODE_SIZE 8U
#define AIQA_PAIRING_NONCE_SIZE 32U
#define AIQA_PAIRING_DEVICE_ID_MAX 32U
#define AIQA_PAIRING_ROUND_MAX 512U
#define AIQA_PAIRING_KEY_SIZE 32U
#define AIQA_PAIRING_NONCE_PREFIX_SIZE 4U
#define AIQA_PAIRING_FINISHED_SIZE 32U
#define AIQA_PAIRING_SESSION_ID_SIZE 8U

typedef enum {
  AIQA_PAIRING_CLIENT = 1,
  AIQA_PAIRING_DEVICE = 2,
} aiqa_pairing_role_t;

typedef enum {
  AIQA_PAIRING_OK = 0,
  AIQA_PAIRING_INVALID_ARGUMENT,
  AIQA_PAIRING_INVALID_STATE,
  AIQA_PAIRING_BUFFER_TOO_SMALL,
  AIQA_PAIRING_CRYPTO_ERROR,
  AIQA_PAIRING_AUTH_FAILED,
  AIQA_PAIRING_REPLAY,
} aiqa_pairing_result_t;

typedef int (*aiqa_pairing_random_fn)(void *context, unsigned char *output,
                                      size_t output_length);

typedef struct aiqa_pairing_context aiqa_pairing_context_t;

typedef struct aiqa_pairing_keys aiqa_pairing_keys_t;

aiqa_pairing_result_t
aiqa_pairing_create(aiqa_pairing_context_t **out_context,
                    aiqa_pairing_role_t role, const uint8_t *pairing_code,
                    size_t pairing_code_length, uint32_t credential_id,
                    uint64_t handshake_id, const uint8_t *device_id,
                    size_t device_id_length,
                    const uint8_t client_nonce[AIQA_PAIRING_NONCE_SIZE],
                    const uint8_t device_nonce[AIQA_PAIRING_NONCE_SIZE],
                    aiqa_pairing_random_fn random, void *random_context);

aiqa_pairing_result_t
aiqa_pairing_write_round_one(aiqa_pairing_context_t *context, uint8_t *output,
                             size_t output_capacity, size_t *out_length);
aiqa_pairing_result_t
aiqa_pairing_read_round_one(aiqa_pairing_context_t *context,
                            const uint8_t *input, size_t input_length);
aiqa_pairing_result_t
aiqa_pairing_write_round_two(aiqa_pairing_context_t *context, uint8_t *output,
                             size_t output_capacity, size_t *out_length);
aiqa_pairing_result_t
aiqa_pairing_read_round_two(aiqa_pairing_context_t *context,
                            const uint8_t *input, size_t input_length);
aiqa_pairing_result_t aiqa_pairing_derive_keys(aiqa_pairing_context_t *context,
                                               aiqa_pairing_keys_t **out_keys);

aiqa_pairing_result_t
aiqa_pairing_create_finished(aiqa_pairing_keys_t *keys,
                             aiqa_pairing_role_t sender,
                             uint8_t output[AIQA_PAIRING_FINISHED_SIZE]);
aiqa_pairing_result_t aiqa_pairing_verify_finished(aiqa_pairing_keys_t *keys,
                                                   aiqa_pairing_role_t sender,
                                                   const uint8_t *tag,
                                                   size_t tag_length);

void aiqa_pairing_keys_destroy(aiqa_pairing_keys_t **keys);
void aiqa_pairing_destroy(aiqa_pairing_context_t **context);

#ifdef __cplusplus
}
#endif
