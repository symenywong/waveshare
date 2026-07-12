#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_DIALOGUE_USER_LINE_MAX_LEN 19
#define AIQA_DIALOGUE_PET_LINE_MAX_LEN 24

typedef struct {
    char user_line[AIQA_DIALOGUE_USER_LINE_MAX_LEN];
    char pet_line[AIQA_DIALOGUE_PET_LINE_MAX_LEN];
    bool has_dialogue;
} aiqa_dialogue_view_t;

void aiqa_dialogue_view_clear(aiqa_dialogue_view_t *view);
void aiqa_dialogue_view_set_user(aiqa_dialogue_view_t *view, const char *text);
void aiqa_dialogue_view_set_pet(aiqa_dialogue_view_t *view, const char *text);

#ifdef __cplusplus
}
#endif
