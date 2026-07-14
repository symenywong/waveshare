#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_ASSISTANT_NAME_MAX_LEN 24
#define AIQA_ASSISTANT_PROFILE_CONTEXT_MAX_LEN 192

typedef enum {
    AIQA_ASSISTANT_GENDER_NEUTRAL = 0,
    AIQA_ASSISTANT_GENDER_FEMALE,
    AIQA_ASSISTANT_GENDER_MALE,
} aiqa_assistant_gender_t;

typedef struct {
    char name[AIQA_ASSISTANT_NAME_MAX_LEN];
    aiqa_assistant_gender_t gender;
} aiqa_assistant_profile_t;

aiqa_assistant_profile_t aiqa_assistant_profile_default(void);
bool aiqa_assistant_profile_is_valid(const aiqa_assistant_profile_t *profile);
bool aiqa_assistant_profile_set_name(aiqa_assistant_profile_t *profile, const char *name);
bool aiqa_assistant_profile_set_gender(
    aiqa_assistant_profile_t *profile,
    aiqa_assistant_gender_t gender);
const char *aiqa_assistant_gender_name(aiqa_assistant_gender_t gender);
aiqa_assistant_gender_t aiqa_assistant_gender_from_name(const char *name);
bool aiqa_assistant_profile_build_context(
    const aiqa_assistant_profile_t *profile,
    char *out_context,
    size_t out_context_size);

#ifdef __cplusplus
}
#endif
