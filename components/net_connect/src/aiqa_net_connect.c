#include "aiqa_net_connect.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "aiqa_net";

#define AIQA_WIFI_CONNECTED_BIT BIT0
#define AIQA_WIFI_FAILED_BIT BIT1

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_wifi_netif;
static bool s_wifi_initialized;
static uint8_t s_wifi_retry_count;
static uint8_t s_wifi_max_retries;

static esp_err_t ok_if_already_initialized(esp_err_t ret)
{
    return ret == ESP_ERR_INVALID_STATE ? ESP_OK : ret;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        (void)esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_count < s_wifi_max_retries) {
            ++s_wifi_retry_count;
            ESP_LOGW(TAG, "Wi-Fi disconnected; retry %u/%u",
                     (unsigned)s_wifi_retry_count,
                     (unsigned)s_wifi_max_retries);
            (void)esp_wifi_connect();
            return;
        }

        if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, AIQA_WIFI_FAILED_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_retry_count = 0;
        ESP_LOGI(TAG, "Wi-Fi got IP");
        if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, AIQA_WIFI_CONNECTED_BIT);
        }
    }
}

static esp_err_t ensure_wifi_initialized(void)
{
    esp_err_t ret = ok_if_already_initialized(esp_netif_init());
    if (ret != ESP_OK) {
        return ret;
    }

    ret = ok_if_already_initialized(esp_event_loop_create_default());
    if (ret != ESP_OK) {
        return ret;
    }

    if (s_wifi_initialized) {
        return ESP_OK;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_wifi_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&wifi_init_config);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    s_wifi_initialized = true;
    return ESP_OK;
}

static esp_err_t connect_wifi(const aiqa_secret_config_t *secrets, const aiqa_net_policy_t *policy)
{
    esp_err_t ret = ensure_wifi_initialized();
    if (ret != ESP_OK) {
        return ret;
    }

    wifi_config_t wifi_config = {0};
    size_t ssid_len = strlen(secrets->wifi_ssid);
    size_t password_len = strlen(secrets->wifi_password);
    if (ssid_len == 0 || ssid_len > sizeof(wifi_config.sta.ssid) ||
        password_len >= sizeof(wifi_config.sta.password)) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)memcpy(wifi_config.sta.ssid, secrets->wifi_ssid, ssid_len);
    (void)memcpy(wifi_config.sta.password, secrets->wifi_password, password_len);
    wifi_config.sta.threshold.authmode =
        secrets->wifi_password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    xEventGroupClearBits(s_wifi_event_group, AIQA_WIFI_CONNECTED_BIT | AIQA_WIFI_FAILED_BIT);
    s_wifi_retry_count = 0;
    s_wifi_max_retries = policy->max_wifi_retries;

    (void)esp_wifi_stop();
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set Wi-Fi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set Wi-Fi config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start Wi-Fi failed");

    ESP_LOGI(TAG, "Wi-Fi connecting with configured SSID");
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        AIQA_WIFI_CONNECTED_BIT | AIQA_WIFI_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(policy->connect_timeout_ms));

    if ((bits & AIQA_WIFI_CONNECTED_BIT) != 0) {
        return ESP_OK;
    }
    if ((bits & AIQA_WIFI_FAILED_BIT) != 0) {
        return ESP_ERR_WIFI_CONN;
    }

    return ESP_ERR_TIMEOUT;
}

static esp_err_t sync_time(const aiqa_net_policy_t *policy)
{
    time_t now = 0;
    time(&now);
    if (aiqa_net_time_is_valid(now)) {
        ESP_LOGI(TAG, "System time already valid");
        return ESP_OK;
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(policy->sntp_server);
    esp_err_t ret = esp_netif_sntp_init(&config);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(policy->sntp_timeout_ms));
    esp_netif_sntp_deinit();
    if (ret != ESP_OK) {
        return ret;
    }

    time(&now);
    if (!aiqa_net_time_is_valid(now)) {
        ESP_LOGE(TAG, "SNTP completed but time is still invalid");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "System time synchronized");
    return ESP_OK;
}

esp_err_t aiqa_net_connect_wifi_and_sync_time(
    const aiqa_secret_config_t *secrets,
    const aiqa_net_policy_t *policy)
{
    if (secrets == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    aiqa_secret_status_t secret_status = aiqa_secret_config_validate(secrets);
    if (secret_status != AIQA_SECRET_OK) {
        ESP_LOGE(TAG, "Cannot connect network; secret validation failed: %s",
                 aiqa_secret_status_name(secret_status));
        return ESP_ERR_INVALID_ARG;
    }

    aiqa_net_policy_t default_policy = aiqa_net_default_policy();
    const aiqa_net_policy_t *active_policy = policy != NULL ? policy : &default_policy;

    ESP_RETURN_ON_ERROR(connect_wifi(secrets, active_policy), TAG, "Wi-Fi connect failed");
    ESP_RETURN_ON_ERROR(sync_time(active_policy), TAG, "SNTP sync failed");
    return ESP_OK;
}
