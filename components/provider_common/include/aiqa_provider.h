#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_PROVIDER_DASHSCOPE_CHAT "dashscope_openai_chat"
#define AIQA_PROVIDER_MINIMAX_CHAT "minimax_openai_chat"
#define AIQA_PROVIDER_DASHSCOPE_ASR_FLASH "dashscope_qwen_asr_flash"
#define AIQA_DEFAULT_QWEN_MODEL "qwen3.7-max"
#define AIQA_DEFAULT_MINIMAX_MODEL "MiniMax-M3"

typedef enum {
    AIQA_AUDIO_SOURCE_MEMORY_STREAM = 0,
    AIQA_AUDIO_SOURCE_DATA_URI,
    AIQA_AUDIO_SOURCE_PUBLIC_URL,
} aiqa_audio_source_kind_t;

typedef struct {
    bool supports_chat_stream;
    bool supports_reasoning_controls;
    bool supports_data_uri_audio;
    bool requires_public_audio_url;
    bool async_transcription;
    size_t max_audio_bytes;
    uint32_t max_audio_seconds;
} aiqa_provider_caps_t;

typedef struct {
    const char *provider_id;
    const char *base_url;
    const char *model;
    const char *api_key_ref;
    uint32_t timeout_ms;
} aiqa_provider_config_t;

typedef struct {
    bool stream;
    bool hide_reasoning;
    int max_completion_tokens;
} aiqa_chat_options_t;

typedef struct {
    int http_status;
    const char *provider_code;
    const char *message;
    const char *request_id;
    bool retryable;
} aiqa_provider_error_t;

typedef void (*aiqa_chat_event_cb_t)(const char *delta, void *user_ctx);

const aiqa_provider_caps_t *aiqa_provider_caps_for(const char *provider_id);
bool aiqa_provider_is_known(const char *provider_id);
bool aiqa_provider_host_allowed(const char *provider_id, const char *host);
bool aiqa_provider_model_allowed(const char *provider_id, const char *model);

#ifdef __cplusplus
}
#endif
