#include "aiqa_config.h"

#include "aiqa_provider.h"

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

static bool has_space(const char *value)
{
    if (value == NULL) {
        return true;
    }
    for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        if (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') {
            return true;
        }
    }
    return false;
}

static bool extract_https_host(const char *base_url, char *host, size_t host_size)
{
    const char *prefix = "https://";
    const size_t prefix_len = strlen(prefix);
    if (base_url == NULL || host == NULL || host_size == 0 || strncmp(base_url, prefix, prefix_len) != 0) {
        return false;
    }

    const char *host_start = base_url + prefix_len;
    if (*host_start == '\0' || *host_start == '/') {
        return false;
    }

    size_t host_len = 0;
    while (host_start[host_len] != '\0' &&
           host_start[host_len] != '/' &&
           host_start[host_len] != ':') {
        if (host_start[host_len] == ' ' || host_start[host_len] == '\t') {
            return false;
        }
        ++host_len;
    }
    if (host_len == 0 || host_len >= host_size) {
        return false;
    }

    (void)memcpy(host, host_start, host_len);
    host[host_len] = '\0';
    return true;
}

aiqa_config_t aiqa_config_default(void)
{
    aiqa_config_t config = {
        .config_version = AIQA_CONFIG_VERSION,
        .stream = true,
        .hide_reasoning = true,
        .max_completion_tokens = 768,
    };
    (void)snprintf(config.active_provider, sizeof(config.active_provider), "%s", AIQA_PROVIDER_DASHSCOPE_CHAT);
    (void)snprintf(config.model, sizeof(config.model), "%s", AIQA_DEFAULT_QWEN_MODEL);
    (void)snprintf(config.base_url, sizeof(config.base_url), "%s", "https://dashscope.aliyuncs.com/compatible-mode/v1");
    return config;
}

aiqa_config_status_t aiqa_config_validate(const aiqa_config_t *config)
{
    if (config == NULL || config->config_version != AIQA_CONFIG_VERSION) {
        return AIQA_CONFIG_ERR_VERSION;
    }
    if (!aiqa_provider_is_known(config->active_provider)) {
        return AIQA_CONFIG_ERR_PROVIDER;
    }
    if (config->model[0] == '\0' || has_space(config->model)) {
        return AIQA_CONFIG_ERR_MODEL;
    }
    if (!aiqa_provider_model_allowed(config->active_provider, config->model)) {
        return AIQA_CONFIG_ERR_MODEL;
    }

    char host[96];
    if (!extract_https_host(config->base_url, host, sizeof(host)) ||
        !aiqa_provider_host_allowed(config->active_provider, host)) {
        return AIQA_CONFIG_ERR_BASE_URL;
    }
    return AIQA_CONFIG_OK;
}

const char *aiqa_config_status_name(aiqa_config_status_t status)
{
    switch (status) {
    case AIQA_CONFIG_OK:
        return "OK";
    case AIQA_CONFIG_ERR_VERSION:
        return "VERSION";
    case AIQA_CONFIG_ERR_PROVIDER:
        return "PROVIDER";
    case AIQA_CONFIG_ERR_MODEL:
        return "MODEL";
    case AIQA_CONFIG_ERR_BASE_URL:
        return "BASE_URL";
    default:
        return "UNKNOWN";
    }
}

aiqa_secret_config_t aiqa_secret_config_empty(void)
{
    aiqa_secret_config_t secrets = {0};
    return secrets;
}

aiqa_secret_status_t aiqa_secret_config_validate(const aiqa_secret_config_t *secrets)
{
    if (secrets == NULL || secrets->wifi_ssid[0] == '\0') {
        return AIQA_SECRET_ERR_WIFI_SSID;
    }

    size_t ssid_len = bounded_strlen(secrets->wifi_ssid, sizeof(secrets->wifi_ssid));
    if (ssid_len == 0 || ssid_len >= sizeof(secrets->wifi_ssid)) {
        return AIQA_SECRET_ERR_WIFI_SSID;
    }

    size_t password_len = bounded_strlen(secrets->wifi_password, sizeof(secrets->wifi_password));
    if (password_len >= sizeof(secrets->wifi_password) ||
        (password_len > 0 && password_len < 8) ||
        password_len > 63) {
        return AIQA_SECRET_ERR_WIFI_PASSWORD;
    }

    size_t chat_key_len = bounded_strlen(secrets->chat_api_key, sizeof(secrets->chat_api_key));
    if (chat_key_len < 6 || chat_key_len >= sizeof(secrets->chat_api_key)) {
        return AIQA_SECRET_ERR_CHAT_API_KEY;
    }

    return AIQA_SECRET_OK;
}

const char *aiqa_secret_status_name(aiqa_secret_status_t status)
{
    switch (status) {
    case AIQA_SECRET_OK:
        return "OK";
    case AIQA_SECRET_ERR_WIFI_SSID:
        return "WIFI_SSID";
    case AIQA_SECRET_ERR_WIFI_PASSWORD:
        return "WIFI_PASSWORD";
    case AIQA_SECRET_ERR_CHAT_API_KEY:
        return "CHAT_API_KEY";
    default:
        return "UNKNOWN";
    }
}
