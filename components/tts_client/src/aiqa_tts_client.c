#include "aiqa_tts_client.h"

#include "aiqa_provider.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "mbedtls/base64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "aiqa_tts";

#define AIQA_TTS_STREAM_CARRY_MAX_LEN (AIQA_TTS_AUDIO_BASE64_MAX_LEN + 4096u)
#define AIQA_TTS_PCM_CHUNK_MAX_BYTES ((AIQA_TTS_AUDIO_BASE64_MAX_LEN * 3u) / 4u)

typedef struct {
    char carry[AIQA_TTS_STREAM_CARRY_MAX_LEN];
    size_t carry_length;
    char audio_b64[AIQA_TTS_AUDIO_BASE64_MAX_LEN];
    uint8_t pcm[AIQA_TTS_PCM_CHUNK_MAX_BYTES];
    bool overflow;
    const char *overflow_reason;
    aiqa_tts_audio_cb_t on_audio;
    void *user_ctx;
    aiqa_tts_result_t *result;
} aiqa_tts_stream_context_t;

static void set_stream_overflow(aiqa_tts_stream_context_t *context, const char *reason)
{
    if (context == NULL) {
        return;
    }
    context->overflow = true;
    if (context->overflow_reason == NULL) {
        context->overflow_reason = reason;
    }
}

static void log_stream_audio_progress(const aiqa_tts_result_t *result, size_t pcm_len)
{
    if (result == NULL) {
        return;
    }
    if (result->audio_chunks < 3 || (result->audio_chunks % 16u) == 0) {
        ESP_LOGI(TAG,
                 "TTS audio chunk decoded: chunk=%u bytes=%u total=%u",
                 (unsigned)(result->audio_chunks + 1),
                 (unsigned)pcm_len,
                 (unsigned)(result->audio_bytes + pcm_len));
    }
}

static void tts_result_reset(aiqa_tts_result_t *result)
{
    if (result == NULL) {
        return;
    }
    *result = (aiqa_tts_result_t){
        .status = AIQA_TTS_ERR_PROVIDER,
        .http_status = 0,
        .audio_bytes = 0,
        .audio_chunks = 0,
    };
}

static aiqa_tts_status_t status_from_transport(esp_err_t ret)
{
    if (ret == ESP_OK) {
        return AIQA_TTS_OK;
    }
    if (ret == ESP_ERR_TIMEOUT) {
        return AIQA_TTS_ERR_TIMEOUT;
    }
    return AIQA_TTS_ERR_PROVIDER;
}

static void secure_zero(void *data, size_t length)
{
    volatile unsigned char *cursor = (volatile unsigned char *)data;
    while (cursor != NULL && length > 0) {
        *cursor++ = 0;
        --length;
    }
}

static char *find_stream_event_separator(char *data, size_t length, size_t *separator_len)
{
    if (data == NULL || separator_len == NULL) {
        return NULL;
    }

    for (size_t index = 0; index + 1 < length; ++index) {
        if (data[index] == '\n' && data[index + 1] == '\n') {
            *separator_len = 2;
            return data + index;
        }
        if (index + 3 < length &&
            data[index] == '\r' &&
            data[index + 1] == '\n' &&
            data[index + 2] == '\r' &&
            data[index + 3] == '\n') {
            *separator_len = 4;
            return data + index;
        }
    }

    return NULL;
}

static void process_stream_event(aiqa_tts_stream_context_t *context, char *event_data, size_t event_len)
{
    if (context == NULL || event_data == NULL || event_len == 0) {
        return;
    }
    if (event_len >= AIQA_TTS_STREAM_CARRY_MAX_LEN) {
        set_stream_overflow(context, "event");
        return;
    }

    event_data[event_len] = '\0';
    aiqa_tts_status_t parse_status =
        aiqa_tts_parse_stream_audio_data(event_data, context->audio_b64, sizeof(context->audio_b64));
    if (parse_status == AIQA_TTS_ERR_BUFFER_TOO_SMALL) {
        set_stream_overflow(context, "audio_b64");
        return;
    }
    if (parse_status != AIQA_TTS_OK) {
        return;
    }
    if (context->audio_b64[0] == '\0') {
        return;
    }

    size_t pcm_len = 0;
    const int b64_ret = mbedtls_base64_decode(context->pcm,
                                              sizeof(context->pcm),
                                              &pcm_len,
                                              (const unsigned char *)context->audio_b64,
                                              strlen(context->audio_b64));
    secure_zero(context->audio_b64, sizeof(context->audio_b64));
    if (b64_ret != 0 || pcm_len == 0 || pcm_len > sizeof(context->pcm)) {
        set_stream_overflow(context, "base64_decode");
        return;
    }

    if (context->on_audio != NULL) {
        context->on_audio(context->pcm, pcm_len, context->user_ctx);
    }
    log_stream_audio_progress(context->result, pcm_len);
    if (context->result != NULL) {
        context->result->audio_bytes += pcm_len;
        context->result->audio_chunks += 1;
    }
    secure_zero(context->pcm, sizeof(context->pcm));
}

static void process_stream_chunk(aiqa_tts_stream_context_t *context, const char *chunk, size_t chunk_len)
{
    if (context == NULL || chunk == NULL || chunk_len == 0) {
        return;
    }
    if (context->carry_length + chunk_len >= sizeof(context->carry)) {
        set_stream_overflow(context, "carry");
        return;
    }

    (void)memcpy(context->carry + context->carry_length, chunk, chunk_len);
    context->carry_length += chunk_len;
    context->carry[context->carry_length] = '\0';

    size_t separator_len = 0;
    char *separator = find_stream_event_separator(context->carry, context->carry_length, &separator_len);
    while (separator != NULL) {
        const size_t event_len = (size_t)(separator - context->carry);
        process_stream_event(context, context->carry, event_len);

        const size_t consumed = event_len + separator_len;
        const size_t remaining = context->carry_length - consumed;
        if (remaining > 0) {
            (void)memmove(context->carry, context->carry + consumed, remaining);
        }
        context->carry_length = remaining;
        context->carry[context->carry_length] = '\0';
        separator = find_stream_event_separator(context->carry, context->carry_length, &separator_len);
    }
}

static void process_stream_remainder(aiqa_tts_stream_context_t *context)
{
    if (context == NULL || context->carry_length == 0) {
        return;
    }

    process_stream_event(context, context->carry, context->carry_length);
    context->carry_length = 0;
    context->carry[0] = '\0';
}

static esp_err_t tts_http_event_handler(esp_http_client_event_t *event)
{
    if (event == NULL || event->user_data == NULL) {
        return ESP_OK;
    }
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data == NULL || event->data_len <= 0) {
        return ESP_OK;
    }

    aiqa_tts_stream_context_t *context = (aiqa_tts_stream_context_t *)event->user_data;
    process_stream_chunk(context, (const char *)event->data, (size_t)event->data_len);
    return ESP_OK;
}

esp_err_t aiqa_tts_speak_streaming(
    const aiqa_config_t *config,
    const aiqa_secret_config_t *secrets,
    const char *text,
    aiqa_tts_audio_cb_t on_audio,
    void *user_ctx,
    aiqa_tts_result_t *result)
{
    tts_result_reset(result);
    if (config == NULL || secrets == NULL || text == NULL || text[0] == '\0' || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (aiqa_config_validate(config) != AIQA_CONFIG_OK ||
        aiqa_secret_config_validate(secrets) != AIQA_SECRET_OK) {
        result->status = AIQA_TTS_ERR_INVALID_ARG;
        return ESP_ERR_INVALID_ARG;
    }

    const aiqa_provider_caps_t *caps = aiqa_provider_caps_for(config->tts_provider);
    if (caps == NULL || !caps->supports_tts_stream) {
        result->status = AIQA_TTS_ERR_UNSUPPORTED_PROVIDER;
        return ESP_ERR_INVALID_ARG;
    }

    char endpoint_url[AIQA_TTS_ENDPOINT_MAX_LEN] = {0};
    aiqa_tts_status_t tts_status = aiqa_tts_build_endpoint_url(
        config->tts_base_url,
        endpoint_url,
        sizeof(endpoint_url));
    if (tts_status != AIQA_TTS_OK) {
        result->status = tts_status;
        return ESP_ERR_INVALID_ARG;
    }

    aiqa_tts_options_t options = {
        .voice = config->tts_voice,
        .format = "pcm",
        .sample_rate_hz = caps->tts_sample_rate_hz,
        .stream = true,
    };
    char *request_body = (char *)malloc(AIQA_TTS_REQUEST_MAX_LEN);
    aiqa_tts_stream_context_t *stream = (aiqa_tts_stream_context_t *)calloc(1, sizeof(*stream));
    if (request_body == NULL || stream == NULL) {
        free(request_body);
        free(stream);
        result->status = AIQA_TTS_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }
    request_body[0] = '\0';

    tts_status = aiqa_tts_build_request_json(config, &options, text, request_body, AIQA_TTS_REQUEST_MAX_LEN);
    if (tts_status != AIQA_TTS_OK) {
        secure_zero(request_body, AIQA_TTS_REQUEST_MAX_LEN);
        secure_zero(stream, sizeof(*stream));
        free(request_body);
        free(stream);
        result->status = tts_status;
        return ESP_ERR_INVALID_ARG;
    }

    stream->on_audio = on_audio;
    stream->user_ctx = user_ctx;
    stream->result = result;

    esp_http_client_config_t http_config = {
        .url = endpoint_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = AIQA_TTS_DEFAULT_TIMEOUT_MS,
        .event_handler = tts_http_event_handler,
        .user_data = stream,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        secure_zero(request_body, AIQA_TTS_REQUEST_MAX_LEN);
        secure_zero(stream, sizeof(*stream));
        free(request_body);
        free(stream);
        result->status = AIQA_TTS_ERR_PROVIDER;
        return ESP_ERR_NO_MEM;
    }

    char auth_header[AIQA_MAX_API_KEY_LEN + 8] = {0};
    int written = snprintf(auth_header, sizeof(auth_header), "Bearer %s", secrets->chat_api_key);
    if (written < 0 || (size_t)written >= sizeof(auth_header)) {
        (void)esp_http_client_cleanup(client);
        secure_zero(auth_header, sizeof(auth_header));
        secure_zero(request_body, AIQA_TTS_REQUEST_MAX_LEN);
        secure_zero(stream, sizeof(*stream));
        free(request_body);
        free(stream);
        result->status = AIQA_TTS_ERR_AUTH;
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (ret == ESP_OK) {
        ret = esp_http_client_set_header(client, "Accept", "text/event-stream");
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_set_header(client, "X-DashScope-SSE", "enable");
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_set_header(client, "Authorization", auth_header);
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_set_post_field(client, request_body, (int)strlen(request_body));
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG,
                 "Sending TTS request: model=%s voice=%s text_bytes=%u",
                 config->tts_model,
                 config->tts_voice,
                 (unsigned)strlen(text));
        ret = esp_http_client_perform(client);
    }

    result->http_status = esp_http_client_get_status_code(client);
    if (ret != ESP_OK) {
        result->status = status_from_transport(ret);
        ESP_LOGE(TAG, "TTS transport failed: %s", esp_err_to_name(ret));
    } else {
        process_stream_remainder(stream);
        result->status = aiqa_tts_status_from_http_status(result->http_status);
        if (result->status == AIQA_TTS_OK) {
            if (stream->overflow) {
                result->status = AIQA_TTS_ERR_BUFFER_TOO_SMALL;
            } else if (result->audio_bytes == 0) {
                result->status = AIQA_TTS_ERR_PARSE;
            }
        }
        if (result->status != AIQA_TTS_OK) {
            ESP_LOGE(TAG,
                     "TTS provider failed: status=%s http=%d audio_chunks=%u audio_bytes=%u overflow=%d reason=%s",
                     aiqa_tts_status_name(result->status),
                     result->http_status,
                     (unsigned)result->audio_chunks,
                     (unsigned)result->audio_bytes,
                     stream->overflow ? 1 : 0,
                     stream->overflow_reason == NULL ? "none" : stream->overflow_reason);
        }
    }

    (void)esp_http_client_cleanup(client);
    secure_zero(auth_header, sizeof(auth_header));
    secure_zero(request_body, AIQA_TTS_REQUEST_MAX_LEN);
    secure_zero(stream, sizeof(*stream));
    free(request_body);
    free(stream);
    return ret == ESP_OK ? ESP_OK : ret;
}
