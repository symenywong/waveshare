#include "aiqa_pairing_client_session.h"

#include "emscripten.h"
#include "mbedtls/platform_util.h"

#include <stddef.h>
#include <stdint.h>

#define AIQA_WASM_SESSION_SLOTS 4U
#define AIQA_WASM_HANDLE_SLOT_MASK 0xffU

typedef struct {
    aiqa_pairing_client_session_t *session;
    uint32_t generation;
} aiqa_wasm_slot_t;

static aiqa_wasm_slot_t s_slots[AIQA_WASM_SESSION_SLOTS];
static uint32_t s_next_generation = 1U;

EM_JS(int, browser_random_fill, (unsigned char *output, size_t output_length), {
    try {
        if (!globalThis.crypto || !globalThis.crypto.getRandomValues) return -1;
        const maximum = 65536;
        for (let offset = 0; offset < output_length; offset += maximum) {
            const length = Math.min(maximum, output_length - offset);
            globalThis.crypto.getRandomValues(
                HEAPU8.subarray(output + offset, output + offset + length));
        }
        return 0;
    } catch (_) {
        return -1;
    }
});

static int random_fill(void *context, unsigned char *output, size_t output_length)
{
    (void)context;
    return browser_random_fill(output, output_length);
}

static int failure(aiqa_pairing_result_t result)
{
    return -((int)result + 1);
}

static aiqa_wasm_slot_t *slot_for(uint32_t handle)
{
    const uint32_t encoded_slot = handle & AIQA_WASM_HANDLE_SLOT_MASK;
    const uint32_t generation = handle >> 8U;
    if (encoded_slot == 0U || encoded_slot > AIQA_WASM_SESSION_SLOTS ||
        generation == 0U) {
        return NULL;
    }
    aiqa_wasm_slot_t *slot = &s_slots[encoded_slot - 1U];
    return slot->session != NULL && slot->generation == generation ? slot : NULL;
}

static uint32_t next_generation(void)
{
    /* Keep the packed handle <= INT32_MAX for the JavaScript FFI contract. */
    uint32_t generation = s_next_generation++ & 0x007fffffU;
    if (generation == 0U) {
        generation = s_next_generation++ & 0x007fffffU;
    }
    return generation;
}

EMSCRIPTEN_KEEPALIVE int aiqa_wasm_api_version(void)
{
    return 1;
}

EMSCRIPTEN_KEEPALIVE int aiqa_wasm_session_create(
    const uint8_t *pairing_code,
    uint32_t credential_id,
    uint32_t handshake_high,
    uint32_t handshake_low,
    const uint8_t *device_id,
    size_t device_id_length,
    const uint8_t *client_nonce,
    const uint8_t *device_nonce)
{
    if (pairing_code == NULL || device_id == NULL || client_nonce == NULL ||
        device_nonce == NULL) {
        return failure(AIQA_PAIRING_INVALID_ARGUMENT);
    }
    size_t index = 0;
    while (index < AIQA_WASM_SESSION_SLOTS && s_slots[index].session != NULL) {
        index += 1U;
    }
    if (index == AIQA_WASM_SESSION_SLOTS) {
        return failure(AIQA_PAIRING_INVALID_STATE);
    }
    const uint64_t handshake_id =
        ((uint64_t)handshake_high << 32U) | (uint64_t)handshake_low;
    aiqa_pairing_client_session_t *session = NULL;
    const aiqa_pairing_result_t result = aiqa_pairing_client_session_create(
        &session,
        pairing_code,
        AIQA_PAIRING_CODE_SIZE,
        credential_id,
        handshake_id,
        device_id,
        device_id_length,
        client_nonce,
        device_nonce,
        random_fill,
        NULL);
    if (result != AIQA_PAIRING_OK) {
        return failure(result);
    }
    const uint32_t generation = next_generation();
    s_slots[index] = (aiqa_wasm_slot_t){
        .session = session,
        .generation = generation,
    };
    return (int)((generation << 8U) | (uint32_t)(index + 1U));
}

static int write_round(
    uint32_t handle,
    uint8_t *output,
    size_t output_capacity,
    unsigned round)
{
    aiqa_wasm_slot_t *slot = slot_for(handle);
    if (slot == NULL) {
        return failure(AIQA_PAIRING_INVALID_STATE);
    }
    size_t output_length = 0;
    const aiqa_pairing_result_t result = round == 1U
        ? aiqa_pairing_client_session_write_round_one(
              slot->session, output, output_capacity, &output_length)
        : aiqa_pairing_client_session_write_round_two(
              slot->session, output, output_capacity, &output_length);
    return result == AIQA_PAIRING_OK ? (int)output_length : failure(result);
}

EMSCRIPTEN_KEEPALIVE int aiqa_wasm_write_round_one(
    uint32_t handle,
    uint8_t *output,
    size_t output_capacity)
{
    return write_round(handle, output, output_capacity, 1U);
}

EMSCRIPTEN_KEEPALIVE int aiqa_wasm_read_round_one(
    uint32_t handle,
    const uint8_t *input,
    size_t input_length)
{
    aiqa_wasm_slot_t *slot = slot_for(handle);
    if (slot == NULL) {
        return failure(AIQA_PAIRING_INVALID_STATE);
    }
    const aiqa_pairing_result_t result =
        aiqa_pairing_client_session_read_round_one(
            slot->session, input, input_length);
    return result == AIQA_PAIRING_OK ? 0 : failure(result);
}

EMSCRIPTEN_KEEPALIVE int aiqa_wasm_write_round_two(
    uint32_t handle,
    uint8_t *output,
    size_t output_capacity)
{
    return write_round(handle, output, output_capacity, 2U);
}

EMSCRIPTEN_KEEPALIVE int aiqa_wasm_read_round_two(
    uint32_t handle,
    const uint8_t *input,
    size_t input_length)
{
    aiqa_wasm_slot_t *slot = slot_for(handle);
    if (slot == NULL) {
        return failure(AIQA_PAIRING_INVALID_STATE);
    }
    const aiqa_pairing_result_t result =
        aiqa_pairing_client_session_read_round_two(
            slot->session, input, input_length);
    return result == AIQA_PAIRING_OK ? 0 : failure(result);
}

EMSCRIPTEN_KEEPALIVE int aiqa_wasm_create_finished(
    uint32_t handle,
    uint8_t *output,
    size_t output_capacity)
{
    aiqa_wasm_slot_t *slot = slot_for(handle);
    if (slot == NULL || output == NULL ||
        output_capacity < AIQA_PAIRING_FINISHED_SIZE) {
        return failure(AIQA_PAIRING_INVALID_ARGUMENT);
    }
    const aiqa_pairing_result_t result =
        aiqa_pairing_client_session_create_finished(slot->session, output);
    return result == AIQA_PAIRING_OK ? (int)AIQA_PAIRING_FINISHED_SIZE
                                     : failure(result);
}

EMSCRIPTEN_KEEPALIVE int aiqa_wasm_verify_finished(
    uint32_t handle,
    const uint8_t *input,
    size_t input_length)
{
    aiqa_wasm_slot_t *slot = slot_for(handle);
    if (slot == NULL) {
        return failure(AIQA_PAIRING_INVALID_STATE);
    }
    const aiqa_pairing_result_t result =
        aiqa_pairing_client_session_verify_finished(
            slot->session, input, input_length);
    return result == AIQA_PAIRING_OK ? 0 : failure(result);
}

EMSCRIPTEN_KEEPALIVE int aiqa_wasm_encrypt_request(
    uint32_t handle,
    const uint8_t *plaintext,
    size_t plaintext_length,
    uint8_t *output,
    size_t output_capacity)
{
    aiqa_wasm_slot_t *slot = slot_for(handle);
    if (slot == NULL) {
        return failure(AIQA_PAIRING_INVALID_STATE);
    }
    size_t output_length = 0;
    const aiqa_pairing_result_t result =
        aiqa_pairing_client_session_encrypt_request(
            slot->session,
            plaintext,
            plaintext_length,
            output,
            output_capacity,
            &output_length);
    return result == AIQA_PAIRING_OK ? (int)output_length : failure(result);
}

EMSCRIPTEN_KEEPALIVE int aiqa_wasm_decrypt_response(
    uint32_t handle,
    uint8_t outer_frame_kind,
    const uint8_t *record,
    size_t record_length,
    uint8_t *plaintext,
    size_t plaintext_capacity)
{
    aiqa_wasm_slot_t *slot = slot_for(handle);
    if (slot == NULL) {
        return failure(AIQA_PAIRING_INVALID_STATE);
    }
    size_t plaintext_length = 0;
    const aiqa_pairing_result_t result =
        aiqa_pairing_client_session_decrypt_response(
            slot->session,
            outer_frame_kind,
            record,
            record_length,
            plaintext,
            plaintext_capacity,
            &plaintext_length);
    return result == AIQA_PAIRING_OK ? (int)plaintext_length : failure(result);
}

EMSCRIPTEN_KEEPALIVE void aiqa_wasm_session_destroy(uint32_t handle)
{
    aiqa_wasm_slot_t *slot = slot_for(handle);
    if (slot == NULL) {
        return;
    }
    aiqa_pairing_client_session_destroy(&slot->session);
    mbedtls_platform_zeroize(slot, sizeof(*slot));
}
