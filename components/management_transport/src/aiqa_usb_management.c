#include "aiqa_usb_management.h"

#include "aiqa_management_access.h"
#include "aiqa_management_protocol.h"
#include "aiqa_management_wire.h"
#include "aiqa_pairing_esp_nvs.h"
#include "aiqa_pairing_esp_platform.h"
#include "aiqa_pairing_rpc.h"
#include "aiqa_runtime.h"
#include "aiqa_secure_channel.h"

#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define AIQA_USB_RX_CHUNK_SIZE 256U
#define AIQA_USB_RX_DRIVER_BUFFER_SIZE 4096U
#define AIQA_USB_TX_DRIVER_BUFFER_SIZE 4096U
#define AIQA_USB_TASK_STACK_SIZE 7168U
#define AIQA_USB_TASK_PRIORITY 0U
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

typedef struct {
    aiqa_management_wire_decoder_t decoder;
    aiqa_usb_request_gate_t gate;
    aiqa_pairing_lifecycle_t *lifecycle;
    aiqa_pairing_rpc_t *pairing_rpc;
    aiqa_secure_channel_t *active_channel;
    aiqa_management_security_context_t access;
    uint64_t next_connection_id;
    uint64_t connection_id;
    uint64_t link_generation;
    bool connected;
    uint8_t tx_frame[AIQA_MANAGEMENT_WIRE_HEADER_SIZE + AIQA_SECURE_RECORD_MAX];
    uint8_t plaintext[AIQA_SECURE_PLAINTEXT_MAX];
    uint8_t secure_record[AIQA_SECURE_RECORD_MAX];
} aiqa_usb_owner_t;

static void secure_zero(void *value, size_t value_size)
{
    volatile uint8_t *bytes = value;
    while (value != NULL && value_size > 0U) {
        *bytes++ = 0;
        value_size -= 1U;
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
    if (gate->requests_in_window >= AIQA_USB_REQUESTS_PER_SECOND) return false;
    gate->requests_in_window += 1U;
    return true;
}

static bool input_bytes_are_allowed(aiqa_usb_request_gate_t *gate, size_t length)
{
    refresh_gate_window(gate);
    if (length > AIQA_USB_RX_BYTES_PER_SECOND - gate->received_bytes_in_window) {
        return false;
    }
    gate->received_bytes_in_window += length;
    return true;
}

static void log_warning_if_allowed(aiqa_usb_request_gate_t *gate, const char *message)
{
    refresh_gate_window(gate);
    if (gate->warnings_in_window < AIQA_USB_WARNINGS_PER_SECOND) {
        gate->warnings_in_window += 1U;
        ESP_LOGW(TAG, "%s", message);
    }
}

static bool show_pairing_code(
    void *context,
    const uint8_t code[AIQA_PAIRING_CODE_SIZE])
{
    (void)context;
    return aiqa_runtime_management_show_pairing_code(code);
}

static bool clear_pairing_code(void *context)
{
    (void)context;
    return aiqa_runtime_management_clear_pairing_code();
}

static void revoke_connection(void *context, uint64_t connection_id)
{
    aiqa_usb_owner_t *owner = context;
    aiqa_management_access_global_revoke();
    if (owner != NULL && owner->connection_id == connection_id) {
        aiqa_secure_channel_destroy(&owner->active_channel);
        secure_zero(&owner->access, sizeof(owner->access));
    }
}

static aiqa_management_result_t runtime_get_status(
    void *context,
    const aiqa_management_security_context_t *access,
    aiqa_management_device_status_t *out_status)
{
    (void)context;
    return aiqa_runtime_management_get_status(access, out_status);
}

static aiqa_management_result_t runtime_get_public_config(
    void *context,
    const aiqa_management_security_context_t *access,
    aiqa_management_public_config_t *out_config)
{
    (void)context;
    return aiqa_runtime_management_get_public_config(access, out_config);
}

static aiqa_management_result_t runtime_submit_wifi(
    void *context,
    const aiqa_management_security_context_t *access,
    const aiqa_management_owned_wifi_update_t *update,
    uint32_t *out_operation_id)
{
    (void)context;
    return aiqa_runtime_management_submit_wifi_update(
        access, update, out_operation_id);
}

static bool link_epoch_is_current(const aiqa_usb_owner_t *owner)
{
    return owner != NULL && owner->connected && owner->connection_id != 0U;
}

static bool send_payload(
    aiqa_usb_owner_t *owner,
    aiqa_management_wire_kind_t kind,
    const uint8_t *payload,
    size_t payload_length,
    bool wait_until_sent)
{
    if (!link_epoch_is_current(owner) ||
        payload_length > AIQA_SECURE_RECORD_MAX ||
        !aiqa_management_wire_encode_header(
            kind,
            payload_length,
            owner->tx_frame,
            AIQA_MANAGEMENT_WIRE_HEADER_SIZE)) {
        return false;
    }
    (void)memcpy(
        owner->tx_frame + AIQA_MANAGEMENT_WIRE_HEADER_SIZE,
        payload,
        payload_length);
    const size_t frame_length = AIQA_MANAGEMENT_WIRE_HEADER_SIZE + payload_length;
    size_t offset = 0;
    while (offset < frame_length) {
        if (!link_epoch_is_current(owner)) {
            secure_zero(owner->tx_frame, frame_length);
            return false;
        }
        const int written = usb_serial_jtag_write_bytes(
            owner->tx_frame + offset,
            frame_length - offset,
            pdMS_TO_TICKS(500));
        if (written <= 0) {
            secure_zero(owner->tx_frame, frame_length);
            return false;
        }
        offset += (size_t)written;
    }
    const bool sent =
        !wait_until_sent ||
        usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(500)) == ESP_OK;
    secure_zero(owner->tx_frame, frame_length);
    return sent && link_epoch_is_current(owner);
}

static void abort_active_connection(aiqa_usb_owner_t *owner)
{
    aiqa_management_access_global_revoke();
    aiqa_secure_channel_destroy(&owner->active_channel);
    secure_zero(&owner->access, sizeof(owner->access));
    if (owner->pairing_rpc != NULL && owner->connection_id != 0U) {
        aiqa_pairing_rpc_abort_connection(
            owner->pairing_rpc, owner->connection_id);
    }
    aiqa_management_wire_decoder_secure_clear(&owner->decoder);
    secure_zero(owner->plaintext, sizeof(owner->plaintext));
    secure_zero(owner->secure_record, sizeof(owner->secure_record));
}

static void handle_secure_request(
    aiqa_usb_owner_t *owner,
    aiqa_management_wire_kind_t kind,
    const uint8_t *payload,
    size_t payload_length)
{
    size_t plaintext_length = 0;
    if (kind != AIQA_MANAGEMENT_WIRE_REQUEST ||
        aiqa_secure_channel_decrypt(
            owner->active_channel,
            AIQA_SECURE_FRAME_REQUEST,
            payload,
            payload_length,
            owner->plaintext,
            sizeof(owner->plaintext),
            &plaintext_length) != AIQA_PAIRING_OK) {
        abort_active_connection(owner);
        return;
    }

    const aiqa_management_protocol_ports_t ports = {
        .context = NULL,
        .get_status = runtime_get_status,
        .get_public_config = runtime_get_public_config,
        .submit_wifi_update = runtime_submit_wifi,
    };
    size_t response_length = 0;
    char *response_json =
        (char *)(owner->tx_frame + AIQA_MANAGEMENT_WIRE_HEADER_SIZE);
    if (!aiqa_management_protocol_handle_authenticated_request(
            owner->plaintext,
            plaintext_length,
            &owner->access,
            &ports,
            response_json,
            AIQA_SECURE_PLAINTEXT_MAX,
            &response_length)) {
        abort_active_connection(owner);
        return;
    }
    size_t record_length = 0;
    if (aiqa_secure_channel_encrypt(
            owner->active_channel,
            AIQA_SECURE_FRAME_RESPONSE,
            (const uint8_t *)response_json,
            response_length,
            owner->secure_record,
            sizeof(owner->secure_record),
            &record_length) != AIQA_PAIRING_OK ||
        !send_payload(
            owner,
            AIQA_MANAGEMENT_WIRE_RESPONSE,
            owner->secure_record,
            record_length,
            false)) {
        abort_active_connection(owner);
        return;
    }
    secure_zero(response_json, response_length);
    secure_zero(owner->plaintext, sizeof(owner->plaintext));
    secure_zero(owner->secure_record, sizeof(owner->secure_record));
    if (aiqa_pairing_lifecycle_authenticated_activity(
            owner->lifecycle, owner->connection_id) !=
        AIQA_PAIRING_LIFECYCLE_OK) {
        abort_active_connection(owner);
    }
}

static bool copy_public_diagnostics(
    void *context,
    aiqa_management_public_diagnostics_t *out_diagnostics)
{
    (void)context;
    return aiqa_runtime_management_copy_public_diagnostics(out_diagnostics);
}

static void handle_public_request(
    aiqa_usb_owner_t *owner,
    aiqa_management_wire_kind_t kind,
    const uint8_t *payload,
    size_t payload_length)
{
    if (kind != AIQA_MANAGEMENT_WIRE_REQUEST) return;
    char *response = (char *)owner->plaintext;
    size_t response_length = 0;
    bool requires_tx_confirmation = false;
    bool produced = owner->pairing_rpc != NULL &&
                    aiqa_pairing_rpc_handle(
                        owner->pairing_rpc,
                        owner->connection_id,
                        payload,
                        payload_length,
                        response,
                        sizeof(owner->plaintext),
                        &response_length,
                        &requires_tx_confirmation);
    if (!produced) {
        const aiqa_management_public_protocol_ports_t public_ports = {
            .context = NULL,
            .copy_diagnostics = copy_public_diagnostics,
        };
        produced = aiqa_management_protocol_handle_public_request_with_ports(
            payload,
            payload_length,
            &public_ports,
            response,
            sizeof(owner->plaintext),
            &response_length);
    }
    if (!produced ||
        !send_payload(
            owner,
            AIQA_MANAGEMENT_WIRE_RESPONSE,
            (const uint8_t *)response,
            response_length,
            requires_tx_confirmation)) {
        secure_zero(owner->plaintext, sizeof(owner->plaintext));
        if (requires_tx_confirmation) abort_active_connection(owner);
        return;
    }
    secure_zero(owner->plaintext, sizeof(owner->plaintext));

    if (requires_tx_confirmation) {
        aiqa_secure_channel_t *channel = NULL;
        aiqa_management_security_context_t access = {0};
        if (!link_epoch_is_current(owner) ||
            aiqa_pairing_rpc_commit_finished(
                owner->pairing_rpc,
                owner->connection_id,
                &channel,
                &access) != AIQA_PAIRING_OK) {
            aiqa_secure_channel_destroy(&channel);
            secure_zero(&access, sizeof(access));
            abort_active_connection(owner);
            return;
        }
        owner->active_channel = channel;
        owner->access = access;
        secure_zero(&access, sizeof(access));
    }
}

static void handle_frame(
    void *context,
    aiqa_management_wire_kind_t kind,
    const uint8_t *payload,
    size_t payload_length)
{
    aiqa_usb_owner_t *owner = context;
    if (owner == NULL || !owner->connected ||
        !request_is_allowed(&owner->gate)) return;
    if (owner->active_channel != NULL) {
        handle_secure_request(owner, kind, payload, payload_length);
    } else {
        handle_public_request(owner, kind, payload, payload_length);
    }
}

static void process_local_action(aiqa_usb_owner_t *owner)
{
    if (owner->lifecycle == NULL) return;
    const aiqa_pairing_local_action_t action =
        aiqa_pairing_esp_pending_local_action();
    if (action == AIQA_PAIRING_LOCAL_START && owner->connected) {
        if (aiqa_pairing_lifecycle_local_open(
                owner->lifecycle, owner->connection_id) !=
            AIQA_PAIRING_LIFECYCLE_OK) {
            (void)aiqa_pairing_esp_consume_local_action(NULL, action);
        }
    } else if (action == AIQA_PAIRING_LOCAL_RESET) {
        if (aiqa_pairing_lifecycle_local_reset(owner->lifecycle) !=
            AIQA_PAIRING_LIFECYCLE_OK) {
            (void)aiqa_pairing_esp_consume_local_action(NULL, action);
        }
    } else if (action != 0) {
        (void)aiqa_pairing_esp_consume_local_action(NULL, action);
    }
}

static void management_task(void *argument)
{
    aiqa_usb_owner_t *owner = argument;
    uint8_t input[AIQA_USB_RX_CHUNK_SIZE] = {0};
    for (;;) {
        process_local_action(owner);
        if (owner->lifecycle != NULL &&
            aiqa_pairing_lifecycle_tick(owner->lifecycle) ==
                AIQA_PAIRING_LIFECYCLE_FAULTED) {
            abort_active_connection(owner);
        }
        const uint64_t read_generation = owner->link_generation;
        const int read = usb_serial_jtag_read_bytes(
            input, sizeof(input), pdMS_TO_TICKS(50));
        const size_t bytes_read = read > 0 ? (size_t)read : 0U;
        if (bytes_read == 0U) {
            vTaskDelay(1U);
            continue;
        }
        if (!link_epoch_is_current(owner) ||
            owner->link_generation != read_generation) {
            secure_zero(input, bytes_read);
            continue;
        }
        if (!input_bytes_are_allowed(&owner->gate, bytes_read)) {
            secure_zero(input, bytes_read);
            aiqa_management_wire_decoder_secure_clear(&owner->decoder);
            log_warning_if_allowed(&owner->gate, "Management input rate exceeded");
            continue;
        }
        const bool accepted = aiqa_management_wire_decoder_feed(
            &owner->decoder,
            input,
            bytes_read,
            handle_frame,
            owner);
        secure_zero(input, bytes_read);
        if (!accepted) {
            log_warning_if_allowed(
                &owner->gate, "Rejected malformed management frame");
        }
    }
}

esp_err_t aiqa_usb_management_start(void)
{
    if (s_management_task != NULL) return ESP_ERR_INVALID_STATE;
    aiqa_usb_owner_t *owner = heap_caps_calloc(
        1, sizeof(*owner), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (owner == NULL) {
        return ESP_ERR_NO_MEM;
    }

    usb_serial_jtag_driver_config_t usb_config = {
        .tx_buffer_size = AIQA_USB_TX_DRIVER_BUFFER_SIZE,
        .rx_buffer_size = AIQA_USB_RX_DRIVER_BUFFER_SIZE,
    };
    esp_err_t error = usb_serial_jtag_driver_install(&usb_config);
    if (error != ESP_OK) {
        secure_zero(owner, sizeof(*owner));
        heap_caps_free(owner);
        return error;
    }

    owner->gate.request_window_started = xTaskGetTickCount();
    aiqa_management_wire_decoder_init(&owner->decoder);
    owner->connected = true;
    owner->next_connection_id = 1U;
    owner->connection_id = 1U;
    owner->link_generation = 1U;

    const aiqa_pairing_lifecycle_ports_t lifecycle_ports = {
        .context = owner,
        .monotonic_ms = aiqa_pairing_esp_monotonic_ms,
        .random_fill = aiqa_pairing_esp_random_fill,
        .load_lock_record = aiqa_pairing_esp_nvs_load_lock_record,
        .store_lock_record = aiqa_pairing_esp_nvs_store_lock_record,
        .show_code = show_pairing_code,
        .clear_code = clear_pairing_code,
        .consume_local_presence = aiqa_pairing_esp_consume_local_action,
        .revoke_connection = revoke_connection,
    };
    const aiqa_pairing_lifecycle_policy_t policy =
        aiqa_pairing_lifecycle_default_policy();
    uint8_t device_id[AIQA_PAIRING_DEVICE_ID_MAX] = {0};
    const bool pairing_ready = aiqa_pairing_esp_copy_device_id(device_id) &&
                               aiqa_pairing_lifecycle_create(
                                   &owner->lifecycle,
                                   &policy,
                                   &lifecycle_ports) ==
                                   AIQA_PAIRING_LIFECYCLE_OK &&
                               aiqa_pairing_rpc_create(
                                   &owner->pairing_rpc,
                                   owner->lifecycle,
                                   aiqa_pairing_esp_random_fill,
                                   NULL,
                                   device_id,
                                   sizeof(device_id)) == AIQA_PAIRING_OK;
    secure_zero(device_id, sizeof(device_id));
    if (!pairing_ready) {
        aiqa_pairing_rpc_destroy(&owner->pairing_rpc);
        aiqa_pairing_lifecycle_destroy(&owner->lifecycle);
        ESP_LOGW(TAG, "Encrypted pairing unavailable; public hello remains ready");
    }

    if (xTaskCreate(
            management_task,
            "aiqa_usb_mgmt",
            AIQA_USB_TASK_STACK_SIZE,
            owner,
            AIQA_USB_TASK_PRIORITY,
            &s_management_task) != pdPASS) {
        aiqa_pairing_rpc_destroy(&owner->pairing_rpc);
        aiqa_pairing_lifecycle_destroy(&owner->lifecycle);
        (void)usb_serial_jtag_driver_uninstall();
        secure_zero(owner, sizeof(*owner));
        heap_caps_free(owner);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "USB encrypted management owner ready");
    return ESP_OK;
}
