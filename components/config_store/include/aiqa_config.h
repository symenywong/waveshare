#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_CONFIG_VERSION 1
#define AIQA_MAX_PROVIDER_ID_LEN 32
#define AIQA_MAX_MODEL_LEN 64
#define AIQA_MAX_BASE_URL_LEN 160
#define AIQA_MAX_WIFI_SSID_LEN 33
#define AIQA_MAX_WIFI_PASSWORD_LEN 65
#define AIQA_MAX_API_KEY_LEN 192
#define AIQA_MAX_TTS_VOICE_LEN 32

typedef struct {
    int config_version;
    char active_provider[AIQA_MAX_PROVIDER_ID_LEN];
    char model[AIQA_MAX_MODEL_LEN];
    char base_url[AIQA_MAX_BASE_URL_LEN];
    char asr_provider[AIQA_MAX_PROVIDER_ID_LEN];
    char asr_model[AIQA_MAX_MODEL_LEN];
    char asr_base_url[AIQA_MAX_BASE_URL_LEN];
    char tts_provider[AIQA_MAX_PROVIDER_ID_LEN];
    char tts_model[AIQA_MAX_MODEL_LEN];
    char tts_base_url[AIQA_MAX_BASE_URL_LEN];
    char tts_voice[AIQA_MAX_TTS_VOICE_LEN];
    bool stream;
    bool hide_reasoning;
    int max_completion_tokens;
} aiqa_config_t;

typedef struct {
    char wifi_ssid[AIQA_MAX_WIFI_SSID_LEN];
    char wifi_password[AIQA_MAX_WIFI_PASSWORD_LEN];
    char chat_api_key[AIQA_MAX_API_KEY_LEN];
    char asr_api_key[AIQA_MAX_API_KEY_LEN];
} aiqa_secret_config_t;

typedef enum {
    AIQA_CONFIG_OK = 0,
    AIQA_CONFIG_ERR_VERSION,
    AIQA_CONFIG_ERR_PROVIDER,
    AIQA_CONFIG_ERR_MODEL,
    AIQA_CONFIG_ERR_BASE_URL,
} aiqa_config_status_t;

typedef enum {
    AIQA_SECRET_OK = 0,
    AIQA_SECRET_ERR_WIFI_SSID,
    AIQA_SECRET_ERR_WIFI_PASSWORD,
    AIQA_SECRET_ERR_CHAT_API_KEY,
} aiqa_secret_status_t;

typedef enum {
    AIQA_WIFI_PASSWORD_KEEP = 0,
    AIQA_WIFI_PASSWORD_REPLACE,
    AIQA_WIFI_PASSWORD_CLEAR,
} aiqa_wifi_password_action_t;

typedef struct {
    uint32_t base_revision;
    const char *ssid;
    aiqa_wifi_password_action_t password_action;
    const char *password;
} aiqa_wifi_update_t;

typedef struct {
    uint32_t revision;
    char ssid[AIQA_MAX_WIFI_SSID_LEN];
    bool has_password;
} aiqa_public_wifi_config_t;

typedef enum {
    AIQA_WIFI_UPDATE_OK = 0,
    AIQA_WIFI_UPDATE_ERR_INVALID_ARGUMENT,
    AIQA_WIFI_UPDATE_ERR_REVISION_CONFLICT,
    AIQA_WIFI_UPDATE_ERR_REVISION_EXHAUSTED,
    AIQA_WIFI_UPDATE_ERR_SSID,
    AIQA_WIFI_UPDATE_ERR_PASSWORD,
    AIQA_WIFI_UPDATE_ERR_PASSWORD_ACTION,
} aiqa_wifi_update_status_t;

aiqa_config_t aiqa_config_default(void);
aiqa_config_status_t aiqa_config_validate(const aiqa_config_t *config);
const char *aiqa_config_status_name(aiqa_config_status_t status);
aiqa_secret_config_t aiqa_secret_config_empty(void);
aiqa_secret_status_t aiqa_secret_config_validate(const aiqa_secret_config_t *secrets);
const char *aiqa_secret_status_name(aiqa_secret_status_t status);
aiqa_wifi_update_status_t aiqa_config_prepare_wifi_update(
    const aiqa_secret_config_t *current,
    uint32_t current_revision,
    const aiqa_wifi_update_t *request,
    aiqa_secret_config_t *updated,
    uint32_t *next_revision);
bool aiqa_config_build_public_wifi_view(
    const aiqa_secret_config_t *secrets,
    uint32_t revision,
    aiqa_public_wifi_config_t *out_view);
const char *aiqa_wifi_update_status_name(aiqa_wifi_update_status_t status);

#ifdef __cplusplus
}
#endif
