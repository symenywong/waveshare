#include "aiqa_net_connect.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>

static const char *TAG = "aiqa_net";

#define AIQA_WIFI_CONNECTED_BIT BIT0
#define AIQA_WIFI_FAILED_BIT BIT1

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_wifi_netif;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static bool s_wifi_driver_initialized;
static bool s_wifi_initialized;
static atomic_bool s_wifi_should_retry = ATOMIC_VAR_INIT(false);
static atomic_uchar s_wifi_retry_count = ATOMIC_VAR_INIT(0);
static atomic_uchar s_wifi_max_retries = ATOMIC_VAR_INIT(0);

static esp_err_t ok_if_already_initialized(esp_err_t ret)
{
    return ret == ESP_ERR_INVALID_STATE ? ESP_OK : ret;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (atomic_load_explicit(&s_wifi_should_retry, memory_order_acquire)) {
            (void)esp_wifi_connect();
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!atomic_load_explicit(&s_wifi_should_retry, memory_order_acquire)) {
            return;
        }
        const uint8_t retry_count =
            atomic_load_explicit(&s_wifi_retry_count, memory_order_relaxed);
        const uint8_t max_retries =
            atomic_load_explicit(&s_wifi_max_retries, memory_order_relaxed);
        if (retry_count < max_retries) {
            const uint8_t next_retry = (uint8_t)(retry_count + 1U);
            atomic_store_explicit(&s_wifi_retry_count, next_retry, memory_order_relaxed);
            ESP_LOGW(TAG, "Wi-Fi disconnected; retry %u/%u",
                     (unsigned)next_retry,
                     (unsigned)max_retries);
            (void)esp_wifi_connect();
            return;
        }

        if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, AIQA_WIFI_FAILED_BIT);
        }
        atomic_store_explicit(&s_wifi_should_retry, false, memory_order_release);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        atomic_store_explicit(&s_wifi_retry_count, 0, memory_order_relaxed);
        ESP_LOGI(TAG, "Wi-Fi got IP");
        if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, AIQA_WIFI_CONNECTED_BIT);
        }
    }
}

static esp_err_t cleanup_partial_wifi_initialization(void)
{
    esp_err_t first_error = ESP_OK;
    if (s_ip_event_instance != NULL) {
        esp_err_t ret = esp_event_handler_instance_unregister(
            IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_event_instance);
        if (ret == ESP_OK) {
            s_ip_event_instance = NULL;
        } else {
            first_error = ret;
        }
    }
    if (s_wifi_event_instance != NULL) {
        esp_err_t ret = esp_event_handler_instance_unregister(
            WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_event_instance);
        if (ret == ESP_OK) {
            s_wifi_event_instance = NULL;
        } else if (first_error == ESP_OK) {
            first_error = ret;
        }
    }
    if (s_wifi_driver_initialized) {
        esp_err_t ret = esp_wifi_deinit();
        if (ret == ESP_OK) {
            s_wifi_driver_initialized = false;
            s_ip_event_instance = NULL;
            s_wifi_event_instance = NULL;
        } else if (first_error == ESP_OK) {
            first_error = ret;
        }
    }
    if (!s_wifi_driver_initialized) {
        if (s_wifi_netif != NULL) {
            esp_netif_destroy_default_wifi(s_wifi_netif);
            s_wifi_netif = NULL;
        }
        if (s_wifi_event_group != NULL) {
            vEventGroupDelete(s_wifi_event_group);
            s_wifi_event_group = NULL;
        }
        s_wifi_initialized = false;
    }
    return first_error;
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

    if (s_wifi_event_group != NULL || s_wifi_netif != NULL ||
        s_wifi_event_instance != NULL || s_ip_event_instance != NULL ||
        s_wifi_driver_initialized) {
        ret = cleanup_partial_wifi_initialization();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_wifi_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi_netif == NULL) {
        (void)cleanup_partial_wifi_initialization();
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&wifi_init_config);
    if (ret != ESP_OK) {
        goto fail;
    }
    s_wifi_driver_initialized = true;

    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ret != ESP_OK) {
        goto fail;
    }

    ret = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &s_wifi_event_instance);
    if (ret != ESP_OK) {
        goto fail;
    }

    ret = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, &s_ip_event_instance);
    if (ret != ESP_OK) {
        goto fail;
    }

    s_wifi_initialized = true;
    return ESP_OK;

fail:
    {
        const esp_err_t cleanup_ret = cleanup_partial_wifi_initialization();
        return cleanup_ret == ESP_OK ? ret : cleanup_ret;
    }
}

static void secure_zero(void *value, size_t value_size)
{
    volatile unsigned char *bytes = value;
    while (value_size > 0) {
        *bytes++ = 0;
        --value_size;
    }
}

static esp_err_t connect_wifi_with_policy(
    const aiqa_wifi_credentials_t *credentials,
    const aiqa_net_policy_t *policy)
{
    esp_err_t ret = ensure_wifi_initialized();
    if (ret != ESP_OK) {
        return ret;
    }

    wifi_config_t wifi_config = {0};
    size_t ssid_len = strlen(credentials->ssid);
    size_t password_len = strlen(credentials->password);
    if (ssid_len == 0 || ssid_len > sizeof(wifi_config.sta.ssid) ||
        password_len >= sizeof(wifi_config.sta.password)) {
        secure_zero(&wifi_config, sizeof(wifi_config));
        return ESP_ERR_INVALID_ARG;
    }
    (void)memcpy(wifi_config.sta.ssid, credentials->ssid, ssid_len);
    (void)memcpy(wifi_config.sta.password, credentials->password, password_len);
    wifi_config.sta.threshold.authmode =
        credentials->password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    xEventGroupClearBits(s_wifi_event_group, AIQA_WIFI_CONNECTED_BIT | AIQA_WIFI_FAILED_BIT);
    atomic_store_explicit(&s_wifi_retry_count, 0, memory_order_relaxed);
    atomic_store_explicit(&s_wifi_max_retries, policy->max_wifi_retries, memory_order_relaxed);

    atomic_store_explicit(&s_wifi_should_retry, false, memory_order_release);
    ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
        secure_zero(&wifi_config, sizeof(wifi_config));
        return ret;
    }
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret == ESP_OK) {
        ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    }
    if (ret == ESP_OK) {
        atomic_store_explicit(&s_wifi_should_retry, true, memory_order_release);
        ret = esp_wifi_start();
    }
    secure_zero(&wifi_config, sizeof(wifi_config));
    if (ret != ESP_OK) {
        atomic_store_explicit(&s_wifi_should_retry, false, memory_order_release);
        return ret;
    }

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

    atomic_store_explicit(&s_wifi_should_retry, false, memory_order_release);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t sync_time_with_policy(const aiqa_net_policy_t *policy)
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

esp_err_t aiqa_net_connect_wifi(
    const aiqa_wifi_credentials_t *credentials,
    const aiqa_net_policy_t *policy)
{
    if (credentials == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    aiqa_secret_status_t secret_status = aiqa_wifi_credentials_validate(credentials);
    if (secret_status != AIQA_SECRET_OK) {
        ESP_LOGE(TAG, "Cannot connect Wi-Fi; credential validation failed: %s",
                 aiqa_secret_status_name(secret_status));
        return ESP_ERR_INVALID_ARG;
    }

    aiqa_net_policy_t default_policy = aiqa_net_default_policy();
    const aiqa_net_policy_t *active_policy = policy != NULL ? policy : &default_policy;
    return connect_wifi_with_policy(credentials, active_policy);
}

esp_err_t aiqa_net_sync_time(const aiqa_net_policy_t *policy)
{
    aiqa_net_policy_t default_policy = aiqa_net_default_policy();
    const aiqa_net_policy_t *active_policy = policy != NULL ? policy : &default_policy;
    return sync_time_with_policy(active_policy);
}

esp_err_t aiqa_net_disconnect_wifi(void)
{
    if (!s_wifi_initialized) {
        return ESP_OK;
    }
    atomic_store_explicit(&s_wifi_should_retry, false, memory_order_release);
    if (s_wifi_event_group != NULL) {
        xEventGroupClearBits(s_wifi_event_group, AIQA_WIFI_CONNECTED_BIT | AIQA_WIFI_FAILED_BIT);
    }
    (void)esp_wifi_disconnect();
    esp_err_t ret = esp_wifi_stop();
    return ret == ESP_ERR_WIFI_NOT_STARTED ? ESP_OK : ret;
}

esp_err_t aiqa_net_forget_wifi(void)
{
    const esp_err_t disconnect_ret = aiqa_net_disconnect_wifi();
    const esp_err_t cleanup_ret = cleanup_partial_wifi_initialization();
    return cleanup_ret != ESP_OK ? cleanup_ret : disconnect_ret;
}

static bool transaction_connect(void *context, const aiqa_wifi_credentials_t *credentials)
{
    aiqa_net_transaction_adapter_t *adapter = context;
    return adapter != NULL && aiqa_net_connect_wifi(credentials, &adapter->policy) == ESP_OK;
}

static void transaction_quarantine(void *context)
{
    (void)context;
    (void)aiqa_net_disconnect_wifi();
}

void aiqa_net_transaction_adapter_init(
    aiqa_net_transaction_adapter_t *adapter,
    const aiqa_net_policy_t *policy)
{
    if (adapter == NULL) {
        return;
    }
    adapter->policy = policy != NULL ? *policy : aiqa_net_default_policy();
}

aiqa_config_network_ports_t aiqa_net_transaction_ports(
    aiqa_net_transaction_adapter_t *adapter)
{
    aiqa_config_network_ports_t ports = {
        .context = adapter,
        .trial_connect = transaction_connect,
        .restore_connect = transaction_connect,
        .quarantine = transaction_quarantine,
    };
    return ports;
}

esp_err_t aiqa_net_connect_wifi_and_sync_time(
    const aiqa_wifi_credentials_t *credentials,
    const aiqa_net_policy_t *policy)
{
    esp_err_t ret = aiqa_net_connect_wifi(credentials, policy);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connect failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = aiqa_net_sync_time(policy);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SNTP sync failed: %s", esp_err_to_name(ret));
    }
    return ret;
}
