#include "aiqa_provider.h"

#include <stdbool.h>
#include <string.h>

static const aiqa_provider_caps_t DASH_SCOPE_CHAT_CAPS = {
    .supports_chat_stream = true,
    .supports_reasoning_controls = true,
    .supports_data_uri_audio = false,
    .requires_public_audio_url = false,
    .async_transcription = false,
    .max_audio_bytes = 0,
    .max_audio_seconds = 0,
};

static const aiqa_provider_caps_t MINIMAX_CHAT_CAPS = {
    .supports_chat_stream = true,
    .supports_reasoning_controls = true,
    .supports_data_uri_audio = false,
    .requires_public_audio_url = false,
    .async_transcription = false,
    .max_audio_bytes = 0,
    .max_audio_seconds = 0,
};

static const aiqa_provider_caps_t DASH_SCOPE_ASR_FLASH_CAPS = {
    .supports_chat_stream = false,
    .supports_reasoning_controls = false,
    .supports_data_uri_audio = true,
    .requires_public_audio_url = false,
    .async_transcription = false,
    .max_audio_bytes = 10 * 1024 * 1024,
    .max_audio_seconds = 5 * 60,
};

const aiqa_provider_caps_t *aiqa_provider_caps_for(const char *provider_id)
{
    if (provider_id == NULL) {
        return NULL;
    }

    if (strcmp(provider_id, AIQA_PROVIDER_DASHSCOPE_CHAT) == 0) {
        return &DASH_SCOPE_CHAT_CAPS;
    }
    if (strcmp(provider_id, AIQA_PROVIDER_MINIMAX_CHAT) == 0) {
        return &MINIMAX_CHAT_CAPS;
    }
    if (strcmp(provider_id, AIQA_PROVIDER_DASHSCOPE_ASR_FLASH) == 0) {
        return &DASH_SCOPE_ASR_FLASH_CAPS;
    }

    return NULL;
}

bool aiqa_provider_is_known(const char *provider_id)
{
    return aiqa_provider_caps_for(provider_id) != NULL;
}

static bool matches_any(const char *value, const char *const *allowed, size_t allowed_count)
{
    if (value == NULL) {
        return false;
    }
    for (size_t index = 0; index < allowed_count; ++index) {
        if (strcmp(value, allowed[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool ends_with(const char *value, const char *suffix)
{
    if (value == NULL || suffix == NULL) {
        return false;
    }
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    return value_len >= suffix_len && strcmp(value + value_len - suffix_len, suffix) == 0;
}

bool aiqa_provider_host_allowed(const char *provider_id, const char *host)
{
    static const char *const dashscope_hosts[] = {
        "dashscope.aliyuncs.com",
        "dashscope-intl.aliyuncs.com",
        "dashscope-us.aliyuncs.com",
        "cn-hongkong.dashscope.aliyuncs.com",
    };
    static const char *const minimax_hosts[] = {
        "api.minimax.io",
        "api.minimax.chat",
    };

    if (provider_id == NULL || host == NULL) {
        return false;
    }

    if (strcmp(provider_id, AIQA_PROVIDER_DASHSCOPE_CHAT) == 0 ||
        strcmp(provider_id, AIQA_PROVIDER_DASHSCOPE_ASR_FLASH) == 0) {
        return matches_any(host, dashscope_hosts, sizeof(dashscope_hosts) / sizeof(dashscope_hosts[0])) ||
               ends_with(host, ".maas.aliyuncs.com");
    }

    if (strcmp(provider_id, AIQA_PROVIDER_MINIMAX_CHAT) == 0) {
        return matches_any(host, minimax_hosts, sizeof(minimax_hosts) / sizeof(minimax_hosts[0]));
    }

    return false;
}

bool aiqa_provider_model_allowed(const char *provider_id, const char *model)
{
    static const char *const dashscope_chat_models[] = {
        AIQA_DEFAULT_QWEN_MODEL,
        "qwen-plus",
        "qwen-turbo",
    };
    static const char *const minimax_chat_models[] = {
        AIQA_DEFAULT_MINIMAX_MODEL,
    };
    static const char *const dashscope_asr_models[] = {
        "qwen3-asr-flash",
    };

    if (provider_id == NULL || model == NULL) {
        return false;
    }

    if (strcmp(provider_id, AIQA_PROVIDER_DASHSCOPE_CHAT) == 0) {
        return matches_any(model, dashscope_chat_models,
                           sizeof(dashscope_chat_models) / sizeof(dashscope_chat_models[0]));
    }
    if (strcmp(provider_id, AIQA_PROVIDER_MINIMAX_CHAT) == 0) {
        return matches_any(model, minimax_chat_models,
                           sizeof(minimax_chat_models) / sizeof(minimax_chat_models[0]));
    }
    if (strcmp(provider_id, AIQA_PROVIDER_DASHSCOPE_ASR_FLASH) == 0) {
        return matches_any(model, dashscope_asr_models,
                           sizeof(dashscope_asr_models) / sizeof(dashscope_asr_models[0]));
    }

    return false;
}
