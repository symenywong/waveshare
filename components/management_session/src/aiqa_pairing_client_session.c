#include "aiqa_pairing_client_session.h"

#include "mbedtls/platform_util.h"

#include <stdbool.h>
#include <stdlib.h>

struct aiqa_pairing_client_session {
    aiqa_pairing_context_t *pairing;
    aiqa_pairing_keys_t *keys;
    aiqa_secure_channel_t *channel;
    bool finished_created;
};

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
    void *random_context)
{
    if (out_session == NULL || *out_session != NULL) {
        return AIQA_PAIRING_INVALID_ARGUMENT;
    }
    aiqa_pairing_client_session_t *session = calloc(1, sizeof(*session));
    if (session == NULL) {
        return AIQA_PAIRING_CRYPTO_ERROR;
    }
    const aiqa_pairing_result_t result = aiqa_pairing_create(
        &session->pairing,
        AIQA_PAIRING_CLIENT,
        pairing_code,
        pairing_code_length,
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

aiqa_pairing_result_t aiqa_pairing_client_session_write_round_one(
    aiqa_pairing_client_session_t *session,
    uint8_t *output,
    size_t output_capacity,
    size_t *out_length)
{
    if (session == NULL || session->pairing == NULL || session->keys != NULL ||
        session->channel != NULL) {
        if (out_length != NULL) {
            *out_length = 0;
        }
        return AIQA_PAIRING_INVALID_STATE;
    }
    return aiqa_pairing_write_round_one(
        session->pairing, output, output_capacity, out_length);
}

aiqa_pairing_result_t aiqa_pairing_client_session_read_round_one(
    aiqa_pairing_client_session_t *session,
    const uint8_t *input,
    size_t input_length)
{
    if (session == NULL || session->pairing == NULL || session->keys != NULL ||
        session->channel != NULL) {
        return AIQA_PAIRING_INVALID_STATE;
    }
    return aiqa_pairing_read_round_one(session->pairing, input, input_length);
}

aiqa_pairing_result_t aiqa_pairing_client_session_write_round_two(
    aiqa_pairing_client_session_t *session,
    uint8_t *output,
    size_t output_capacity,
    size_t *out_length)
{
    if (session == NULL || session->pairing == NULL || session->keys != NULL ||
        session->channel != NULL) {
        if (out_length != NULL) {
            *out_length = 0;
        }
        return AIQA_PAIRING_INVALID_STATE;
    }
    return aiqa_pairing_write_round_two(
        session->pairing, output, output_capacity, out_length);
}

aiqa_pairing_result_t aiqa_pairing_client_session_read_round_two(
    aiqa_pairing_client_session_t *session,
    const uint8_t *input,
    size_t input_length)
{
    if (session == NULL || session->pairing == NULL || session->keys != NULL ||
        session->channel != NULL) {
        return AIQA_PAIRING_INVALID_STATE;
    }
    return aiqa_pairing_read_round_two(session->pairing, input, input_length);
}

aiqa_pairing_result_t aiqa_pairing_client_session_create_finished(
    aiqa_pairing_client_session_t *session,
    uint8_t output[AIQA_PAIRING_FINISHED_SIZE])
{
    if (output != NULL) {
        mbedtls_platform_zeroize(output, AIQA_PAIRING_FINISHED_SIZE);
    }
    if (session == NULL || output == NULL || session->pairing == NULL ||
        session->keys != NULL || session->channel != NULL ||
        session->finished_created) {
        return AIQA_PAIRING_INVALID_STATE;
    }
    aiqa_pairing_result_t result =
        aiqa_pairing_derive_keys(session->pairing, &session->keys);
    if (result != AIQA_PAIRING_OK) {
        return result;
    }
    result = aiqa_pairing_create_finished(
        session->keys, AIQA_PAIRING_CLIENT, output);
    if (result == AIQA_PAIRING_OK) {
        session->finished_created = true;
    }
    return result;
}

aiqa_pairing_result_t aiqa_pairing_client_session_verify_finished(
    aiqa_pairing_client_session_t *session,
    const uint8_t *tag,
    size_t tag_length)
{
    if (session == NULL || tag == NULL || session->keys == NULL ||
        session->channel != NULL || !session->finished_created) {
        return AIQA_PAIRING_INVALID_STATE;
    }
    aiqa_pairing_result_t result = aiqa_pairing_verify_finished(
        session->keys, AIQA_PAIRING_DEVICE, tag, tag_length);
    if (result != AIQA_PAIRING_OK) {
        aiqa_pairing_keys_destroy(&session->keys);
        return result;
    }
    result = aiqa_secure_channel_create(&session->channel, &session->keys);
    return result;
}

aiqa_pairing_result_t aiqa_pairing_client_session_encrypt_request(
    aiqa_pairing_client_session_t *session,
    const uint8_t *plaintext,
    size_t plaintext_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *out_length)
{
    if (session == NULL || session->channel == NULL) {
        if (out_length != NULL) {
            *out_length = 0;
        }
        return AIQA_PAIRING_INVALID_STATE;
    }
    return aiqa_secure_channel_encrypt(
        session->channel,
        AIQA_SECURE_FRAME_REQUEST,
        plaintext,
        plaintext_length,
        output,
        output_capacity,
        out_length);
}

aiqa_pairing_result_t aiqa_pairing_client_session_decrypt_response(
    aiqa_pairing_client_session_t *session,
    uint8_t outer_frame_kind,
    const uint8_t *record,
    size_t record_length,
    uint8_t *plaintext,
    size_t plaintext_capacity,
    size_t *out_plaintext_length)
{
    if (session == NULL || session->channel == NULL) {
        if (out_plaintext_length != NULL) {
            *out_plaintext_length = 0;
        }
        return AIQA_PAIRING_INVALID_STATE;
    }
    return aiqa_secure_channel_decrypt(
        session->channel,
        outer_frame_kind,
        record,
        record_length,
        plaintext,
        plaintext_capacity,
        out_plaintext_length);
}

void aiqa_pairing_client_session_destroy(
    aiqa_pairing_client_session_t **session)
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
