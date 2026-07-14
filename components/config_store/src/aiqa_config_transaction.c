#include "aiqa_config_transaction.h"
#include "aiqa_config_transaction_internal.h"

#include <stddef.h>
#include <stdatomic.h>

typedef struct {
    aiqa_config_record_t active;
    aiqa_config_transaction_ports_t ports;
    atomic_bool locked;
    bool recovery_required;
    bool initialized;
} aiqa_config_transaction_impl_t;

static atomic_bool s_global_transaction_locked = ATOMIC_VAR_INIT(false);

bool aiqa_config_lifecycle_try_lock(void)
{
    bool expected = false;
    return atomic_compare_exchange_strong_explicit(
        &s_global_transaction_locked,
        &expected,
        true,
        memory_order_acquire,
        memory_order_relaxed);
}

void aiqa_config_lifecycle_unlock(void)
{
    atomic_store_explicit(&s_global_transaction_locked, false, memory_order_release);
}

_Static_assert(
    sizeof(aiqa_config_transaction_impl_t) <= sizeof(aiqa_config_transaction_t),
    "AIQA_CONFIG_TRANSACTION_PRIVATE_SIZE is too small");

static aiqa_config_transaction_impl_t *transaction_impl(aiqa_config_transaction_t *transaction)
{
    return (aiqa_config_transaction_impl_t *)(void *)transaction;
}

static void secure_zero(void *value, size_t value_size)
{
    volatile unsigned char *bytes = value;
    while (value_size > 0) {
        *bytes++ = 0;
        --value_size;
    }
}

static bool slot_is_valid(aiqa_config_slot_t slot)
{
    return slot == AIQA_CONFIG_SLOT_LEGACY ||
           slot == AIQA_CONFIG_SLOT_A ||
           slot == AIQA_CONFIG_SLOT_B;
}

static aiqa_config_slot_t inactive_slot(aiqa_config_slot_t active_slot)
{
    return active_slot == AIQA_CONFIG_SLOT_A ? AIQA_CONFIG_SLOT_B : AIQA_CONFIG_SLOT_A;
}

static bool ports_are_valid(const aiqa_config_transaction_ports_t *ports)
{
    return ports != NULL &&
           ports->storage.stage != NULL &&
           ports->storage.verify != NULL &&
           ports->storage.activate != NULL &&
           ports->storage.discard != NULL &&
           ports->network.trial_connect != NULL &&
           ports->network.restore_connect != NULL &&
           ports->network.quarantine != NULL;
}

static bool try_atomic_lock(atomic_bool *locked)
{
    bool expected = false;
    return atomic_compare_exchange_strong_explicit(
        locked,
        &expected,
        true,
        memory_order_acquire,
        memory_order_relaxed);
}

static bool try_lock(aiqa_config_transaction_impl_t *transaction)
{
    return try_atomic_lock(&transaction->locked);
}

static void unlock(aiqa_config_transaction_impl_t *transaction)
{
    atomic_store_explicit(&transaction->locked, false, memory_order_release);
}

static void unlock_apply(aiqa_config_transaction_impl_t *transaction)
{
    aiqa_config_lifecycle_unlock();
    unlock(transaction);
}

static aiqa_config_transaction_status_t map_wifi_status(aiqa_wifi_update_status_t status)
{
    switch (status) {
    case AIQA_WIFI_UPDATE_OK:
        return AIQA_CONFIG_TRANSACTION_OK;
    case AIQA_WIFI_UPDATE_ERR_REVISION_CONFLICT:
        return AIQA_CONFIG_TRANSACTION_ERR_REVISION_CONFLICT;
    case AIQA_WIFI_UPDATE_ERR_REVISION_EXHAUSTED:
        return AIQA_CONFIG_TRANSACTION_ERR_REVISION_EXHAUSTED;
    case AIQA_WIFI_UPDATE_ERR_SSID:
        return AIQA_CONFIG_TRANSACTION_ERR_SSID;
    case AIQA_WIFI_UPDATE_ERR_PASSWORD:
        return AIQA_CONFIG_TRANSACTION_ERR_PASSWORD;
    case AIQA_WIFI_UPDATE_ERR_PASSWORD_ACTION:
        return AIQA_CONFIG_TRANSACTION_ERR_PASSWORD_ACTION;
    case AIQA_WIFI_UPDATE_ERR_INVALID_ARGUMENT:
    default:
        return AIQA_CONFIG_TRANSACTION_ERR_INVALID_ARGUMENT;
    }
}

bool aiqa_config_transaction_init(
    aiqa_config_transaction_t *transaction,
    const aiqa_config_record_t *active,
    const aiqa_config_transaction_ports_t *ports)
{
    if (transaction == NULL || active == NULL || active->revision == 0 ||
        !slot_is_valid(active->active_slot) ||
        aiqa_config_validate(&active->config) != AIQA_CONFIG_OK ||
        aiqa_secret_config_validate(&active->secrets) != AIQA_SECRET_OK ||
        !ports_are_valid(ports)) {
        return false;
    }

    aiqa_config_transaction_impl_t *impl = transaction_impl(transaction);
    impl->initialized = false;
    impl->active = *active;
    impl->ports = *ports;
    impl->recovery_required = false;
    atomic_init(&impl->locked, false);
    impl->initialized = true;
    return true;
}

aiqa_config_transaction_read_status_t aiqa_config_transaction_copy_active(
    aiqa_config_transaction_t *transaction,
    aiqa_config_record_t *out_active)
{
    if (out_active == NULL) {
        return AIQA_CONFIG_TRANSACTION_READ_INVALID_ARGUMENT;
    }
    aiqa_config_record_secure_clear(out_active);
    if (transaction == NULL) {
        return AIQA_CONFIG_TRANSACTION_READ_INVALID_ARGUMENT;
    }
    aiqa_config_transaction_impl_t *impl = transaction_impl(transaction);
    if (!impl->initialized) {
        return AIQA_CONFIG_TRANSACTION_READ_INVALID_ARGUMENT;
    }
    if (!try_lock(impl)) {
        return AIQA_CONFIG_TRANSACTION_READ_BUSY;
    }

    const bool available = !impl->recovery_required;
    if (available) {
        *out_active = impl->active;
    }
    unlock(impl);
    return available ? AIQA_CONFIG_TRANSACTION_READ_OK
                     : AIQA_CONFIG_TRANSACTION_READ_RECOVERY_REQUIRED;
}

aiqa_config_transaction_status_t aiqa_config_transaction_apply_wifi(
    aiqa_config_transaction_t *transaction,
    const aiqa_wifi_update_t *request,
    aiqa_public_wifi_config_t *out_view)
{
    if (transaction == NULL || request == NULL) {
        return AIQA_CONFIG_TRANSACTION_ERR_INVALID_ARGUMENT;
    }
    aiqa_config_transaction_impl_t *impl = transaction_impl(transaction);
    if (!impl->initialized) {
        return AIQA_CONFIG_TRANSACTION_ERR_INVALID_ARGUMENT;
    }
    if (!try_lock(impl)) {
        return AIQA_CONFIG_TRANSACTION_ERR_BUSY;
    }
    if (!aiqa_config_lifecycle_try_lock()) {
        unlock(impl);
        return AIQA_CONFIG_TRANSACTION_ERR_BUSY;
    }
    if (impl->recovery_required) {
        unlock_apply(impl);
        return AIQA_CONFIG_TRANSACTION_ERR_RECOVERY_REQUIRED;
    }

    aiqa_config_record_t candidate = impl->active;
    uint32_t next_revision = 0;
    aiqa_wifi_update_status_t wifi_status = aiqa_config_prepare_wifi_update(
        &impl->active.secrets,
        impl->active.revision,
        request,
        &candidate.secrets,
        &next_revision);
    if (wifi_status != AIQA_WIFI_UPDATE_OK) {
        secure_zero(&candidate, sizeof(candidate));
        unlock_apply(impl);
        return map_wifi_status(wifi_status);
    }

    candidate.revision = next_revision;
    candidate.active_slot = inactive_slot(impl->active.active_slot);
    aiqa_config_transaction_status_t result = AIQA_CONFIG_TRANSACTION_OK;
    const aiqa_config_storage_ports_t *storage = &impl->ports.storage;
    const aiqa_config_network_ports_t *network = &impl->ports.network;
    if (!storage->stage(storage->context, candidate.active_slot, &candidate)) {
        const bool discarded = storage->discard(storage->context, candidate.active_slot);
        impl->recovery_required = !discarded;
        result = discarded ? AIQA_CONFIG_TRANSACTION_ERR_STAGE_FAILED
                           : AIQA_CONFIG_TRANSACTION_ERR_CANDIDATE_CLEANUP_FAILED;
        goto done;
    }
    if (!storage->verify(storage->context, candidate.active_slot, &candidate)) {
        const bool discarded = storage->discard(storage->context, candidate.active_slot);
        impl->recovery_required = !discarded;
        result = discarded ? AIQA_CONFIG_TRANSACTION_ERR_VERIFY_FAILED
                           : AIQA_CONFIG_TRANSACTION_ERR_CANDIDATE_CLEANUP_FAILED;
        goto done;
    }
    if (!network->trial_connect(network->context, &candidate.secrets)) {
        const bool restored = network->restore_connect(network->context, &impl->active.secrets);
        const bool discarded = storage->discard(storage->context, candidate.active_slot);
        impl->recovery_required = !restored || !discarded;
        if (!restored) {
            result = AIQA_CONFIG_TRANSACTION_ERR_NETWORK_RECOVERY_FAILED;
        } else if (!discarded) {
            result = AIQA_CONFIG_TRANSACTION_ERR_CANDIDATE_CLEANUP_FAILED;
        } else {
            result = AIQA_CONFIG_TRANSACTION_ERR_TRIAL_FAILED_ROLLED_BACK;
        }
        goto done;
    }

    const aiqa_config_activation_result_t activation =
        storage->activate(
            storage->context,
            &candidate,
            impl->active.active_slot,
            impl->active.revision);
    if (activation == AIQA_CONFIG_ACTIVATION_INDETERMINATE ||
        (activation != AIQA_CONFIG_ACTIVATION_COMMITTED &&
         activation != AIQA_CONFIG_ACTIVATION_NOT_COMMITTED)) {
        network->quarantine(network->context);
        impl->recovery_required = true;
        result = AIQA_CONFIG_TRANSACTION_ERR_ACTIVATION_INDETERMINATE;
        goto done;
    }
    if (activation == AIQA_CONFIG_ACTIVATION_NOT_COMMITTED) {
        const bool restored = network->restore_connect(network->context, &impl->active.secrets);
        const bool discarded = storage->discard(storage->context, candidate.active_slot);
        impl->recovery_required = !restored || !discarded;
        if (!restored) {
            result = AIQA_CONFIG_TRANSACTION_ERR_NETWORK_RECOVERY_FAILED;
        } else if (!discarded) {
            result = AIQA_CONFIG_TRANSACTION_ERR_CANDIDATE_CLEANUP_FAILED;
        } else {
            result = AIQA_CONFIG_TRANSACTION_ERR_ACTIVATE_FAILED_ROLLED_BACK;
        }
        goto done;
    }

    impl->active = candidate;
    if (out_view != NULL) {
        (void)aiqa_config_build_public_wifi_view(
            &impl->active.secrets, impl->active.revision, out_view);
    }

done:
    secure_zero(&candidate, sizeof(candidate));
    unlock_apply(impl);
    return result;
}

void aiqa_config_record_secure_clear(aiqa_config_record_t *record)
{
    if (record != NULL) {
        secure_zero(record, sizeof(*record));
    }
}

void aiqa_config_transaction_secure_clear(aiqa_config_transaction_t *transaction)
{
    if (transaction != NULL) {
        secure_zero(transaction, sizeof(*transaction));
    }
}

const char *aiqa_config_transaction_status_name(aiqa_config_transaction_status_t status)
{
    switch (status) {
    case AIQA_CONFIG_TRANSACTION_OK:
        return "OK";
    case AIQA_CONFIG_TRANSACTION_ERR_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case AIQA_CONFIG_TRANSACTION_ERR_BUSY:
        return "BUSY";
    case AIQA_CONFIG_TRANSACTION_ERR_REVISION_CONFLICT:
        return "REVISION_CONFLICT";
    case AIQA_CONFIG_TRANSACTION_ERR_REVISION_EXHAUSTED:
        return "REVISION_EXHAUSTED";
    case AIQA_CONFIG_TRANSACTION_ERR_SSID:
        return "SSID";
    case AIQA_CONFIG_TRANSACTION_ERR_PASSWORD:
        return "PASSWORD";
    case AIQA_CONFIG_TRANSACTION_ERR_PASSWORD_ACTION:
        return "PASSWORD_ACTION";
    case AIQA_CONFIG_TRANSACTION_ERR_STAGE_FAILED:
        return "STAGE_FAILED";
    case AIQA_CONFIG_TRANSACTION_ERR_VERIFY_FAILED:
        return "VERIFY_FAILED";
    case AIQA_CONFIG_TRANSACTION_ERR_TRIAL_FAILED_ROLLED_BACK:
        return "TRIAL_FAILED_ROLLED_BACK";
    case AIQA_CONFIG_TRANSACTION_ERR_ACTIVATE_FAILED_ROLLED_BACK:
        return "ACTIVATE_FAILED_ROLLED_BACK";
    case AIQA_CONFIG_TRANSACTION_ERR_NETWORK_RECOVERY_FAILED:
        return "NETWORK_RECOVERY_FAILED";
    case AIQA_CONFIG_TRANSACTION_ERR_ACTIVATION_INDETERMINATE:
        return "ACTIVATION_INDETERMINATE";
    case AIQA_CONFIG_TRANSACTION_ERR_CANDIDATE_CLEANUP_FAILED:
        return "CANDIDATE_CLEANUP_FAILED";
    case AIQA_CONFIG_TRANSACTION_ERR_RECOVERY_REQUIRED:
        return "RECOVERY_REQUIRED";
    default:
        return "UNKNOWN";
    }
}
