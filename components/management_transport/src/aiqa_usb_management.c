#include "aiqa_usb_management.h"

#include "aiqa_management_protocol.h"
#include "aiqa_management_wire.h"

#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AIQA_USB_RX_CHUNK_SIZE 256U
#define AIQA_USB_TX_BUFFER_SIZE 1024U
#define AIQA_USB_RX_BUFFER_SIZE 4096U
#define AIQA_USB_TASK_STACK_SIZE 4096U
#define AIQA_USB_TASK_PRIORITY 5U
#define AIQA_USB_REQUESTS_PER_SECOND 10U
#define AIQA_USB_RX_BYTES_PER_SECOND 12288U
#define AIQA_USB_WARNINGS_PER_SECOND 1U

static const char *TAG = "aiqa_usb_mgmt";
static TaskHandle_t s_management_task;

typedef struct {
    TickType_t request_window_started;
    uint32_t requests_in_window;
    size_t received_bytes_in_window;
    uint32_t warnings_in_window;
} aiqa_usb_request_gate_t;

static void secure_zero(void *value, size_t value_size)
{
    volatile uint8_t *bytes = value;
    while (value_size > 0) {
        *bytes++ = 0;
        --value_size;
    }
}

static void refresh_gate_window(aiqa_usb_request_gate_t *gate)
{
    const TickType_t now = xTaskGetTickCount();
    const TickType_t one_second = pdMS_TO_TICKS(1000);
    if ((now - gate->request_window_started) >= one_second) {
        gate->request_window_started = now;
        gate->requests_in_window = 0;
        gate->received_bytes_in_window = 0;
        gate->warnings_in_window = 0;
    }
}

static bool request_is_allowed(aiqa_usb_request_gate_t *gate)
{
    refresh_gate_window(gate);
    if (gate->requests_in_window >= AIQA_USB_REQUESTS_PER_SECOND) {
        return false;
    }
    gate->requests_in_window += 1U;
    return true;
}

static bool input_bytes_are_allowed(
    aiqa_usb_request_gate_t *gate,
    size_t data_length)
{
    refresh_gate_window(gate);
    if (data_length > AIQA_USB_RX_BYTES_PER_SECOND -
                          gate->received_bytes_in_window) {
        return false;
    }
    gate->received_bytes_in_window += data_length;
    return true;
}

static void log_warning_if_allowed(
    aiqa_usb_request_gate_t *gate,
    const char *message)
{
    refresh_gate_window(gate);
    if (gate->warnings_in_window < AIQA_USB_WARNINGS_PER_SECOND) {
        gate->warnings_in_window += 1U;
        ESP_LOGW(TAG, "%s", message);
    }
}

static void handle_frame(
    void *context,
    aiqa_management_wire_kind_t kind,
    const uint8_t *payload,
    size_t payload_length)
{
    aiqa_usb_request_gate_t *gate = context;
    if (kind != AIQA_MANAGEMENT_WIRE_REQUEST || !request_is_allowed(gate)) {
        return;
    }

    uint8_t response_frame[
        AIQA_MANAGEMENT_WIRE_HEADER_SIZE +
        AIQA_MANAGEMENT_PROTOCOL_MAX_RESPONSE] = {0};
    char *response =
        (char *)(response_frame + AIQA_MANAGEMENT_WIRE_HEADER_SIZE);
    size_t response_length = 0;
    if (!aiqa_management_protocol_handle_public_request(
            payload,
            payload_length,
            response,
            AIQA_MANAGEMENT_PROTOCOL_MAX_RESPONSE,
            &response_length) ||
        !aiqa_management_wire_encode_header(
            AIQA_MANAGEMENT_WIRE_RESPONSE,
            response_length,
            response_frame,
            AIQA_MANAGEMENT_WIRE_HEADER_SIZE)) {
        log_warning_if_allowed(gate, "Management reply could not be prepared");
    } else {
        const size_t frame_length =
            AIQA_MANAGEMENT_WIRE_HEADER_SIZE + response_length;
        const int written = usb_serial_jtag_write_bytes(
            response_frame, frame_length, pdMS_TO_TICKS(100));
        if (written != (int)frame_length) {
            log_warning_if_allowed(gate, "Management reply could not be sent");
        }
    }

    secure_zero(response_frame, sizeof(response_frame));
}

static void management_task(void *argument)
{
    aiqa_management_wire_decoder_t *decoder = argument;
    aiqa_usb_request_gate_t gate = {
        .request_window_started = xTaskGetTickCount(),
        .requests_in_window = 0,
        .received_bytes_in_window = 0,
        .warnings_in_window = 0,
    };
    uint8_t input[AIQA_USB_RX_CHUNK_SIZE] = {0};

    for (;;) {
        const int bytes_read = usb_serial_jtag_read_bytes(
            input, sizeof(input), pdMS_TO_TICKS(250));
        if (bytes_read <= 0) {
            continue;
        }

        if (!input_bytes_are_allowed(&gate, (size_t)bytes_read)) {
            secure_zero(input, (size_t)bytes_read);
            aiqa_management_wire_decoder_secure_clear(decoder);
            log_warning_if_allowed(&gate, "Management input rate exceeded");
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }

        const bool accepted = aiqa_management_wire_decoder_feed(
            decoder,
            input,
            (size_t)bytes_read,
            handle_frame,
            &gate);
        secure_zero(input, (size_t)bytes_read);
        if (!accepted) {
            log_warning_if_allowed(&gate, "Rejected malformed management frame");
        }
    }
}

esp_err_t aiqa_usb_management_start(void)
{
    if (s_management_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    usb_serial_jtag_driver_config_t driver_config = {
        .tx_buffer_size = AIQA_USB_TX_BUFFER_SIZE,
        .rx_buffer_size = AIQA_USB_RX_BUFFER_SIZE,
    };
    esp_err_t error = usb_serial_jtag_driver_install(&driver_config);
    if (error != ESP_OK) {
        return error;
    }

    aiqa_management_wire_decoder_t *decoder = heap_caps_calloc(
        1,
        sizeof(*decoder),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (decoder == NULL) {
        (void)usb_serial_jtag_driver_uninstall();
        return ESP_ERR_NO_MEM;
    }
    aiqa_management_wire_decoder_init(decoder);

    if (xTaskCreate(
            management_task,
            "aiqa_usb_mgmt",
            AIQA_USB_TASK_STACK_SIZE,
            decoder,
            AIQA_USB_TASK_PRIORITY,
            &s_management_task) != pdPASS) {
        aiqa_management_wire_decoder_secure_clear(decoder);
        heap_caps_free(decoder);
        (void)usb_serial_jtag_driver_uninstall();
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "USB management hello gate ready");
    return ESP_OK;
}
