#pragma once

#include "aiqa_pairing_crypto.h"
#include "aiqa_secure_channel.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct aiqa_pairing_device_session aiqa_pairing_device_session_t;

aiqa_pairing_result_t aiqa_pairing_device_session_create(
    aiqa_pairing_device_session_t **out_session,
    const uint8_t pairing_code[AIQA_PAIRING_CODE_SIZE],
    uint32_t credential_id,
    uint64_t handshake_id,
    const uint8_t *device_id,
    size_t device_id_length,
    const uint8_t client_nonce[AIQA_PAIRING_NONCE_SIZE],
    const uint8_t device_nonce[AIQA_PAIRING_NONCE_SIZE],
    aiqa_pairing_random_fn random,
    void *random_context);
aiqa_pairing_result_t aiqa_pairing_device_session_write_round_one(
    aiqa_pairing_device_session_t *session,
    uint8_t *output,
    size_t output_capacity,
    size_t *out_length);
aiqa_pairing_result_t aiqa_pairing_device_session_read_round_one(
    aiqa_pairing_device_session_t *session,
    const uint8_t *input,
    size_t input_length);
aiqa_pairing_result_t aiqa_pairing_device_session_complete_round_two(
    aiqa_pairing_device_session_t *session,
    const uint8_t *client_round,
    size_t client_round_length,
    uint8_t *device_round,
    size_t device_round_capacity,
    size_t *out_device_round_length);
aiqa_pairing_result_t aiqa_pairing_device_session_confirm(
    aiqa_pairing_device_session_t *session,
    const uint8_t client_finished[AIQA_PAIRING_FINISHED_SIZE],
    uint8_t device_finished[AIQA_PAIRING_FINISHED_SIZE]);
aiqa_pairing_result_t aiqa_pairing_device_session_take_channel(
    aiqa_pairing_device_session_t *session,
    aiqa_secure_channel_t **out_channel,
    uint8_t out_session_id[AIQA_PAIRING_SESSION_ID_SIZE]);
void aiqa_pairing_device_session_destroy(
    aiqa_pairing_device_session_t **session);

#ifdef __cplusplus
}
#endif
