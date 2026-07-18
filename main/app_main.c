#include "aiqa_runtime.h"
#include "aiqa_management_access.h"
#include "aiqa_pairing_esp_platform.h"
#include "aiqa_usb_management.h"

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "ai_pet_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Waveshare AI Pet runtime");
    aiqa_management_access_global_init();
    const esp_err_t pairing_entropy_error = aiqa_pairing_esp_entropy_init();
    if (pairing_entropy_error != ESP_OK) {
        ESP_LOGE(TAG, "Pairing entropy unavailable; management stays locked");
    }

    const esp_err_t nvs_error = nvs_flash_init();
    if (nvs_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "NVS unavailable; refusing destructive automatic recovery: %s",
            esp_err_to_name(nvs_error));
    }

    const esp_err_t management_error = aiqa_usb_management_start();
    if (management_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "USB management channel unavailable: %s",
            esp_err_to_name(management_error));
    }

    const esp_err_t runtime_error = aiqa_runtime_start();
    if (runtime_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Device runtime unavailable; management diagnostics stay online: %s",
            esp_err_to_name(runtime_error));
    }
}
