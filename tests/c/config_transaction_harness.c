#include "aiqa_config_transaction.h"

#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

static const char k_test_new_wifi_password[] = {
    'n', 'e', 'w', '-', 'p', 'a', 's', 's', 'w', 'o', 'r', 'd', '\0'};
static const char k_test_unexpected_password_value[] = {
    'u', 'n', 'e', 'x', 'p', 'e', 'c', 't', 'e', 'd', '-', 'v', 'a', 'l', 'u', 'e', '\0'};

typedef struct {
    char trace[32];
    size_t trace_length;
    bool stage_ok;
    bool verify_ok;
    bool trial_ok;
    bool restore_ok;
    aiqa_config_activation_result_t activation_result;
    bool discard_ok;
    aiqa_config_slot_t discarded_slot;
    atomic_bool block_stage;
    atomic_bool stage_entered;
    atomic_bool release_stage;
    aiqa_config_record_t staged;
    aiqa_config_transaction_t *transaction;
    aiqa_wifi_update_t reentrant_request;
    aiqa_config_transaction_status_t reentrant_status;
    aiqa_config_transaction_read_status_t reentrant_read_status;
} fake_context_t;

static void trace(fake_context_t *context, char event)
{
    assert(context->trace_length + 1 < sizeof(context->trace));
    context->trace[context->trace_length++] = event;
    context->trace[context->trace_length] = '\0';
}

static bool fake_stage(void *opaque, aiqa_config_slot_t slot, const aiqa_config_record_t *candidate)
{
    fake_context_t *context = opaque;
    trace(context, 'W');
    assert(candidate != NULL);
    assert(slot == candidate->active_slot);
    context->staged = *candidate;
    if (atomic_load(&context->block_stage)) {
        atomic_store(&context->stage_entered, true);
        while (!atomic_load(&context->release_stage)) {
            sched_yield();
        }
    }
    return context->stage_ok;
}

static bool fake_verify(void *opaque, aiqa_config_slot_t slot, const aiqa_config_record_t *candidate)
{
    fake_context_t *context = opaque;
    trace(context, 'V');
    assert(slot == candidate->active_slot);
    assert(memcmp(&context->staged, candidate, sizeof(*candidate)) == 0);
    return context->verify_ok;
}

static bool fake_trial(void *opaque, const aiqa_wifi_credentials_t *credentials)
{
    fake_context_t *context = opaque;
    trace(context, 'T');
    assert(credentials != NULL);
    assert(strcmp(credentials->ssid, "new-wifi") == 0);
    if (context->transaction != NULL) {
        aiqa_config_record_t active = {0};
        context->reentrant_read_status = aiqa_config_transaction_copy_active(
            context->transaction, &active);
        context->reentrant_status = aiqa_config_transaction_apply_wifi(
            context->transaction, &context->reentrant_request, NULL);
    }
    return context->trial_ok;
}

static bool fake_restore(void *opaque, const aiqa_wifi_credentials_t *credentials)
{
    fake_context_t *context = opaque;
    trace(context, 'R');
    assert(credentials != NULL);
    assert(strcmp(credentials->ssid, "old-wifi") == 0);
    return context->restore_ok;
}

static void fake_quarantine(void *opaque)
{
    fake_context_t *context = opaque;
    trace(context, 'Q');
}

static aiqa_config_activation_result_t fake_activate(
    void *opaque,
    const aiqa_config_record_t *candidate,
    aiqa_config_slot_t expected_slot,
    uint32_t expected_revision)
{
    fake_context_t *context = opaque;
    trace(context, 'A');
    assert(candidate != NULL);
    assert(candidate->active_slot == AIQA_CONFIG_SLOT_B);
    assert(candidate->revision == 8);
    assert(expected_slot == AIQA_CONFIG_SLOT_A);
    assert(expected_revision == 7);
    return context->activation_result;
}

static bool fake_discard(void *opaque, aiqa_config_slot_t slot)
{
    fake_context_t *context = opaque;
    trace(context, 'D');
    context->discarded_slot = slot;
    return context->discard_ok;
}

static aiqa_config_record_t make_active(void)
{
    aiqa_config_record_t active = {
        .config = aiqa_config_default(),
        .revision = 7,
        .active_slot = AIQA_CONFIG_SLOT_A,
    };
    (void)snprintf(active.secrets.wifi_ssid, sizeof(active.secrets.wifi_ssid), "%s", "old-wifi");
    (void)snprintf(active.secrets.wifi_password, sizeof(active.secrets.wifi_password), "%s", "old-password");
    (void)snprintf(active.secrets.chat_api_key, sizeof(active.secrets.chat_api_key), "%s", "test-chat-key");
    (void)snprintf(active.secrets.asr_api_key, sizeof(active.secrets.asr_api_key), "%s", "test-asr-key");
    return active;
}

static fake_context_t make_context(void)
{
    fake_context_t context = {
        .stage_ok = true,
        .verify_ok = true,
        .trial_ok = true,
        .restore_ok = true,
        .activation_result = AIQA_CONFIG_ACTIVATION_COMMITTED,
        .discard_ok = true,
        .reentrant_status = AIQA_CONFIG_TRANSACTION_OK,
        .reentrant_read_status = AIQA_CONFIG_TRANSACTION_READ_OK,
    };
    return context;
}

static aiqa_config_transaction_ports_t make_ports(fake_context_t *context)
{
    aiqa_config_transaction_ports_t ports = {
        .storage = {
            .context = context,
            .stage = fake_stage,
            .verify = fake_verify,
            .activate = fake_activate,
            .discard = fake_discard,
        },
        .network = {
            .context = context,
            .trial_connect = fake_trial,
            .restore_connect = fake_restore,
            .quarantine = fake_quarantine,
        },
    };
    return ports;
}

static aiqa_wifi_update_t make_request(void)
{
    aiqa_wifi_update_t request = {
        .base_revision = 7,
        .ssid = "new-wifi",
        .password_action = AIQA_WIFI_PASSWORD_REPLACE,
        .password = k_test_new_wifi_password,
    };
    return request;
}

static void assert_active_unchanged(
    aiqa_config_transaction_t *transaction,
    const aiqa_config_record_t *before)
{
    aiqa_config_record_t active = {0};
    assert(aiqa_config_transaction_copy_active(transaction, &active) ==
           AIQA_CONFIG_TRANSACTION_READ_OK);
    assert(memcmp(&active, before, sizeof(*before)) == 0);
    aiqa_config_record_secure_clear(&active);
}

static void run_success(void)
{
    aiqa_config_record_t original = make_active();
    aiqa_config_record_t input = original;
    fake_context_t context = make_context();
    aiqa_config_transaction_ports_t ports = make_ports(&context);
    aiqa_config_transaction_t transaction;
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));

    aiqa_public_wifi_config_t public_view = {0};
    aiqa_wifi_update_t request = make_request();
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, &public_view) ==
           AIQA_CONFIG_TRANSACTION_OK);
    assert(strcmp(context.trace, "W V T A") != 0);
    assert(strcmp(context.trace, "WVTAD") == 0);
    assert(context.discarded_slot == AIQA_CONFIG_SLOT_A);
    assert(memcmp(&input, &original, sizeof(input)) == 0);

    aiqa_config_record_t active = {0};
    assert(aiqa_config_transaction_copy_active(&transaction, &active) ==
           AIQA_CONFIG_TRANSACTION_READ_OK);
    assert(active.revision == 8);
    assert(active.active_slot == AIQA_CONFIG_SLOT_B);
    assert(strcmp(active.secrets.wifi_ssid, "new-wifi") == 0);
    assert(strcmp(active.secrets.wifi_password, "new-password") == 0);
    assert(strcmp(active.secrets.chat_api_key, "test-chat-key") == 0);
    assert(memcmp(&active.config, &original.config, sizeof(active.config)) == 0);
    assert(public_view.revision == 8);
    assert(strcmp(public_view.ssid, "new-wifi") == 0);
    assert(public_view.has_password);
    aiqa_config_record_secure_clear(&active);
}

static void run_validation(void)
{
    aiqa_config_record_t input = make_active();
    fake_context_t context = make_context();
    aiqa_config_transaction_ports_t ports = make_ports(&context);
    aiqa_config_transaction_t transaction;
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));

    aiqa_wifi_update_t request = make_request();
    request.base_revision = 6;
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_REVISION_CONFLICT);
    assert(context.trace[0] == '\0');
    assert_active_unchanged(&transaction, &input);

    request.base_revision = 7;
    request.ssid = "";
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_SSID);
    assert(context.trace[0] == '\0');
    assert_active_unchanged(&transaction, &input);
}

static void run_storage_failures(void)
{
    aiqa_config_record_t input = make_active();
    aiqa_wifi_update_t request = make_request();

    fake_context_t stage_context = make_context();
    stage_context.stage_ok = false;
    aiqa_config_transaction_ports_t stage_ports = make_ports(&stage_context);
    aiqa_config_transaction_t stage_transaction;
    assert(aiqa_config_transaction_init(&stage_transaction, &input, &stage_ports));
    assert(aiqa_config_transaction_apply_wifi(&stage_transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_STAGE_FAILED);
    assert(strcmp(stage_context.trace, "WD") == 0);
    assert(stage_context.discarded_slot == AIQA_CONFIG_SLOT_B);
    assert_active_unchanged(&stage_transaction, &input);

    fake_context_t verify_context = make_context();
    verify_context.verify_ok = false;
    aiqa_config_transaction_ports_t verify_ports = make_ports(&verify_context);
    aiqa_config_transaction_t verify_transaction;
    assert(aiqa_config_transaction_init(&verify_transaction, &input, &verify_ports));
    assert(aiqa_config_transaction_apply_wifi(&verify_transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_VERIFY_FAILED);
    assert(strcmp(verify_context.trace, "WVD") == 0);
    assert(verify_context.discarded_slot == AIQA_CONFIG_SLOT_B);
    assert_active_unchanged(&verify_transaction, &input);
}

static void run_trial_failure(bool restore_ok, aiqa_config_transaction_status_t expected)
{
    aiqa_config_record_t input = make_active();
    fake_context_t context = make_context();
    context.trial_ok = false;
    context.restore_ok = restore_ok;
    aiqa_config_transaction_ports_t ports = make_ports(&context);
    aiqa_config_transaction_t transaction;
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));
    aiqa_wifi_update_t request = make_request();
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) == expected);
    assert(strcmp(context.trace, "WVTRD") == 0);
    assert(context.discarded_slot == AIQA_CONFIG_SLOT_B);
    if (restore_ok) {
        assert_active_unchanged(&transaction, &input);
    } else {
        aiqa_config_record_t copied = {0};
        assert(aiqa_config_transaction_copy_active(&transaction, &copied) ==
               AIQA_CONFIG_TRANSACTION_READ_RECOVERY_REQUIRED);
        assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
               AIQA_CONFIG_TRANSACTION_ERR_RECOVERY_REQUIRED);
    }
}

static void run_activation_failure(void)
{
    aiqa_config_record_t input = make_active();
    fake_context_t context = make_context();
    context.activation_result = AIQA_CONFIG_ACTIVATION_NOT_COMMITTED;
    aiqa_config_transaction_ports_t ports = make_ports(&context);
    aiqa_config_transaction_t transaction;
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));
    aiqa_wifi_update_t request = make_request();
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_ACTIVATE_FAILED_ROLLED_BACK);
    assert(strcmp(context.trace, "WVTARD") == 0);
    assert(context.discarded_slot == AIQA_CONFIG_SLOT_B);
    assert_active_unchanged(&transaction, &input);
}

static void run_activation_recovery_failure(void)
{
    aiqa_config_record_t input = make_active();
    fake_context_t context = make_context();
    context.activation_result = AIQA_CONFIG_ACTIVATION_NOT_COMMITTED;
    context.restore_ok = false;
    aiqa_config_transaction_ports_t ports = make_ports(&context);
    aiqa_config_transaction_t transaction;
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));
    aiqa_wifi_update_t request = make_request();
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_NETWORK_RECOVERY_FAILED);
    assert(strcmp(context.trace, "WVTARD") == 0);
    aiqa_config_record_t copied = input;
    assert(aiqa_config_transaction_copy_active(&transaction, &copied) ==
           AIQA_CONFIG_TRANSACTION_READ_RECOVERY_REQUIRED);
    aiqa_config_record_t cleared = {0};
    assert(memcmp(&copied, &cleared, sizeof(copied)) == 0);
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_RECOVERY_REQUIRED);
}

static void run_reentrant(void)
{
    aiqa_config_record_t input = make_active();
    fake_context_t context = make_context();
    aiqa_config_transaction_ports_t ports = make_ports(&context);
    aiqa_config_transaction_t transaction;
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));
    context.transaction = &transaction;
    context.reentrant_request = make_request();
    aiqa_wifi_update_t request = make_request();
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_OK);
    assert(context.reentrant_status == AIQA_CONFIG_TRANSACTION_ERR_BUSY);
    assert(context.reentrant_read_status == AIQA_CONFIG_TRANSACTION_READ_BUSY);
    assert(strcmp(context.trace, "WVTAD") == 0);
    assert(context.discarded_slot == AIQA_CONFIG_SLOT_A);
}

typedef struct {
    aiqa_config_transaction_t *transaction;
    aiqa_wifi_update_t request;
    aiqa_config_transaction_status_t result;
} apply_thread_args_t;

static void *apply_in_thread(void *opaque)
{
    apply_thread_args_t *args = opaque;
    args->result = aiqa_config_transaction_apply_wifi(args->transaction, &args->request, NULL);
    return NULL;
}

static void run_parallel(void)
{
    aiqa_config_record_t input = make_active();
    fake_context_t context = make_context();
    atomic_store(&context.block_stage, true);
    aiqa_config_transaction_ports_t ports = make_ports(&context);
    aiqa_config_transaction_t transaction;
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));
    apply_thread_args_t args = {
        .transaction = &transaction,
        .request = make_request(),
        .result = AIQA_CONFIG_TRANSACTION_ERR_INVALID_ARGUMENT,
    };
    pthread_t worker;
    assert(pthread_create(&worker, NULL, apply_in_thread, &args) == 0);
    while (!atomic_load(&context.stage_entered)) {
        sched_yield();
    }
    aiqa_wifi_update_t competing = make_request();
    assert(aiqa_config_transaction_apply_wifi(&transaction, &competing, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_BUSY);
    aiqa_config_transaction_t second_transaction;
    assert(aiqa_config_transaction_init(&second_transaction, &input, &ports));
    assert(aiqa_config_transaction_apply_wifi(&second_transaction, &competing, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_BUSY);
    atomic_store(&context.release_stage, true);
    assert(pthread_join(worker, NULL) == 0);
    assert(args.result == AIQA_CONFIG_TRANSACTION_OK);
    assert(strcmp(context.trace, "WVTAD") == 0);
    assert(context.discarded_slot == AIQA_CONFIG_SLOT_A);
}

static void run_activation_indeterminate(void)
{
    aiqa_config_record_t input = make_active();
    fake_context_t context = make_context();
    context.activation_result = AIQA_CONFIG_ACTIVATION_INDETERMINATE;
    aiqa_config_transaction_ports_t ports = make_ports(&context);
    aiqa_config_transaction_t transaction;
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));
    aiqa_wifi_update_t request = make_request();
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_ACTIVATION_INDETERMINATE);
    assert(strcmp(context.trace, "WVTAQ") == 0);
    aiqa_config_record_t copied = {0};
    assert(aiqa_config_transaction_copy_active(&transaction, &copied) ==
           AIQA_CONFIG_TRANSACTION_READ_RECOVERY_REQUIRED);
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_RECOVERY_REQUIRED);
    assert(strcmp(context.trace, "WVTAQ") == 0);
}

static void run_cleanup_failure(void)
{
    aiqa_config_record_t input = make_active();
    fake_context_t context = make_context();
    context.stage_ok = false;
    context.discard_ok = false;
    aiqa_config_transaction_ports_t ports = make_ports(&context);
    aiqa_config_transaction_t transaction;
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));
    aiqa_wifi_update_t request = make_request();
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_CANDIDATE_CLEANUP_FAILED);
    assert(strcmp(context.trace, "WD") == 0);
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_RECOVERY_REQUIRED);
    assert(strcmp(context.trace, "WD") == 0);
}

static void run_retired_slot_cleanup_failure(void)
{
    aiqa_config_record_t input = make_active();
    fake_context_t context = make_context();
    context.discard_ok = false;
    aiqa_config_transaction_ports_t ports = make_ports(&context);
    aiqa_config_transaction_t transaction;
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));
    aiqa_wifi_update_t request = make_request();
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_RETIRED_SLOT_CLEANUP_FAILED);
    assert(strcmp(context.trace, "WVTAD") == 0);
    assert(context.discarded_slot == AIQA_CONFIG_SLOT_A);

    aiqa_config_record_t copied = {0};
    assert(aiqa_config_transaction_copy_active(&transaction, &copied) ==
           AIQA_CONFIG_TRANSACTION_READ_RECOVERY_REQUIRED);
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_RECOVERY_REQUIRED);
}

static void run_api_edges(void)
{
    aiqa_config_record_t input = make_active();
    fake_context_t context = make_context();
    aiqa_config_transaction_ports_t ports = make_ports(&context);
    aiqa_config_transaction_t transaction = {0};
    aiqa_wifi_update_t request = make_request();

    assert(!aiqa_config_transaction_init(NULL, &input, &ports));
    assert(!aiqa_config_transaction_init(&transaction, NULL, &ports));
    input.active_slot = (aiqa_config_slot_t)99;
    assert(!aiqa_config_transaction_init(&transaction, &input, &ports));
    input = make_active();
    assert(!aiqa_config_transaction_init(&transaction, &input, NULL));

#define EXPECT_INVALID_PORT(member)                \
    do {                                            \
        aiqa_config_transaction_ports_t invalid = ports; \
        invalid.storage.member = NULL;             \
        assert(!aiqa_config_transaction_init(&transaction, &input, &invalid)); \
    } while (0)
    EXPECT_INVALID_PORT(stage);
    EXPECT_INVALID_PORT(verify);
    EXPECT_INVALID_PORT(activate);
    EXPECT_INVALID_PORT(discard);
#undef EXPECT_INVALID_PORT

    aiqa_config_transaction_ports_t invalid_network = ports;
    invalid_network.network.trial_connect = NULL;
    assert(!aiqa_config_transaction_init(&transaction, &input, &invalid_network));
    invalid_network = ports;
    invalid_network.network.restore_connect = NULL;
    assert(!aiqa_config_transaction_init(&transaction, &input, &invalid_network));
    invalid_network = ports;
    invalid_network.network.quarantine = NULL;
    assert(!aiqa_config_transaction_init(&transaction, &input, &invalid_network));

    input.active_slot = AIQA_CONFIG_SLOT_LEGACY;
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));
    input.active_slot = AIQA_CONFIG_SLOT_B;
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));
    aiqa_config_record_t copied = {0};
    assert(aiqa_config_transaction_copy_active(NULL, &copied) ==
           AIQA_CONFIG_TRANSACTION_READ_INVALID_ARGUMENT);
    aiqa_config_transaction_t uninitialized = {0};
    assert(aiqa_config_transaction_copy_active(&uninitialized, &copied) ==
           AIQA_CONFIG_TRANSACTION_READ_INVALID_ARGUMENT);
    assert(aiqa_config_transaction_copy_active(&transaction, NULL) ==
           AIQA_CONFIG_TRANSACTION_READ_INVALID_ARGUMENT);
    assert(aiqa_config_transaction_apply_wifi(NULL, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_INVALID_ARGUMENT);
    assert(aiqa_config_transaction_apply_wifi(&uninitialized, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_INVALID_ARGUMENT);

    input = make_active();
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));
    assert(aiqa_config_transaction_apply_wifi(&transaction, NULL, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_INVALID_ARGUMENT);
    request.password = "short";
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_PASSWORD);
    request.password_action = AIQA_WIFI_PASSWORD_KEEP;
    request.password = k_test_unexpected_password_value;
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_PASSWORD_ACTION);
    input.revision = UINT32_MAX;
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));
    request.base_revision = UINT32_MAX;
    request.password = NULL;
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_REVISION_EXHAUSTED);
    assert(context.trace[0] == '\0');

    const aiqa_config_transaction_status_t statuses[] = {
        AIQA_CONFIG_TRANSACTION_OK,
        AIQA_CONFIG_TRANSACTION_ERR_INVALID_ARGUMENT,
        AIQA_CONFIG_TRANSACTION_ERR_BUSY,
        AIQA_CONFIG_TRANSACTION_ERR_REVISION_CONFLICT,
        AIQA_CONFIG_TRANSACTION_ERR_REVISION_EXHAUSTED,
        AIQA_CONFIG_TRANSACTION_ERR_SSID,
        AIQA_CONFIG_TRANSACTION_ERR_PASSWORD,
        AIQA_CONFIG_TRANSACTION_ERR_PASSWORD_ACTION,
        AIQA_CONFIG_TRANSACTION_ERR_STAGE_FAILED,
        AIQA_CONFIG_TRANSACTION_ERR_VERIFY_FAILED,
        AIQA_CONFIG_TRANSACTION_ERR_TRIAL_FAILED_ROLLED_BACK,
        AIQA_CONFIG_TRANSACTION_ERR_ACTIVATE_FAILED_ROLLED_BACK,
        AIQA_CONFIG_TRANSACTION_ERR_NETWORK_RECOVERY_FAILED,
        AIQA_CONFIG_TRANSACTION_ERR_ACTIVATION_INDETERMINATE,
        AIQA_CONFIG_TRANSACTION_ERR_CANDIDATE_CLEANUP_FAILED,
        AIQA_CONFIG_TRANSACTION_ERR_RETIRED_SLOT_CLEANUP_FAILED,
        AIQA_CONFIG_TRANSACTION_ERR_RECOVERY_REQUIRED,
    };
    for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
        assert(strcmp(aiqa_config_transaction_status_name(statuses[i]), "UNKNOWN") != 0);
    }
    assert(strcmp(aiqa_config_transaction_status_name((aiqa_config_transaction_status_t)99),
                  "UNKNOWN") == 0);

    aiqa_config_record_t secret_copy = make_active();
    aiqa_config_record_secure_clear(&secret_copy);
    const unsigned char *bytes = (const unsigned char *)&secret_copy;
    for (size_t i = 0; i < sizeof(secret_copy); ++i) {
        assert(bytes[i] == 0);
    }
    aiqa_config_record_secure_clear(NULL);

    input = make_active();
    assert(aiqa_config_transaction_init(&transaction, &input, &ports));
    aiqa_config_transaction_secure_clear(&transaction);
    assert(aiqa_config_transaction_apply_wifi(&transaction, &request, NULL) ==
           AIQA_CONFIG_TRANSACTION_ERR_INVALID_ARGUMENT);
    const unsigned char *transaction_bytes = (const unsigned char *)&transaction;
    for (size_t i = 0; i < sizeof(transaction); ++i) {
        assert(transaction_bytes[i] == 0);
    }
    aiqa_config_transaction_secure_clear(NULL);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "success") == 0) {
        run_success();
    } else if (strcmp(argv[1], "validation") == 0) {
        run_validation();
    } else if (strcmp(argv[1], "storage_failures") == 0) {
        run_storage_failures();
    } else if (strcmp(argv[1], "trial_failure") == 0) {
        run_trial_failure(true, AIQA_CONFIG_TRANSACTION_ERR_TRIAL_FAILED_ROLLED_BACK);
    } else if (strcmp(argv[1], "recovery_failure") == 0) {
        run_trial_failure(false, AIQA_CONFIG_TRANSACTION_ERR_NETWORK_RECOVERY_FAILED);
    } else if (strcmp(argv[1], "activation_failure") == 0) {
        run_activation_failure();
    } else if (strcmp(argv[1], "activation_recovery_failure") == 0) {
        run_activation_recovery_failure();
    } else if (strcmp(argv[1], "reentrant") == 0) {
        run_reentrant();
    } else if (strcmp(argv[1], "parallel") == 0) {
        run_parallel();
    } else if (strcmp(argv[1], "activation_indeterminate") == 0) {
        run_activation_indeterminate();
    } else if (strcmp(argv[1], "cleanup_failure") == 0) {
        run_cleanup_failure();
    } else if (strcmp(argv[1], "retired_slot_cleanup_failure") == 0) {
        run_retired_slot_cleanup_failure();
    } else if (strcmp(argv[1], "api_edges") == 0) {
        run_api_edges();
    } else {
        return 2;
    }
    return 0;
}
