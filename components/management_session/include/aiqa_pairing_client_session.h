#pragma once

#include "aiqa_pairing_crypto.h"
#include "aiqa_secure_channel.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct aiqa_pairing_client_session aiqa_pairing_client_session_t;

aiqa_pairing_result_t aiqa_pairing_client_session_create(
    aiqa_pairing_client_session_t **out_session,
    const uint8_t *pairing_code,
    size_t pairing_code_length,
    uint32_t credential_id,
    uint64_t handshake_id,
    const uint8_t *device_id,
    size_t device_id_length,
    const uint8_t client_nonce[AIQA_PAIRING_NONCE_SIZE],
    const uint8_t device_nonce[AIQA_PAIRING_NONCE_SIZE],
    aiqa_pairing_random_fn random,
    void *random_context);
aiqa_pairing_result_t aiqa_pairing_client_session_write_round_one(
    aiqa_pairing_client_session_t *session,
    uint8_t *output,
    size_t output_capacity,
    size_t *out_length);
aiqa_pairing_result_t aiqa_pairing_client_session_read_round_one(
    aiqa_pairing_client_session_t *session,
    const uint8_t *input,
    size_t input_length);
aiqa_pairing_result_t aiqa_pairing_client_session_write_round_two(
    aiqa_pairing_client_session_t *session,
    uint8_t *output,
    size_t output_capacity,
    size_t *out_length);
aiqa_pairing_result_t aiqa_pairing_client_session_read_round_two(
    aiqa_pairing_client_session_t *session,
    const uint8_t *input,
    size_t input_length);
aiqa_pairing_result_t aiqa_pairing_client_session_create_finished(
    aiqa_pairing_client_session_t *session,
    uint8_t output[AIQA_PAIRING_FINISHED_SIZE]);
aiqa_pairing_result_t aiqa_pairing_client_session_verify_finished(
    aiqa_pairing_client_session_t *session,
    const uint8_t *tag,
    size_t tag_length);
aiqa_pairing_result_t aiqa_pairing_client_session_encrypt_request(
    aiqa_pairing_client_session_t *session,
    const uint8_t *plaintext,
    size_t plaintext_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *out_length);
aiqa_pairing_result_t aiqa_pairing_client_session_decrypt_response(
    aiqa_pairing_client_session_t *session,
    uint8_t outer_frame_kind,
    const uint8_t *record,
    size_t record_length,
    uint8_t *plaintext,
    size_t plaintext_capacity,
    size_t *out_plaintext_length);
void aiqa_pairing_client_session_destroy(
    aiqa_pairing_client_session_t **session);

#ifdef __cplusplus
}
#endif
