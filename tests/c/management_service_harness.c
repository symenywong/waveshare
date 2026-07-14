#include "aiqa_management_service.h"

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static atomic_bool s_block_status = ATOMIC_VAR_INIT(false);
static atomic_bool s_status_entered = ATOMIC_VAR_INIT(false);
static atomic_bool s_release_status = ATOMIC_VAR_INIT(false);
static const char FIXTURE_WIFI_PASSWORD[] = {
    'f', 'i', 'x', 't', 'u', 'r', 'e', '-', 'p', 'a', 's', 's', 'w', 'o', 'r', 'd', '\0'};
static const char INVALID_SHORT_PASSWORD[] = {'s', 'h', 'o', 'r', 't', '\0'};
static const char SENTINEL_PREFIX[] = {
    's', 'e', 'c', 'r', 'e', 't', '-', 's', 'e', 'n', 't', 'i', 'n', 'e', 'l', '\0'};
static const char SENTINEL_WIFI_PASSWORD[] = {
    's', 'e', 'c', 'r', 'e', 't', '-', 's', 'e', 'n', 't', 'i', 'n', 'e', 'l',
    '-', 'p', 'a', 's', 's', 'w', 'o', 'r', 'd', '\0'};
static const char SENTINEL_CHAT_KEY[] = {
    's', 'e', 'c', 'r', 'e', 't', '-', 's', 'e', 'n', 't', 'i', 'n', 'e', 'l',
    '-', 'c', 'h', 'a', 't', '-', 'k', 'e', 'y', '\0'};
static const char SENTINEL_ASR_KEY[] = {
    's', 'e', 'c', 'r', 'e', 't', '-', 's', 'e', 'n', 't', 'i', 'n', 'e', 'l',
    '-', 'a', 's', 'r', '-', 'k', 'e', 'y', '\0'};

typedef struct {
    aiqa_management_device_status_t status;
    aiqa_management_public_config_t public_config;
    bool status_ok;
    bool config_ok;
    bool submit_ok;
    bool authorize_read;
    bool authorize_manage;
    unsigned submit_count;
    uint32_t submitted_operation_id;
    aiqa_management_owned_wifi_update_t submitted_update;
} fake_context_t;

static bool authorize(
    void *opaque,
    uint32_t session_id,
    aiqa_management_capability_t capability)
{
    fake_context_t *context = opaque;
    if (session_id != 7U) {
        return false;
    }
    return capability == AIQA_MANAGEMENT_CAPABILITY_READ
               ? context->authorize_read
               : context->authorize_manage;
}

static bool copy_status(void *opaque, aiqa_management_device_status_t *out_status)
{
    fake_context_t *context = opaque;
    if (!context->status_ok) {
        return false;
    }
    if (atomic_load_explicit(&s_block_status, memory_order_acquire)) {
        atomic_store_explicit(&s_status_entered, true, memory_order_release);
        while (!atomic_load_explicit(&s_release_status, memory_order_acquire)) {
            sched_yield();
        }
    }
    *out_status = context->status;
    return true;
}

static bool copy_public_config(void *opaque, aiqa_management_public_config_t *out_config)
{
    fake_context_t *context = opaque;
    if (!context->config_ok) {
        return false;
    }
    *out_config = context->public_config;
    return true;
}

static aiqa_management_result_t submit_wifi(
    void *opaque,
    uint32_t operation_id,
    const aiqa_management_owned_wifi_update_t *update)
{
    fake_context_t *context = opaque;
    context->submit_count += 1;
    context->submitted_operation_id = operation_id;
    context->submitted_update = *update;
    return context->submit_ok ? AIQA_MANAGEMENT_OK : AIQA_MANAGEMENT_INTERNAL_ERROR;
}

static aiqa_management_ports_t make_ports(fake_context_t *context)
{
    return (aiqa_management_ports_t){
        .context = context,
        .authorize = authorize,
        .copy_status = copy_status,
        .copy_public_config = copy_public_config,
        .submit_wifi = submit_wifi,
    };
}

static fake_context_t make_context(void)
{
    fake_context_t context = {
        .status = {
            .state = AIQA_STATE_IDLE,
            .error = AIQA_ERROR_NONE,
            .sequence = 8,
            .wifi = {.connected = true, .rssi_available = true, .rssi_dbm = -48},
            .config = {
                .available = true,
                .revision = 4,
                .chat_provider = "dashscope_openai_chat",
                .chat_model = "qwen3.7-max",
                .has_chat_api_key = true,
                .has_asr_api_key = true,
            },
        },
        .public_config = {
            .revision = 4,
            .wifi = {.revision = 4, .ssid = "lab-wifi", .has_password = true},
            .chat_provider = "dashscope_openai_chat",
            .chat_model = "qwen3.7-max",
            .has_chat_api_key = true,
            .has_asr_api_key = true,
        },
        .status_ok = true,
        .config_ok = true,
        .submit_ok = true,
        .authorize_read = true,
        .authorize_manage = true,
    };
    return context;
}

static aiqa_management_service_t make_service(fake_context_t *context)
{
    aiqa_management_service_t service = {0};
    aiqa_management_ports_t ports = make_ports(context);
    assert(aiqa_management_service_init(&service, &ports));
    return service;
}

static bool contains_bytes(
    const void *value,
    size_t value_size,
    const char *needle)
{
    const unsigned char *bytes = value;
    const size_t needle_size = strlen(needle);
    if (needle_size == 0 || needle_size > value_size) {
        return false;
    }
    for (size_t offset = 0; offset <= value_size - needle_size; ++offset) {
        if (memcmp(bytes + offset, needle, needle_size) == 0) {
            return true;
        }
    }
    return false;
}

static void run_read(void)
{
    fake_context_t context = make_context();
    aiqa_management_service_t service = make_service(&context);
    const aiqa_management_security_context_t access = {.session_id = 7};

    aiqa_management_device_status_t status = {0};
    assert(aiqa_management_service_get_status(&service, &access, &status) ==
           AIQA_MANAGEMENT_OK);
    assert(status.state == AIQA_STATE_IDLE);
    assert(status.sequence == 8);
    assert(status.config.revision == 4);
    assert(status.latest_operation.state == AIQA_MANAGEMENT_OPERATION_NONE);

    aiqa_management_public_config_t config = {0};
    assert(aiqa_management_service_get_public_config(&service, &access, &config) ==
           AIQA_MANAGEMENT_OK);
    assert(config.revision == 4);
    assert(strcmp(config.wifi.ssid, "lab-wifi") == 0);
    assert(config.has_chat_api_key);
}

static void run_security(void)
{
    fake_context_t context = make_context();
    aiqa_management_service_t service = make_service(&context);
    const aiqa_management_security_context_t denied = {0};
    aiqa_management_device_status_t status = {0};
    aiqa_management_public_config_t config = {0};
    assert(aiqa_management_service_get_status(&service, &denied, &status) ==
           AIQA_MANAGEMENT_FORBIDDEN);
    assert(aiqa_management_service_get_public_config(&service, &denied, &config) ==
           AIQA_MANAGEMENT_FORBIDDEN);

    aiqa_management_owned_wifi_update_t update = {
        .base_revision = 4,
        .password_action = AIQA_WIFI_PASSWORD_CLEAR,
        .ssid = "guest",
    };
    uint32_t operation_id = 99;
    context.authorize_read = true;
    context.authorize_manage = false;
    const aiqa_management_security_context_t read_only = {.session_id = 7};
    assert(aiqa_management_service_submit_wifi_update(
               &service, &read_only, &update, &operation_id) ==
           AIQA_MANAGEMENT_FORBIDDEN);
    assert(operation_id == 0);
    assert(context.submit_count == 0);
}

static void run_submit(void)
{
    fake_context_t context = make_context();
    aiqa_management_service_t service = make_service(&context);
    const aiqa_management_security_context_t access = {.session_id = 7};
    aiqa_management_owned_wifi_update_t update = {
        .base_revision = 4,
        .password_action = AIQA_WIFI_PASSWORD_REPLACE,
        .ssid = "new-network",
    };
    (void)memcpy(update.password, FIXTURE_WIFI_PASSWORD, sizeof(FIXTURE_WIFI_PASSWORD));
    uint32_t operation_id = 0;
    assert(aiqa_management_service_submit_wifi_update(
               &service, &access, &update, &operation_id) == AIQA_MANAGEMENT_OK);
    assert(operation_id == 1);
    assert(context.submit_count == 1);
    assert(context.submitted_operation_id == operation_id);
    assert(strcmp(context.submitted_update.password, FIXTURE_WIFI_PASSWORD) == 0);

    uint32_t competing_id = 0;
    assert(aiqa_management_service_submit_wifi_update(
               &service, &access, &update, &competing_id) == AIQA_MANAGEMENT_BUSY);
    assert(competing_id == 0);
    assert(context.submit_count == 1);

    assert(aiqa_management_service_complete_wifi_update(
        &service, operation_id, AIQA_MANAGEMENT_WIFI_UNREACHABLE_ROLLED_BACK));
    aiqa_management_device_status_t status = {0};
    assert(aiqa_management_service_get_status(&service, &access, &status) ==
           AIQA_MANAGEMENT_OK);
    assert(status.latest_operation.id == operation_id);
    assert(status.latest_operation.state == AIQA_MANAGEMENT_OPERATION_FAILED);
    assert(status.latest_operation.result ==
           AIQA_MANAGEMENT_WIFI_UNREACHABLE_ROLLED_BACK);
}

static void run_validation(void)
{
    fake_context_t context = make_context();
    aiqa_management_service_t service = make_service(&context);
    const aiqa_management_security_context_t access = {.session_id = 7};
    aiqa_management_owned_wifi_update_t update = {
        .base_revision = 4,
        .password_action = AIQA_WIFI_PASSWORD_REPLACE,
        .ssid = "network",
    };
    (void)memcpy(update.password, INVALID_SHORT_PASSWORD, sizeof(INVALID_SHORT_PASSWORD));
    uint32_t operation_id = 0;
    assert(aiqa_management_service_submit_wifi_update(
               &service, &access, &update, &operation_id) ==
           AIQA_MANAGEMENT_INVALID_REQUEST);
    update.password_action = AIQA_WIFI_PASSWORD_KEEP;
    (void)snprintf(update.password, sizeof(update.password), "%s", "unexpected");
    assert(aiqa_management_service_submit_wifi_update(
               &service, &access, &update, &operation_id) ==
           AIQA_MANAGEMENT_INVALID_REQUEST);
    update.password[0] = '\0';
    update.ssid[0] = '\0';
    assert(aiqa_management_service_submit_wifi_update(
               &service, &access, &update, &operation_id) ==
           AIQA_MANAGEMENT_INVALID_REQUEST);
    assert(context.submit_count == 0);
}

static void run_projection(void)
{
    aiqa_config_snapshot_t snapshot = {
        .config = aiqa_config_default(),
        .revision = 9,
        .namespace_found = true,
    };
    (void)snprintf(snapshot.secrets.wifi_ssid,
                   sizeof(snapshot.secrets.wifi_ssid), "%s", "private-wifi");
    (void)snprintf(snapshot.secrets.wifi_password,
                   sizeof(snapshot.secrets.wifi_password), "%s", SENTINEL_WIFI_PASSWORD);
    (void)snprintf(snapshot.secrets.chat_api_key,
                   sizeof(snapshot.secrets.chat_api_key), "%s", SENTINEL_CHAT_KEY);
    (void)snprintf(snapshot.secrets.asr_api_key,
                   sizeof(snapshot.secrets.asr_api_key), "%s", SENTINEL_ASR_KEY);
    snapshot.user_prefs.volume_percent = 42;
    snapshot.user_prefs.assistant_profile = aiqa_assistant_profile_default();

    aiqa_management_public_config_t public_config = {0};
    assert(aiqa_management_public_config_from_snapshot(&snapshot, &public_config));
    assert(public_config.revision == 9);
    assert(strcmp(public_config.wifi.ssid, "private-wifi") == 0);
    assert(public_config.wifi.has_password);
    assert(public_config.has_chat_api_key);
    assert(public_config.has_asr_api_key);

    assert(!contains_bytes(&public_config, sizeof(public_config), SENTINEL_PREFIX));
}

typedef struct {
    aiqa_management_service_t *service;
    aiqa_management_security_context_t access;
    aiqa_management_device_status_t status;
    aiqa_management_result_t result;
} read_thread_args_t;

static void *read_status_thread(void *opaque)
{
    read_thread_args_t *args = opaque;
    args->result = aiqa_management_service_get_status(
        args->service, &args->access, &args->status);
    return NULL;
}

static void run_concurrency(void)
{
    fake_context_t context = make_context();
    aiqa_management_service_t service = make_service(&context);
    const aiqa_management_security_context_t access = {.session_id = 7};
    aiqa_management_owned_wifi_update_t update = {
        .base_revision = 4,
        .password_action = AIQA_WIFI_PASSWORD_CLEAR,
        .ssid = "guest",
    };
    uint32_t operation_id = 0;
    assert(aiqa_management_service_submit_wifi_update(
               &service, &access, &update, &operation_id) == AIQA_MANAGEMENT_OK);

    atomic_store_explicit(&s_status_entered, false, memory_order_release);
    atomic_store_explicit(&s_release_status, false, memory_order_release);
    atomic_store_explicit(&s_block_status, true, memory_order_release);
    read_thread_args_t args = {.service = &service, .access = access};
    pthread_t thread;
    assert(pthread_create(&thread, NULL, read_status_thread, &args) == 0);
    while (!atomic_load_explicit(&s_status_entered, memory_order_acquire)) {
        sched_yield();
    }

    assert(aiqa_management_service_complete_wifi_update(
        &service, operation_id, AIQA_MANAGEMENT_OK));
    atomic_store_explicit(&s_release_status, true, memory_order_release);
    assert(pthread_join(thread, NULL) == 0);
    atomic_store_explicit(&s_block_status, false, memory_order_release);
    assert(args.result == AIQA_MANAGEMENT_OK);
    assert(args.status.latest_operation.id == operation_id);
    assert(args.status.latest_operation.state == AIQA_MANAGEMENT_OPERATION_SUCCEEDED);
}

static void run_edges(void)
{
    fake_context_t context = make_context();
    aiqa_management_ports_t ports = make_ports(&context);
    aiqa_management_service_t service = {0};
    const aiqa_management_security_context_t access = {.session_id = 7};
    aiqa_management_device_status_t status = {0};
    aiqa_management_public_config_t config = {0};
    aiqa_management_owned_wifi_update_t update = {
        .base_revision = 4,
        .password_action = AIQA_WIFI_PASSWORD_CLEAR,
        .ssid = "guest",
    };
    uint32_t operation_id = 0;

    assert(!aiqa_management_service_init(NULL, &ports));
    assert(!aiqa_management_service_init(&service, NULL));
    ports.copy_status = NULL;
    assert(!aiqa_management_service_init(&service, &ports));
    assert(aiqa_management_service_get_status(NULL, &access, &status) ==
           AIQA_MANAGEMENT_INVALID_REQUEST);
    assert(aiqa_management_service_get_status(&service, NULL, &status) ==
           AIQA_MANAGEMENT_INVALID_REQUEST);
    assert(aiqa_management_service_get_status(&service, &access, NULL) ==
           AIQA_MANAGEMENT_INVALID_REQUEST);
    assert(aiqa_management_service_get_status(&service, &access, &status) ==
           AIQA_MANAGEMENT_INVALID_REQUEST);
    assert(aiqa_management_service_get_public_config(NULL, &access, &config) ==
           AIQA_MANAGEMENT_INVALID_REQUEST);
    assert(aiqa_management_service_get_public_config(&service, NULL, &config) ==
           AIQA_MANAGEMENT_INVALID_REQUEST);
    assert(aiqa_management_service_get_public_config(&service, &access, NULL) ==
           AIQA_MANAGEMENT_INVALID_REQUEST);
    assert(aiqa_management_service_submit_wifi_update(
               NULL, &access, &update, &operation_id) == AIQA_MANAGEMENT_INVALID_REQUEST);
    assert(aiqa_management_service_submit_wifi_update(
               &service, NULL, &update, &operation_id) == AIQA_MANAGEMENT_INVALID_REQUEST);
    assert(aiqa_management_service_submit_wifi_update(
               &service, &access, NULL, &operation_id) == AIQA_MANAGEMENT_INVALID_REQUEST);
    assert(aiqa_management_service_submit_wifi_update(
               &service, &access, &update, NULL) == AIQA_MANAGEMENT_INVALID_REQUEST);
    assert(!aiqa_management_service_complete_wifi_update(
        &service, 1, AIQA_MANAGEMENT_OK));

    service = make_service(&context);
    context.status_ok = false;
    context.config_ok = false;
    assert(aiqa_management_service_get_status(&service, &access, &status) ==
           AIQA_MANAGEMENT_NOT_READY);
    assert(aiqa_management_service_get_public_config(&service, &access, &config) ==
           AIQA_MANAGEMENT_NOT_READY);
    context.submit_ok = false;
    assert(aiqa_management_service_submit_wifi_update(
               &service, &access, &update, &operation_id) ==
           AIQA_MANAGEMENT_INTERNAL_ERROR);
    context.submit_ok = true;
    assert(aiqa_management_service_submit_wifi_update(
               &service, &access, &update, &operation_id) == AIQA_MANAGEMENT_OK);
    assert(!aiqa_management_service_complete_wifi_update(
        &service, operation_id + 1U, AIQA_MANAGEMENT_OK));
    assert(aiqa_management_service_complete_wifi_update(
        &service, operation_id, AIQA_MANAGEMENT_OK));

    assert(!aiqa_management_public_config_from_snapshot(NULL, &config));
    assert(!aiqa_management_public_config_from_snapshot(
        &(aiqa_config_snapshot_t){0}, &config));
    aiqa_management_owned_wifi_update_secure_clear(&update);
    assert(update.ssid[0] == '\0');

    const aiqa_management_result_t results[] = {
        AIQA_MANAGEMENT_OK,
        AIQA_MANAGEMENT_INVALID_REQUEST,
        AIQA_MANAGEMENT_FORBIDDEN,
        AIQA_MANAGEMENT_NOT_READY,
        AIQA_MANAGEMENT_REVISION_CONFLICT,
        AIQA_MANAGEMENT_REVISION_EXHAUSTED,
        AIQA_MANAGEMENT_BUSY,
        AIQA_MANAGEMENT_WIFI_UNREACHABLE_ROLLED_BACK,
        AIQA_MANAGEMENT_PERSISTENCE_FAILED_ROLLED_BACK,
        AIQA_MANAGEMENT_RECOVERY_REQUIRED,
        AIQA_MANAGEMENT_CANCELLED,
        AIQA_MANAGEMENT_INTERNAL_ERROR,
    };
    for (size_t index = 0; index < sizeof(results) / sizeof(results[0]); ++index) {
        assert(strcmp(aiqa_management_result_name(results[index]), "UNKNOWN") != 0);
    }
    assert(strcmp(aiqa_management_result_name((aiqa_management_result_t)99), "UNKNOWN") == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "read") == 0) {
        run_read();
    } else if (strcmp(argv[1], "security") == 0) {
        run_security();
    } else if (strcmp(argv[1], "submit") == 0) {
        run_submit();
    } else if (strcmp(argv[1], "validation") == 0) {
        run_validation();
    } else if (strcmp(argv[1], "projection") == 0) {
        run_projection();
    } else if (strcmp(argv[1], "concurrency") == 0) {
        run_concurrency();
    } else if (strcmp(argv[1], "edges") == 0) {
        run_edges();
    } else {
        return 2;
    }
    return 0;
}
