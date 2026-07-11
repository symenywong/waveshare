#include "aiqa_asr_client.h"

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "aiqa_asr";

#define AIQA_ASR_HTTP_RESPONSE_MAX_LEN 4096

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool overflow;
} aiqa_asr_response_buffer_t;

static void asr_result_reset(aiqa_asr_result_t *result)
{
    if (result == NULL) {
        return;
    }
    *result = (aiqa_asr_result_t){
        .status = AIQA_ASR_ERR_PROVIDER,
        .http_status = 0,
        .text = {0},
    };
}

static void secure_zero(void *data, size_t length)
{
    volatile unsigned char *cursor = (volatile unsigned char *)data;
    while (cursor != NULL && length > 0) {
        *cursor++ = 0;
        --length;
    }
}

static esp_err_t asr_http_event_handler(esp_http_client_event_t *event)
{
    if (event == NULL || event->user_data == NULL) {
        return ESP_OK;
    }

    aiqa_asr_response_buffer_t *buffer = (aiqa_asr_response_buffer_t *)event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data == NULL || event->data_len <= 0) {
        return ESP_OK;
    }

    const size_t chunk_len = (size_t)event->data_len;
    if (buffer->length + chunk_len >= buffer->capacity) {
        buffer->overflow = true;
        return ESP_OK;
    }

    (void)memcpy(buffer->data + buffer->length, event->data, chunk_len);
    buffer->length += chunk_len;
    buffer->data[buffer->length] = '\0';
    return ESP_OK;
}

static aiqa_asr_status_t status_from_transport(esp_err_t ret)
{
    if (ret == ESP_OK) {
        return AIQA_ASR_OK;
    }
    if (ret == ESP_ERR_TIMEOUT) {
        return AIQA_ASR_ERR_TIMEOUT;
    }
    return AIQA_ASR_ERR_PROVIDER;
}

static const char *select_asr_key(const aiqa_secret_config_t *secrets)
{
    if (secrets == NULL) {
        return NULL;
    }
    return secrets->asr_api_key[0] != '\0' ? secrets->asr_api_key : secrets->chat_api_key;
}

esp_err_t aiqa_asr_transcribe_once(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *audio_ref,
    aiqa_asr_result_t *result)
{
    asr_result_reset(result);
    if (config == NULL || secrets == NULL || audio_ref == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (aiqa_config_validate(config) != AIQA_CONFIG_OK ||
        aiqa_secret_config_validate(secrets) != AIQA_SECRET_OK) {
        result->status = AIQA_ASR_ERR_INVALID_ARG;
        return ESP_ERR_INVALID_ARG;
    }

    const char *api_key = select_asr_key(secrets);
    if (api_key == NULL || api_key[0] == '\0') {
        result->status = AIQA_ASR_ERR_AUTH;
        return ESP_ERR_INVALID_ARG;
    }

    char endpoint_url[AIQA_ASR_ENDPOINT_MAX_LEN] = {0};
    aiqa_asr_status_t asr_status = aiqa_asr_build_endpoint_url(
        config->asr_base_url,
        endpoint_url,
        sizeof(endpoint_url));
    if (asr_status != AIQA_ASR_OK) {
        result->status = asr_status;
        return ESP_ERR_INVALID_ARG;
    }

    aiqa_asr_options_t options = {
        .audio_ref = audio_ref,
        .language_hint = "zh",
        .enable_itn = true,
    };
    char *request_body = (char *)malloc(AIQA_ASR_REQUEST_MAX_LEN);
    char *response_body = (char *)malloc(AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
    if (request_body == NULL || response_body == NULL) {
        free(request_body);
        free(response_body);
        result->status = AIQA_ASR_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }
    request_body[0] = '\0';
    response_body[0] = '\0';

    asr_status = aiqa_asr_build_request_json(config, &options, request_body, AIQA_ASR_REQUEST_MAX_LEN);
    if (asr_status != AIQA_ASR_OK) {
        secure_zero(request_body, AIQA_ASR_REQUEST_MAX_LEN);
        secure_zero(response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
        free(request_body);
        free(response_body);
        result->status = asr_status;
        return ESP_ERR_INVALID_ARG;
    }

    aiqa_asr_response_buffer_t response = {
        .data = response_body,
        .capacity = AIQA_ASR_HTTP_RESPONSE_MAX_LEN,
        .length = 0,
        .overflow = false,
    };
    esp_http_client_config_t http_config = {
        .url = endpoint_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = AIQA_ASR_DEFAULT_TIMEOUT_MS,
        .event_handler = asr_http_event_handler,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        secure_zero(request_body, AIQA_ASR_REQUEST_MAX_LEN);
        secure_zero(response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
        free(request_body);
        free(response_body);
        result->status = AIQA_ASR_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }

    char auth_header[AIQA_MAX_API_KEY_LEN + 8] = {0};
    int written = snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);
    if (written < 0 || (size_t)written >= sizeof(auth_header)) {
        (void)esp_http_client_cleanup(client);
        secure_zero(auth_header, sizeof(auth_header));
        secure_zero(request_body, AIQA_ASR_REQUEST_MAX_LEN);
        secure_zero(response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
        free(request_body);
        free(response_body);
        result->status = AIQA_ASR_ERR_AUTH;
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (ret == ESP_OK) {
        ret = esp_http_client_set_header(client, "Authorization", auth_header);
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_set_post_field(client, request_body, (int)strlen(request_body));
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sending ASR request to configured provider");
        ret = esp_http_client_perform(client);
    }

    result->http_status = esp_http_client_get_status_code(client);
    if (ret != ESP_OK) {
        result->status = status_from_transport(ret);
        ESP_LOGE(TAG, "ASR transport failed: %s", esp_err_to_name(ret));
    } else if (response.overflow) {
        result->status = AIQA_ASR_ERR_BUFFER_TOO_SMALL;
        ESP_LOGE(TAG, "ASR response exceeded buffer");
    } else {
        result->status = aiqa_asr_status_from_http_status(result->http_status);
        if (result->status == AIQA_ASR_OK) {
            result->status = aiqa_asr_parse_transcript_text(response.data, result->text, sizeof(result->text));
        }
        if (result->status != AIQA_ASR_OK) {
            ESP_LOGE(TAG, "ASR provider failed: status=%s http=%d",
                     aiqa_asr_status_name(result->status),
                     result->http_status);
        }
    }

    (void)esp_http_client_cleanup(client);
    secure_zero(auth_header, sizeof(auth_header));
    secure_zero(request_body, AIQA_ASR_REQUEST_MAX_LEN);
    secure_zero(response_body, AIQA_ASR_HTTP_RESPONSE_MAX_LEN);
    free(request_body);
    free(response_body);
    return ret == ESP_OK ? ESP_OK : ret;
}
