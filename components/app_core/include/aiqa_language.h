#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AIQA_DIALOGUE_LANGUAGE_ENGLISH = 0,
    AIQA_DIALOGUE_LANGUAGE_CHINESE,
} aiqa_dialogue_language_t;

aiqa_dialogue_language_t aiqa_language_default(void);
bool aiqa_language_is_valid(aiqa_dialogue_language_t language);
bool aiqa_language_detect_switch_command(const char *text, aiqa_dialogue_language_t *out_language);
const char *aiqa_language_confirmation(aiqa_dialogue_language_t language);
const char *aiqa_language_display_label(aiqa_dialogue_language_t language);
const char *aiqa_language_chat_code(aiqa_dialogue_language_t language);
bool aiqa_language_from_chat_code(
    const char *language_code,
    aiqa_dialogue_language_t *out_language);
const char *aiqa_language_name(aiqa_dialogue_language_t language);

#ifdef __cplusplus
}
#endif
