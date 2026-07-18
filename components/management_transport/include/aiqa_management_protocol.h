#pragma once

#include "aiqa_management_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_MANAGEMENT_PROTOCOL_MAX_RESPONSE 2048U

typedef struct {
    void *context;
    aiqa_management_result_t (*get_status)(
        void *context,
        const aiqa_management_security_context_t *access,
        aiqa_management_device_status_t *out_status);
    aiqa_management_result_t (*get_public_config)(
        void *context,
        const aiqa_management_security_context_t *access,
        aiqa_management_public_config_t *out_config);
    aiqa_management_result_t (*submit_wifi_update)(
        void *context,
        const aiqa_management_security_context_t *access,
        const aiqa_management_owned_wifi_update_t *update,
        uint32_t *out_operation_id);
} aiqa_management_protocol_ports_t;

/*
 * Handles the deliberately small, unauthenticated protocol surface.
 * A true return value means a response (success or rejection) was produced.
 */
bool aiqa_management_protocol_handle_public_request(
    const uint8_t *payload,
    size_t payload_length,
    char *out_response,
    size_t response_capacity,
    size_t *out_response_length);

/*
 * Handles plaintext that has already passed secure-record authentication.
 * access is owner-minted local state and must never come from the payload.
 */
bool aiqa_management_protocol_handle_authenticated_request(
    const uint8_t *payload,
    size_t payload_length,
    const aiqa_management_security_context_t *access,
    const aiqa_management_protocol_ports_t *ports,
    char *out_response,
    size_t response_capacity,
    size_t *out_response_length);

#ifdef __cplusplus
}
#endif
