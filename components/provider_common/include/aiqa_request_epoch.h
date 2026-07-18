#pragma once

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    atomic_uint_least32_t value;
} aiqa_request_epoch_t;

#define AIQA_REQUEST_EPOCH_INITIALIZER \
    { .value = ATOMIC_VAR_INIT(1U) }

uint32_t aiqa_request_epoch_capture(const aiqa_request_epoch_t *epoch);
void aiqa_request_epoch_cancel(aiqa_request_epoch_t *epoch);
bool aiqa_request_epoch_is_current(
    const aiqa_request_epoch_t *epoch,
    uint32_t captured_epoch);

#ifdef __cplusplus
}
#endif
