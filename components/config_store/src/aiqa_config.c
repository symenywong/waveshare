#include "aiqa_config.h"

#include "aiqa_provider.h"

#include <stdio.h>
#include <string.h>

static void secure_zero(void *value, size_t value_size);

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
    (void)snprintf(config.asr_provider, sizeof(config.asr_provider), "%s", AIQA_PROVIDER_DASHSCOPE_ASR_FLASH);
    (void)snprintf(config.asr_model, sizeof(config.asr_model), "%s", AIQA_DEFAULT_QWEN_ASR_MODEL);
    (void)snprintf(config.asr_base_url, sizeof(config.asr_base_url), "%s",
                   "https://dashscope.aliyuncs.com/compatible-mode/v1");
    (void)snprintf(config.tts_provider, sizeof(config.tts_provider), "%s", AIQA_PROVIDER_DASHSCOPE_TTS);
    (void)snprintf(config.tts_model, sizeof(config.tts_model), "%s", AIQA_DEFAULT_QWEN_TTS_MODEL);
    (void)snprintf(config.tts_base_url, sizeof(config.tts_base_url), "%s", "https://dashscope.aliyuncs.com/api/v1");
    (void)snprintf(config.tts_voice, sizeof(config.tts_voice), "%s", AIQA_DEFAULT_QWEN_TTS_VOICE);
    return config;
}

static aiqa_config_status_t validate_provider_endpoint(
    const char *provider,
    const char *model,
    const char *base_url,
    bool require_chat,
    bool require_audio,
    bool require_tts)
{
    const aiqa_provider_caps_t *caps = aiqa_provider_caps_for(provider);
    if (caps == NULL) {
        return AIQA_CONFIG_ERR_PROVIDER;
    }
    if ((require_chat && !caps->supports_chat_stream) ||
        (require_audio && caps->max_audio_bytes == 0 && !caps->supports_data_uri_audio &&
         !caps->requires_public_audio_url) ||
        (require_tts && !caps->supports_tts_stream)) {
        return AIQA_CONFIG_ERR_PROVIDER;
    }
    if (model == NULL || model[0] == '\0' || has_space(model)) {
        return AIQA_CONFIG_ERR_MODEL;
    }
    if (!aiqa_provider_model_allowed(provider, model)) {
        return AIQA_CONFIG_ERR_MODEL;
    }

    char host[96];
    if (!extract_https_host(base_url, host, sizeof(host)) ||
        !aiqa_provider_host_allowed(provider, host)) {
        return AIQA_CONFIG_ERR_BASE_URL;
    }
    return AIQA_CONFIG_OK;
}

aiqa_config_status_t aiqa_config_validate(const aiqa_config_t *config)
{
    if (config == NULL || config->config_version != AIQA_CONFIG_VERSION) {
        return AIQA_CONFIG_ERR_VERSION;
    }

    aiqa_config_status_t status = validate_provider_endpoint(
        config->active_provider,
        config->model,
        config->base_url,
        true,
        false,
        false);
    if (status != AIQA_CONFIG_OK) {
        return status;
    }

    status = validate_provider_endpoint(
        config->asr_provider,
        config->asr_model,
        config->asr_base_url,
        false,
        true,
        false);
    if (status != AIQA_CONFIG_OK) {
        return status;
    }

    if (config->tts_voice[0] == '\0' ||
        bounded_strlen(config->tts_voice, sizeof(config->tts_voice)) >= sizeof(config->tts_voice) ||
        has_space(config->tts_voice)) {
        return AIQA_CONFIG_ERR_MODEL;
    }

    return validate_provider_endpoint(
        config->tts_provider,
        config->tts_model,
        config->tts_base_url,
        false,
        false,
        true);
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

aiqa_secret_status_t aiqa_wifi_credentials_validate(const aiqa_wifi_credentials_t *credentials)
{
    if (credentials == NULL || credentials->ssid[0] == '\0') {
        return AIQA_SECRET_ERR_WIFI_SSID;
    }

    size_t ssid_len = bounded_strlen(credentials->ssid, sizeof(credentials->ssid));
    if (ssid_len == 0 || ssid_len >= sizeof(credentials->ssid)) {
        return AIQA_SECRET_ERR_WIFI_SSID;
    }

    size_t password_len = bounded_strlen(credentials->password, sizeof(credentials->password));
    if (password_len >= sizeof(credentials->password) ||
        (password_len > 0 && password_len < 8) ||
        password_len > 63) {
        return AIQA_SECRET_ERR_WIFI_PASSWORD;
    }

    return AIQA_SECRET_OK;
}

void aiqa_wifi_credentials_secure_clear(aiqa_wifi_credentials_t *credentials)
{
    if (credentials != NULL) {
        secure_zero(credentials, sizeof(*credentials));
    }
}

bool aiqa_wifi_credentials_from_secrets(
    const aiqa_secret_config_t *secrets,
    aiqa_wifi_credentials_t *credentials)
{
    if (secrets == NULL || credentials == NULL) {
        return false;
    }
    aiqa_wifi_credentials_t projected = {0};
    (void)memcpy(projected.ssid, secrets->wifi_ssid, sizeof(projected.ssid));
    (void)memcpy(projected.password, secrets->wifi_password, sizeof(projected.password));
    if (aiqa_wifi_credentials_validate(&projected) != AIQA_SECRET_OK) {
        aiqa_wifi_credentials_secure_clear(&projected);
        aiqa_wifi_credentials_secure_clear(credentials);
        return false;
    }
    *credentials = projected;
    aiqa_wifi_credentials_secure_clear(&projected);
    return true;
}

aiqa_secret_status_t aiqa_wifi_secret_config_validate(const aiqa_secret_config_t *secrets)
{
    if (secrets == NULL) {
        return AIQA_SECRET_ERR_WIFI_SSID;
    }
    aiqa_wifi_credentials_t credentials = {0};
    (void)memcpy(credentials.ssid, secrets->wifi_ssid, sizeof(credentials.ssid));
    (void)memcpy(credentials.password, secrets->wifi_password, sizeof(credentials.password));
    const aiqa_secret_status_t status = aiqa_wifi_credentials_validate(&credentials);
    aiqa_wifi_credentials_secure_clear(&credentials);
    return status;
}

aiqa_secret_status_t aiqa_secret_config_validate(const aiqa_secret_config_t *secrets)
{
    aiqa_secret_status_t wifi_status = aiqa_wifi_secret_config_validate(secrets);
    if (wifi_status != AIQA_SECRET_OK) {
        return wifi_status;
    }

    size_t chat_key_len = bounded_strlen(secrets->chat_api_key, sizeof(secrets->chat_api_key));
    if (chat_key_len < 6 || chat_key_len >= sizeof(secrets->chat_api_key)) {
        return AIQA_SECRET_ERR_CHAT_API_KEY;
    }

    const size_t asr_key_len =
        bounded_strlen(secrets->asr_api_key, sizeof(secrets->asr_api_key));
    if (secrets->asr_api_key[0] != '\0' &&
        (asr_key_len < 6 || asr_key_len >= sizeof(secrets->asr_api_key))) {
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

static bool wifi_ssid_is_valid(const char *ssid)
{
    const size_t ssid_len = bounded_strlen(ssid, AIQA_MAX_WIFI_SSID_LEN);
    return ssid_len > 0 && ssid_len < AIQA_MAX_WIFI_SSID_LEN;
}

static bool wifi_password_is_valid(const char *password)
{
    const size_t password_len = bounded_strlen(password, AIQA_MAX_WIFI_PASSWORD_LEN);
    return password_len >= 8 && password_len <= 63;
}

static void secure_zero(void *value, size_t value_size)
{
    volatile unsigned char *bytes = (volatile unsigned char *)value;
    while (value_size > 0) {
        *bytes++ = 0;
        --value_size;
    }
}

aiqa_wifi_update_status_t aiqa_config_prepare_wifi_update(
    const aiqa_secret_config_t *current,
    uint32_t current_revision,
    const aiqa_wifi_update_t *request,
    aiqa_secret_config_t *updated,
    uint32_t *next_revision)
{
    if (current == NULL || request == NULL || updated == NULL || next_revision == NULL) {
        return AIQA_WIFI_UPDATE_ERR_INVALID_ARGUMENT;
    }
    if (request->base_revision != current_revision) {
        return AIQA_WIFI_UPDATE_ERR_REVISION_CONFLICT;
    }
    if (current_revision == UINT32_MAX) {
        return AIQA_WIFI_UPDATE_ERR_REVISION_EXHAUSTED;
    }
    if (!wifi_ssid_is_valid(request->ssid)) {
        return AIQA_WIFI_UPDATE_ERR_SSID;
    }

    switch (request->password_action) {
    case AIQA_WIFI_PASSWORD_KEEP:
        if (request->password != NULL && request->password[0] != '\0') {
            return AIQA_WIFI_UPDATE_ERR_PASSWORD_ACTION;
        }
        break;
    case AIQA_WIFI_PASSWORD_REPLACE: {
        if (!wifi_password_is_valid(request->password)) {
            return AIQA_WIFI_UPDATE_ERR_PASSWORD;
        }
        break;
    }
    case AIQA_WIFI_PASSWORD_CLEAR:
        if (request->password != NULL && request->password[0] != '\0') {
            return AIQA_WIFI_UPDATE_ERR_PASSWORD_ACTION;
        }
        break;
    default:
        return AIQA_WIFI_UPDATE_ERR_PASSWORD_ACTION;
    }

    aiqa_secret_config_t candidate = *current;
    const size_t ssid_len = bounded_strlen(request->ssid, AIQA_MAX_WIFI_SSID_LEN);
    (void)memset(candidate.wifi_ssid, 0, sizeof(candidate.wifi_ssid));
    (void)memcpy(candidate.wifi_ssid, request->ssid, ssid_len);
    candidate.wifi_ssid[ssid_len] = '\0';
    if (request->password_action == AIQA_WIFI_PASSWORD_REPLACE) {
        const size_t password_len = bounded_strlen(request->password, AIQA_MAX_WIFI_PASSWORD_LEN);
        (void)memset(candidate.wifi_password, 0, sizeof(candidate.wifi_password));
        (void)memcpy(candidate.wifi_password, request->password, password_len);
        candidate.wifi_password[password_len] = '\0';
    } else if (request->password_action == AIQA_WIFI_PASSWORD_CLEAR) {
        (void)memset(candidate.wifi_password, 0, sizeof(candidate.wifi_password));
    }

    *updated = candidate;
    *next_revision = current_revision + 1;
    secure_zero(&candidate, sizeof(candidate));
    return AIQA_WIFI_UPDATE_OK;
}

bool aiqa_config_build_public_wifi_view(
    const aiqa_secret_config_t *secrets,
    uint32_t revision,
    aiqa_public_wifi_config_t *out_view)
{
    if (secrets == NULL || out_view == NULL || !wifi_ssid_is_valid(secrets->wifi_ssid)) {
        return false;
    }

    aiqa_public_wifi_config_t view = {.revision = revision};
    const size_t ssid_len = bounded_strlen(secrets->wifi_ssid, sizeof(view.ssid));
    (void)memcpy(view.ssid, secrets->wifi_ssid, ssid_len);
    view.ssid[ssid_len] = '\0';
    view.has_password = secrets->wifi_password[0] != '\0';
    *out_view = view;
    return true;
}

const char *aiqa_wifi_update_status_name(aiqa_wifi_update_status_t status)
{
    switch (status) {
    case AIQA_WIFI_UPDATE_OK:
        return "OK";
    case AIQA_WIFI_UPDATE_ERR_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case AIQA_WIFI_UPDATE_ERR_REVISION_CONFLICT:
        return "REVISION_CONFLICT";
    case AIQA_WIFI_UPDATE_ERR_REVISION_EXHAUSTED:
        return "REVISION_EXHAUSTED";
    case AIQA_WIFI_UPDATE_ERR_SSID:
        return "SSID";
    case AIQA_WIFI_UPDATE_ERR_PASSWORD:
        return "PASSWORD";
    case AIQA_WIFI_UPDATE_ERR_PASSWORD_ACTION:
        return "PASSWORD_ACTION";
    default:
        return "UNKNOWN";
    }
}
