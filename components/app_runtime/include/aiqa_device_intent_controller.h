#pragma once

#include "aiqa_assistant_profile.h"
#include "aiqa_device_intent.h"
#include "aiqa_language.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_DEVICE_INTENT_PENDING_TTL_US 30000000LL
#define AIQA_DEVICE_INTENT_REPLY_MAX_LEN 160

typedef struct {
    void *context;
    bool (*load_profile)(void *context, aiqa_assistant_profile_t *out_profile);
    bool (*load_language)(void *context, aiqa_dialogue_language_t *out_language);
    esp_err_t (*save_profile)(void *context, const aiqa_assistant_profile_t *profile);
    void (*publish_profile)(void *context, const aiqa_assistant_profile_t *profile);
    esp_err_t (*save_language)(void *context, aiqa_dialogue_language_t language);
    void (*publish_language)(void *context, aiqa_dialogue_language_t language);
    bool (*authorize_generation)(void *context, uint32_t generation);
} aiqa_device_intent_ports_t;

typedef struct {
    bool active;
    uint32_t serial;
    uint32_t proposal_generation;
    uint32_t confirmation_generation;
    int64_t expires_at_us;
    aiqa_device_intent_t intent;
} aiqa_device_intent_pending_t;

typedef struct {
    uint32_t next_serial;
    aiqa_device_intent_pending_t pending;
} aiqa_device_intent_controller_t;

void aiqa_device_intent_controller_init(aiqa_device_intent_controller_t *controller);
void aiqa_device_intent_controller_clear(aiqa_device_intent_controller_t *controller);
bool aiqa_device_intent_controller_has_pending(
    const aiqa_device_intent_controller_t *controller);

bool aiqa_device_intent_controller_handle_cloud_intent(
    aiqa_device_intent_controller_t *controller,
    const aiqa_device_intent_t *intent,
    const char *transcript,
    uint32_t generation,
    int64_t now_us,
    const aiqa_device_intent_ports_t *ports,
    char *out_reply,
    size_t reply_capacity);

bool aiqa_device_intent_controller_handle_pending_transcript(
    aiqa_device_intent_controller_t *controller,
    const char *transcript,
    uint32_t generation,
    int64_t now_us,
    const aiqa_device_intent_ports_t *ports,
    char *out_reply,
    size_t reply_capacity);

#ifdef __cplusplus
}
#endif
