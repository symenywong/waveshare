#pragma once

#include <stdbool.h>
#include <stddef.h>

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

typedef struct {
    int config_version;
    char active_provider[AIQA_MAX_PROVIDER_ID_LEN];
    char model[AIQA_MAX_MODEL_LEN];
    char base_url[AIQA_MAX_BASE_URL_LEN];
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

aiqa_config_t aiqa_config_default(void);
aiqa_config_status_t aiqa_config_validate(const aiqa_config_t *config);
const char *aiqa_config_status_name(aiqa_config_status_t status);
aiqa_secret_config_t aiqa_secret_config_empty(void);
aiqa_secret_status_t aiqa_secret_config_validate(const aiqa_secret_config_t *secrets);
const char *aiqa_secret_status_name(aiqa_secret_status_t status);

#ifdef __cplusplus
}
#endif
