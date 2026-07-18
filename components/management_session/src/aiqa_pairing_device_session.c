#include "aiqa_pairing_device_session.h"

#include "mbedtls/platform_util.h"

#include <stdbool.h>
#include <stdlib.h>

struct aiqa_pairing_device_session {
    aiqa_pairing_context_t *pairing;
    aiqa_pairing_keys_t *keys;
    aiqa_secure_channel_t *channel;
    bool confirmed;
};

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
    void *random_context)
{
    if (out_session == NULL || *out_session != NULL) {
        return AIQA_PAIRING_INVALID_ARGUMENT;
    }
    aiqa_pairing_device_session_t *session = calloc(1, sizeof(*session));
    if (session == NULL) {
        return AIQA_PAIRING_CRYPTO_ERROR;
    }
    const aiqa_pairing_result_t result = aiqa_pairing_create(
        &session->pairing,
        AIQA_PAIRING_DEVICE,
        pairing_code,
        AIQA_PAIRING_CODE_SIZE,
        credential_id,
        handshake_id,
        device_id,
        device_id_length,
        client_nonce,
        device_nonce,
        random,
        random_context);
    if (result != AIQA_PAIRING_OK) {
        mbedtls_platform_zeroize(session, sizeof(*session));
        free(session);
        return result;
    }
    *out_session = session;
    return AIQA_PAIRING_OK;
}

aiqa_pairing_result_t aiqa_pairing_device_session_write_round_one(
    aiqa_pairing_device_session_t *session,
    uint8_t *output,
    size_t output_capacity,
    size_t *out_length)
{
    if (session == NULL || session->pairing == NULL || session->keys != NULL ||
        session->channel != NULL) {
        if (out_length != NULL) *out_length = 0;
        return AIQA_PAIRING_INVALID_STATE;
    }
    return aiqa_pairing_write_round_one(
        session->pairing, output, output_capacity, out_length);
}

aiqa_pairing_result_t aiqa_pairing_device_session_read_round_one(
    aiqa_pairing_device_session_t *session,
    const uint8_t *input,
    size_t input_length)
{
    if (session == NULL || session->pairing == NULL || session->keys != NULL ||
        session->channel != NULL) {
        return AIQA_PAIRING_INVALID_STATE;
    }
    return aiqa_pairing_read_round_one(session->pairing, input, input_length);
}

aiqa_pairing_result_t aiqa_pairing_device_session_complete_round_two(
    aiqa_pairing_device_session_t *session,
    const uint8_t *client_round,
    size_t client_round_length,
    uint8_t *device_round,
    size_t device_round_capacity,
    size_t *out_device_round_length)
{
    if (out_device_round_length != NULL) *out_device_round_length = 0;
    if (session == NULL || session->pairing == NULL || session->keys != NULL ||
        session->channel != NULL) {
        return AIQA_PAIRING_INVALID_STATE;
    }
    aiqa_pairing_result_t result = aiqa_pairing_read_round_two(
        session->pairing, client_round, client_round_length);
    if (result == AIQA_PAIRING_OK) {
        result = aiqa_pairing_write_round_two(
            session->pairing,
            device_round,
            device_round_capacity,
            out_device_round_length);
    }
    if (result == AIQA_PAIRING_OK) {
        result = aiqa_pairing_derive_keys(session->pairing, &session->keys);
    }
    return result;
}

aiqa_pairing_result_t aiqa_pairing_device_session_confirm(
    aiqa_pairing_device_session_t *session,
    const uint8_t client_finished[AIQA_PAIRING_FINISHED_SIZE],
    uint8_t device_finished[AIQA_PAIRING_FINISHED_SIZE])
{
    if (device_finished != NULL) {
        mbedtls_platform_zeroize(device_finished, AIQA_PAIRING_FINISHED_SIZE);
    }
    if (session == NULL || session->keys == NULL || session->channel != NULL ||
        session->confirmed || client_finished == NULL || device_finished == NULL) {
        return AIQA_PAIRING_INVALID_STATE;
    }
    aiqa_pairing_result_t result = aiqa_pairing_verify_finished(
        session->keys,
        AIQA_PAIRING_CLIENT,
        client_finished,
        AIQA_PAIRING_FINISHED_SIZE);
    if (result == AIQA_PAIRING_OK) {
        result = aiqa_pairing_create_finished(
            session->keys, AIQA_PAIRING_DEVICE, device_finished);
    }
    if (result == AIQA_PAIRING_OK) {
        result = aiqa_secure_channel_create(&session->channel, &session->keys);
    }
    if (result == AIQA_PAIRING_OK) {
        session->confirmed = true;
    } else {
        aiqa_pairing_keys_destroy(&session->keys);
        mbedtls_platform_zeroize(device_finished, AIQA_PAIRING_FINISHED_SIZE);
    }
    return result;
}

aiqa_pairing_result_t aiqa_pairing_device_session_take_channel(
    aiqa_pairing_device_session_t *session,
    aiqa_secure_channel_t **out_channel,
    uint8_t out_session_id[AIQA_PAIRING_SESSION_ID_SIZE])
{
    if (out_session_id != NULL) {
        mbedtls_platform_zeroize(
            out_session_id, AIQA_PAIRING_SESSION_ID_SIZE);
    }
    if (session == NULL || out_channel == NULL || *out_channel != NULL ||
        out_session_id == NULL || !session->confirmed ||
        session->channel == NULL) {
        return AIQA_PAIRING_INVALID_ARGUMENT;
    }
    const aiqa_pairing_result_t result = aiqa_secure_channel_copy_session_id(
        session->channel, out_session_id);
    if (result != AIQA_PAIRING_OK) {
        return result;
    }
    *out_channel = session->channel;
    session->channel = NULL;
    session->confirmed = false;
    return AIQA_PAIRING_OK;
}

void aiqa_pairing_device_session_destroy(
    aiqa_pairing_device_session_t **session)
{
    if (session == NULL || *session == NULL) {
        return;
    }
    aiqa_secure_channel_destroy(&(*session)->channel);
    aiqa_pairing_keys_destroy(&(*session)->keys);
    aiqa_pairing_destroy(&(*session)->pairing);
    mbedtls_platform_zeroize(*session, sizeof(**session));
    free(*session);
    *session = NULL;
}
