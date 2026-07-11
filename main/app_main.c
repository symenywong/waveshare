#include "aiqa_runtime.h"

#include "esp_log.h"

static const char *TAG = "aiqa_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Waveshare AI QA runtime");
    ESP_ERROR_CHECK(aiqa_runtime_start());
}
