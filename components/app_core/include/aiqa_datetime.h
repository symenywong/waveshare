#pragma once

#include "aiqa_local_command.h"

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_DATETIME_TRUSTED_CONTEXT_MAX_LEN 192

bool aiqa_datetime_format_local_reply(
    aiqa_local_command_type_t command_type,
    const struct tm *local_time,
    char *out_reply,
    size_t out_reply_size);

bool aiqa_datetime_format_trusted_context(
    const struct tm *local_time,
    char *out_context,
    size_t out_context_size);

#ifdef __cplusplus
}
#endif
