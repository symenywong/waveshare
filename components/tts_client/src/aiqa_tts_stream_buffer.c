#include "aiqa_tts_stream_buffer.h"

#include <stdint.h>

bool aiqa_tts_secure_clear_used(void *buffer, size_t capacity, size_t used)
{
    if (used > capacity || (buffer == NULL && used > 0)) {
        return false;
    }

    volatile uint8_t *cursor = (volatile uint8_t *)buffer;
    for (size_t index = 0; index < used; ++index) {
        cursor[index] = 0;
    }
    return true;
}
