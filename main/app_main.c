#include "aiqa_runtime.h"

#include "esp_log.h"

static const char *TAG = "ai_pet_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Waveshare AI Pet runtime");
    ESP_ERROR_CHECK(aiqa_runtime_start());
}
