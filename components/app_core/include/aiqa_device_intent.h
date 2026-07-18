#pragma once

#include "aiqa_assistant_profile.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_DEVICE_INTENT_VALUE_MAX_LEN AIQA_ASSISTANT_NAME_MAX_LEN
#define AIQA_DEVICE_INTENT_EVIDENCE_MAX_LEN 96

typedef enum {
    AIQA_DEVICE_INTENT_INVALID = 0,
    AIQA_DEVICE_INTENT_NONE,
    AIQA_DEVICE_INTENT_SET_NAME,
    AIQA_DEVICE_INTENT_SET_GENDER,
    AIQA_DEVICE_INTENT_SET_LANGUAGE,
    AIQA_DEVICE_INTENT_GET_PROFILE_NAME,
    AIQA_DEVICE_INTENT_GET_PROFILE_GENDER,
    AIQA_DEVICE_INTENT_GET_PROFILE_LANGUAGE,
} aiqa_device_intent_type_t;

typedef struct {
    aiqa_device_intent_type_t type;
    char value[AIQA_DEVICE_INTENT_VALUE_MAX_LEN];
    char evidence[AIQA_DEVICE_INTENT_EVIDENCE_MAX_LEN];
} aiqa_device_intent_t;

bool aiqa_device_intent_is_setting(aiqa_device_intent_type_t type);
bool aiqa_device_intent_is_valid(
    const aiqa_device_intent_t *intent,
    const char *transcript);
void aiqa_device_intent_clear(aiqa_device_intent_t *intent);

#ifdef __cplusplus
}
#endif
