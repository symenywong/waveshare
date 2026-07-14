#include "aiqa_runtime.h"
#include "aiqa_usb_management.h"

#include "esp_log.h"

static const char *TAG = "ai_pet_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Waveshare AI Pet runtime");
    ESP_ERROR_CHECK(aiqa_runtime_start());

    const esp_err_t management_error = aiqa_usb_management_start();
    if (management_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "USB management channel unavailable: %s",
            esp_err_to_name(management_error));
    }
}
