#pragma once

#include "aiqa_pairing_crypto.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_SECURE_RECORD_HEADER_SIZE 28U
#define AIQA_SECURE_RECORD_TAG_SIZE 16U
#define AIQA_SECURE_RECORD_MAX 4096U
#define AIQA_SECURE_PLAINTEXT_MAX                                              \
  (AIQA_SECURE_RECORD_MAX - AIQA_SECURE_RECORD_HEADER_SIZE -                   \
   AIQA_SECURE_RECORD_TAG_SIZE)
#define AIQA_SECURE_FRAME_REQUEST 1U
#define AIQA_SECURE_FRAME_RESPONSE 2U
#define AIQA_SECURE_FRAME_EVENT 3U

typedef struct aiqa_secure_channel aiqa_secure_channel_t;

/*
 * Consumes one opaque key handle only after both role-specific Finished proofs
 * completed. A channel has one owning task and is not safe for concurrent
 * calls. Encryption and decryption input/output regions must not overlap.
 */
aiqa_pairing_result_t
aiqa_secure_channel_create(aiqa_secure_channel_t **out_channel,
                           aiqa_pairing_keys_t **confirmed_keys);
aiqa_pairing_result_t
aiqa_secure_channel_encrypt(aiqa_secure_channel_t *channel,
                            uint8_t outer_frame_kind, const uint8_t *plaintext,
                            size_t plaintext_length, uint8_t *output,
                            size_t output_capacity, size_t *out_length);
aiqa_pairing_result_t aiqa_secure_channel_decrypt(
    aiqa_secure_channel_t *channel, uint8_t outer_frame_kind,
    const uint8_t *record, size_t record_length, uint8_t *plaintext,
    size_t plaintext_capacity, size_t *out_plaintext_length);
void aiqa_secure_channel_destroy(aiqa_secure_channel_t **channel);

#ifdef __cplusplus
}
#endif
