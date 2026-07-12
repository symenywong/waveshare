#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_DIALOGUE_USER_LINE_MAX_LEN 19
#define AIQA_DIALOGUE_PET_LINE_MAX_LEN 24

typedef enum {
    AIQA_DIALOGUE_EMOTION_NONE = 0,
    AIQA_DIALOGUE_EMOTION_HAPPY,
    AIQA_DIALOGUE_EMOTION_SAD,
    AIQA_DIALOGUE_EMOTION_SHY,
    AIQA_DIALOGUE_EMOTION_FRUSTRATED,
    AIQA_DIALOGUE_EMOTION_BOUNCING,
    AIQA_DIALOGUE_EMOTION_LAUGHING,
    AIQA_DIALOGUE_EMOTION_CRYING,
} aiqa_dialogue_emotion_t;

typedef struct {
    char user_line[AIQA_DIALOGUE_USER_LINE_MAX_LEN];
    char pet_line[AIQA_DIALOGUE_PET_LINE_MAX_LEN];
    aiqa_dialogue_emotion_t pet_emotion;
    bool has_dialogue;
} aiqa_dialogue_view_t;

void aiqa_dialogue_view_clear(aiqa_dialogue_view_t *view);
void aiqa_dialogue_view_set_user(aiqa_dialogue_view_t *view, const char *text);
void aiqa_dialogue_view_set_pet(aiqa_dialogue_view_t *view, const char *text);

#ifdef __cplusplus
}
#endif
