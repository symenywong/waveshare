#pragma once

#include "aiqa_management_access.h"
#include "aiqa_pairing_device_session.h"
#include "aiqa_pairing_lifecycle.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct aiqa_pairing_rpc aiqa_pairing_rpc_t;

aiqa_pairing_result_t aiqa_pairing_rpc_create(
    aiqa_pairing_rpc_t **out_rpc,
    aiqa_pairing_lifecycle_t *lifecycle,
    aiqa_pairing_random_fn random,
    void *random_context,
    const uint8_t *device_id,
    size_t device_id_length);
void aiqa_pairing_rpc_destroy(aiqa_pairing_rpc_t **rpc);

/* Returns false only when the method is not part of the pairing surface. */
bool aiqa_pairing_rpc_handle(
    aiqa_pairing_rpc_t *rpc,
    uint64_t connection_id,
    const uint8_t *payload,
    size_t payload_length,
    char *out_response,
    size_t response_capacity,
    size_t *out_response_length,
    bool *out_requires_tx_confirmation);

/* Called only after the plaintext device Finished response fully drained. */
aiqa_pairing_result_t aiqa_pairing_rpc_commit_finished(
    aiqa_pairing_rpc_t *rpc,
    uint64_t connection_id,
    aiqa_secure_channel_t **out_channel,
    aiqa_management_security_context_t *out_access);

void aiqa_pairing_rpc_abort_connection(
    aiqa_pairing_rpc_t *rpc,
    uint64_t connection_id);

#ifdef __cplusplus
}
#endif
