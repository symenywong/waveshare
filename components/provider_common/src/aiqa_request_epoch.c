#include "aiqa_request_epoch.h"

#include <stddef.h>

uint32_t aiqa_request_epoch_capture(const aiqa_request_epoch_t *epoch)
{
    if (epoch == NULL) {
        return 0;
    }
    return atomic_load_explicit(&epoch->value, memory_order_acquire);
}

void aiqa_request_epoch_cancel(aiqa_request_epoch_t *epoch)
{
    if (epoch == NULL) {
        return;
    }
    uint32_t next =
        atomic_fetch_add_explicit(&epoch->value, 1U, memory_order_acq_rel) + 1U;
    if (next == 0U) {
        atomic_store_explicit(&epoch->value, 1U, memory_order_release);
    }
}

bool aiqa_request_epoch_is_current(
    const aiqa_request_epoch_t *epoch,
    uint32_t captured_epoch)
{
    return captured_epoch != 0U &&
           captured_epoch == aiqa_request_epoch_capture(epoch);
}
