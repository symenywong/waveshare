#pragma once

#include "aiqa_assistant_profile.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_LOCAL_COMMAND_TEXT_MAX_LEN AIQA_ASSISTANT_NAME_MAX_LEN

typedef enum {
    AIQA_LOCAL_COMMAND_NONE = 0,
    AIQA_LOCAL_COMMAND_VOLUME_SET,
    AIQA_LOCAL_COMMAND_VOLUME_RELATIVE,
    AIQA_LOCAL_COMMAND_VOLUME_QUERY,
    AIQA_LOCAL_COMMAND_BATTERY_QUERY,
    AIQA_LOCAL_COMMAND_DATE_QUERY,
    AIQA_LOCAL_COMMAND_TIME_QUERY,
    AIQA_LOCAL_COMMAND_WEEKDAY_QUERY,
    AIQA_LOCAL_COMMAND_DATETIME_QUERY,
    AIQA_LOCAL_COMMAND_SET_NAME,
    AIQA_LOCAL_COMMAND_SET_GENDER,
} aiqa_local_command_type_t;

typedef struct {
    aiqa_local_command_type_t type;
    int value;
    aiqa_assistant_gender_t gender;
    char text[AIQA_LOCAL_COMMAND_TEXT_MAX_LEN];
} aiqa_local_command_t;

bool aiqa_local_command_parse(const char *transcript, aiqa_local_command_t *out_command);
uint8_t aiqa_local_command_clamp_volume(int volume_percent);

#ifdef __cplusplus
}
#endif
