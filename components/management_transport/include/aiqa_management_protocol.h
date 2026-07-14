#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_MANAGEMENT_PROTOCOL_MAX_RESPONSE 512U

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

#ifdef __cplusplus
}
#endif
