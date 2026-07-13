#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_CONVERSATION_MEMORY_MAX_TURNS 3
#define AIQA_CONVERSATION_MEMORY_USER_MAX_LEN 128
#define AIQA_CONVERSATION_MEMORY_PET_MAX_LEN 192
#define AIQA_CONVERSATION_MEMORY_CONTEXT_MAX_LEN 768

typedef struct {
    char user[AIQA_CONVERSATION_MEMORY_USER_MAX_LEN];
    char pet[AIQA_CONVERSATION_MEMORY_PET_MAX_LEN];
} aiqa_conversation_turn_t;

typedef struct {
    aiqa_conversation_turn_t turns[AIQA_CONVERSATION_MEMORY_MAX_TURNS];
    size_t start;
    size_t count;
} aiqa_conversation_memory_t;

void aiqa_conversation_memory_init(aiqa_conversation_memory_t *memory);
void aiqa_conversation_memory_clear(aiqa_conversation_memory_t *memory);
bool aiqa_conversation_memory_add_turn(
    aiqa_conversation_memory_t *memory,
    const char *user,
    const char *pet);
bool aiqa_conversation_memory_build_context(
    const aiqa_conversation_memory_t *memory,
    char *out_context,
    size_t out_context_size);

#ifdef __cplusplus
}
#endif
