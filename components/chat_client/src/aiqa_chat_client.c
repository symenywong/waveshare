#include "aiqa_chat_client.h"

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "aiqa_chat";

#define AIQA_CHAT_HTTP_RESPONSE_MAX_LEN 4096

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool overflow;
} aiqa_chat_response_buffer_t;

static void chat_result_reset(aiqa_chat_result_t *result)
{
    if (result == NULL) {
        return;
    }
    *result = (aiqa_chat_result_t){
        .status = AIQA_CHAT_ERR_PROVIDER,
        .http_status = 0,
        .text = {0},
    };
}

static esp_err_t chat_http_event_handler(esp_http_client_event_t *event)
{
    if (event == NULL || event->user_data == NULL) {
        return ESP_OK;
    }

    aiqa_chat_response_buffer_t *buffer = (aiqa_chat_response_buffer_t *)event->user_data;
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

static aiqa_chat_status_t status_from_transport(esp_err_t ret)
{
    if (ret == ESP_OK) {
        return AIQA_CHAT_OK;
    }
    if (ret == ESP_ERR_TIMEOUT) {
        return AIQA_CHAT_ERR_TIMEOUT;
    }
    return AIQA_CHAT_ERR_PROVIDER;
}

static void secure_zero(void *data, size_t length)
{
    volatile unsigned char *cursor = (volatile unsigned char *)data;
    while (cursor != NULL && length > 0) {
        *cursor++ = 0;
        --length;
    }
}

esp_err_t aiqa_chat_send_once(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *prompt,
    aiqa_chat_result_t *result)
{
    chat_result_reset(result);
    if (config == NULL || secrets == NULL || prompt == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (aiqa_config_validate(config) != AIQA_CONFIG_OK ||
        aiqa_secret_config_validate(secrets) != AIQA_SECRET_OK) {
        result->status = AIQA_CHAT_ERR_INVALID_ARG;
        return ESP_ERR_INVALID_ARG;
    }

    char endpoint_url[AIQA_CHAT_ENDPOINT_MAX_LEN] = {0};
    aiqa_chat_status_t chat_status = aiqa_chat_build_endpoint_url(
        config->base_url,
        endpoint_url,
        sizeof(endpoint_url));
    if (chat_status != AIQA_CHAT_OK) {
        result->status = chat_status;
        return ESP_ERR_INVALID_ARG;
    }

    aiqa_chat_options_t options = {
        .stream = false,
        .hide_reasoning = config->hide_reasoning,
        .max_completion_tokens = config->max_completion_tokens,
    };
    char *request_body = (char *)malloc(AIQA_CHAT_REQUEST_MAX_LEN);
    char *response_body = (char *)malloc(AIQA_CHAT_HTTP_RESPONSE_MAX_LEN);
    if (request_body == NULL || response_body == NULL) {
        free(request_body);
        free(response_body);
        result->status = AIQA_CHAT_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }
    request_body[0] = '\0';
    response_body[0] = '\0';

    chat_status = aiqa_chat_build_request_json(
        config,
        &options,
        prompt,
        request_body,
        AIQA_CHAT_REQUEST_MAX_LEN);
    if (chat_status != AIQA_CHAT_OK) {
        secure_zero(request_body, AIQA_CHAT_REQUEST_MAX_LEN);
        secure_zero(response_body, AIQA_CHAT_HTTP_RESPONSE_MAX_LEN);
        free(request_body);
        free(response_body);
        result->status = chat_status;
        return ESP_ERR_INVALID_ARG;
    }

    aiqa_chat_response_buffer_t response = {
        .data = response_body,
        .capacity = AIQA_CHAT_HTTP_RESPONSE_MAX_LEN,
        .length = 0,
        .overflow = false,
    };
    esp_http_client_config_t http_config = {
        .url = endpoint_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = AIQA_CHAT_DEFAULT_TIMEOUT_MS,
        .event_handler = chat_http_event_handler,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        secure_zero(request_body, AIQA_CHAT_REQUEST_MAX_LEN);
        secure_zero(response_body, AIQA_CHAT_HTTP_RESPONSE_MAX_LEN);
        free(request_body);
        free(response_body);
        result->status = AIQA_CHAT_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }

    char auth_header[AIQA_MAX_API_KEY_LEN + 8] = {0};
    int written = snprintf(auth_header, sizeof(auth_header), "Bearer %s", secrets->chat_api_key);
    if (written < 0 || (size_t)written >= sizeof(auth_header)) {
        (void)esp_http_client_cleanup(client);
        secure_zero(auth_header, sizeof(auth_header));
        secure_zero(request_body, AIQA_CHAT_REQUEST_MAX_LEN);
        secure_zero(response_body, AIQA_CHAT_HTTP_RESPONSE_MAX_LEN);
        free(request_body);
        free(response_body);
        result->status = AIQA_CHAT_ERR_AUTH;
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
        ESP_LOGI(TAG, "Sending chat request to configured provider");
        ret = esp_http_client_perform(client);
    }

    result->http_status = esp_http_client_get_status_code(client);
    if (ret != ESP_OK) {
        result->status = status_from_transport(ret);
        ESP_LOGE(TAG, "Chat transport failed: %s", esp_err_to_name(ret));
    } else if (response.overflow) {
        result->status = AIQA_CHAT_ERR_BUFFER_TOO_SMALL;
        ESP_LOGE(TAG, "Chat response exceeded buffer");
    } else {
        result->status = aiqa_chat_status_from_http_status(result->http_status);
        if (result->status == AIQA_CHAT_OK) {
            result->status = aiqa_chat_parse_response_text(response.data, result->text, sizeof(result->text));
        }
        if (result->status != AIQA_CHAT_OK) {
            ESP_LOGE(TAG, "Chat provider failed: status=%s http=%d",
                     aiqa_chat_status_name(result->status),
                     result->http_status);
        }
    }

    (void)esp_http_client_cleanup(client);
    secure_zero(auth_header, sizeof(auth_header));
    secure_zero(request_body, AIQA_CHAT_REQUEST_MAX_LEN);
    secure_zero(response_body, AIQA_CHAT_HTTP_RESPONSE_MAX_LEN);
    free(request_body);
    free(response_body);
    return ret == ESP_OK ? ESP_OK : ret;
}
