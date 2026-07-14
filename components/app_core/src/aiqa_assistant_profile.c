#include "aiqa_assistant_profile.h"

#include <stdio.h>
#include <string.h>

static size_t bounded_strlen(const char *value, size_t max_len)
{
    if (value == NULL) {
        return 0;
    }
    size_t len = 0;
    while (len < max_len && value[len] != '\0') {
        ++len;
    }
    return len;
}

aiqa_assistant_profile_t aiqa_assistant_profile_default(void)
{
    aiqa_assistant_profile_t profile = {
        .gender = AIQA_ASSISTANT_GENDER_NEUTRAL,
    };
    (void)snprintf(profile.name, sizeof(profile.name), "%s", "AIQA");
    return profile;
}

bool aiqa_assistant_profile_is_valid(const aiqa_assistant_profile_t *profile)
{
    if (profile == NULL ||
        profile->gender < AIQA_ASSISTANT_GENDER_NEUTRAL ||
        profile->gender > AIQA_ASSISTANT_GENDER_MALE) {
        return false;
    }
    const size_t name_len = bounded_strlen(profile->name, sizeof(profile->name));
    return name_len > 0 && name_len < sizeof(profile->name);
}

bool aiqa_assistant_profile_set_name(aiqa_assistant_profile_t *profile, const char *name)
{
    if (profile == NULL || name == NULL || name[0] == '\0') {
        return false;
    }
    const size_t name_len = bounded_strlen(name, AIQA_ASSISTANT_NAME_MAX_LEN);
    if (name_len == 0 || name_len >= AIQA_ASSISTANT_NAME_MAX_LEN || name[name_len] != '\0') {
        return false;
    }
    (void)memcpy(profile->name, name, name_len);
    profile->name[name_len] = '\0';
    return aiqa_assistant_profile_is_valid(profile);
}

bool aiqa_assistant_profile_set_gender(
    aiqa_assistant_profile_t *profile,
    aiqa_assistant_gender_t gender)
{
    if (profile == NULL ||
        gender < AIQA_ASSISTANT_GENDER_NEUTRAL ||
        gender > AIQA_ASSISTANT_GENDER_MALE) {
        return false;
    }
    profile->gender = gender;
    return aiqa_assistant_profile_is_valid(profile);
}

const char *aiqa_assistant_gender_name(aiqa_assistant_gender_t gender)
{
    switch (gender) {
    case AIQA_ASSISTANT_GENDER_FEMALE:
        return "female";
    case AIQA_ASSISTANT_GENDER_MALE:
        return "male";
    case AIQA_ASSISTANT_GENDER_NEUTRAL:
    default:
        return "neutral";
    }
}

aiqa_assistant_gender_t aiqa_assistant_gender_from_name(const char *name)
{
    if (name == NULL) {
        return AIQA_ASSISTANT_GENDER_NEUTRAL;
    }
    if (strcmp(name, "female") == 0) {
        return AIQA_ASSISTANT_GENDER_FEMALE;
    }
    if (strcmp(name, "male") == 0) {
        return AIQA_ASSISTANT_GENDER_MALE;
    }
    return AIQA_ASSISTANT_GENDER_NEUTRAL;
}

bool aiqa_assistant_profile_build_context(
    const aiqa_assistant_profile_t *profile,
    char *out_context,
    size_t out_context_size)
{
    if (out_context == NULL || out_context_size == 0) {
        return false;
    }
    out_context[0] = '\0';
    if (!aiqa_assistant_profile_is_valid(profile)) {
        return false;
    }
    const int written = snprintf(out_context,
                                 out_context_size,
                                 "Assistant profile: name=%s, gender=%s.",
                                 profile->name,
                                 aiqa_assistant_gender_name(profile->gender));
    if (written < 0 || (size_t)written >= out_context_size) {
        out_context[0] = '\0';
        return false;
    }
    return true;
}
