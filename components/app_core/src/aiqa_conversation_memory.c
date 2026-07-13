#include "aiqa_conversation_memory.h"

#include <string.h>

static size_t utf8_prefix_len(const char *text, size_t max_bytes)
{
    if (text == NULL || max_bytes == 0) {
        return 0;
    }

    size_t offset = 0;
    while (text[offset] != '\0' && offset < max_bytes) {
        const unsigned char first = (unsigned char)text[offset];
        size_t char_len = 1;
        if ((first & 0x80u) == 0) {
            char_len = 1;
        } else if ((first & 0xE0u) == 0xC0u) {
            char_len = 2;
        } else if ((first & 0xF0u) == 0xE0u) {
            char_len = 3;
        } else if ((first & 0xF8u) == 0xF0u) {
            char_len = 4;
        }

        if (offset + char_len > max_bytes) {
            break;
        }

        bool valid = true;
        for (size_t index = 1; index < char_len; ++index) {
            const unsigned char follow = (unsigned char)text[offset + index];
            if (follow == '\0' || (follow & 0xC0u) != 0x80u) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            char_len = 1;
        }
        offset += char_len;
    }
    return offset;
}

static void copy_turn_text(char *out, size_t out_size, const char *text)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (text == NULL || text[0] == '\0') {
        return;
    }

    const size_t copy_len = utf8_prefix_len(text, out_size - 1);
    if (copy_len > 0) {
        (void)memcpy(out, text, copy_len);
    }
    out[copy_len] = '\0';
}

static bool append_text(char *out, size_t out_size, size_t *pos, const char *text)
{
    if (out == NULL || out_size == 0 || pos == NULL || text == NULL) {
        return false;
    }
    const size_t text_len = strlen(text);
    if (*pos + text_len >= out_size) {
        return false;
    }
    (void)memcpy(out + *pos, text, text_len);
    *pos += text_len;
    out[*pos] = '\0';
    return true;
}

void aiqa_conversation_memory_init(aiqa_conversation_memory_t *memory)
{
    aiqa_conversation_memory_clear(memory);
}

void aiqa_conversation_memory_clear(aiqa_conversation_memory_t *memory)
{
    if (memory == NULL) {
        return;
    }
    (void)memset(memory, 0, sizeof(*memory));
}

bool aiqa_conversation_memory_add_turn(
    aiqa_conversation_memory_t *memory,
    const char *user,
    const char *pet)
{
    if (memory == NULL || user == NULL || pet == NULL || user[0] == '\0' || pet[0] == '\0') {
        return false;
    }

    size_t slot = 0;
    if (memory->count < AIQA_CONVERSATION_MEMORY_MAX_TURNS) {
        slot = (memory->start + memory->count) % AIQA_CONVERSATION_MEMORY_MAX_TURNS;
        memory->count += 1;
    } else {
        slot = memory->start;
        memory->start = (memory->start + 1) % AIQA_CONVERSATION_MEMORY_MAX_TURNS;
    }

    copy_turn_text(memory->turns[slot].user, sizeof(memory->turns[slot].user), user);
    copy_turn_text(memory->turns[slot].pet, sizeof(memory->turns[slot].pet), pet);
    return memory->turns[slot].user[0] != '\0' && memory->turns[slot].pet[0] != '\0';
}

bool aiqa_conversation_memory_build_context(
    const aiqa_conversation_memory_t *memory,
    char *out_context,
    size_t out_context_size)
{
    if (out_context == NULL || out_context_size == 0) {
        return false;
    }
    out_context[0] = '\0';
    if (memory == NULL || memory->count == 0) {
        return false;
    }

    size_t pos = 0;
    for (size_t offset = 0; offset < memory->count; ++offset) {
        const size_t index = (memory->start + offset) % AIQA_CONVERSATION_MEMORY_MAX_TURNS;
        const aiqa_conversation_turn_t *turn = &memory->turns[index];
        if (turn->user[0] == '\0' || turn->pet[0] == '\0') {
            continue;
        }
        if (!append_text(out_context, out_context_size, &pos, "User: ") ||
            !append_text(out_context, out_context_size, &pos, turn->user) ||
            !append_text(out_context, out_context_size, &pos, "\nPet: ") ||
            !append_text(out_context, out_context_size, &pos, turn->pet) ||
            !append_text(out_context, out_context_size, &pos, "\n")) {
            return pos > 0;
        }
    }
    return pos > 0;
}
