#include "aiqa_management_access.h"

#include <stddef.h>

static aiqa_management_access_registry_t s_global_registry;
static atomic_bool s_global_initialized = ATOMIC_VAR_INIT(false);

static void secure_zero(void *value, size_t value_size)
{
    volatile uint8_t *bytes = value;
    while (value != NULL && value_size > 0U) {
        *bytes++ = 0;
        value_size -= 1U;
    }
}

static void lock_registry(aiqa_management_access_registry_t *registry)
{
    while (atomic_flag_test_and_set_explicit(&registry->lock, memory_order_acquire)) {
    }
}

static void unlock_registry(aiqa_management_access_registry_t *registry)
{
    atomic_flag_clear_explicit(&registry->lock, memory_order_release);
}

static bool session_id_is_nonzero(const uint8_t session_id[8])
{
    uint8_t combined = 0;
    for (size_t index = 0; index < 8U; ++index) {
        combined |= session_id[index];
    }
    return combined != 0U;
}

static bool access_is_valid(const aiqa_management_security_context_t *access)
{
    return access != NULL && access->connection_generation != 0U &&
           access->authorization_generation != 0U &&
           session_id_is_nonzero(access->session_id);
}

static bool session_ids_match(const uint8_t first[8], const uint8_t second[8])
{
    uint8_t difference = 0;
    for (size_t index = 0; index < 8U; ++index) {
        difference |= (uint8_t)(first[index] ^ second[index]);
    }
    return difference == 0U;
}

void aiqa_management_access_registry_init(
    aiqa_management_access_registry_t *registry)
{
    if (registry == NULL) {
        return;
    }
    *registry = (aiqa_management_access_registry_t){
        .lock = ATOMIC_FLAG_INIT,
    };
}

bool aiqa_management_access_registry_prepare(
    aiqa_management_access_registry_t *registry,
    const aiqa_management_security_context_t *access)
{
    if (registry == NULL || !access_is_valid(access)) {
        return false;
    }
    lock_registry(registry);
    secure_zero(&registry->access, sizeof(registry->access));
    registry->access = *access;
    registry->prepared = true;
    registry->active = false;
    unlock_registry(registry);
    return true;
}

bool aiqa_management_access_registry_activate(
    aiqa_management_access_registry_t *registry,
    uint64_t connection_generation,
    uint64_t authorization_generation)
{
    if (registry == NULL || connection_generation == 0U ||
        authorization_generation == 0U) {
        return false;
    }
    lock_registry(registry);
    const bool matches = registry->prepared && !registry->active &&
                         registry->access.connection_generation ==
                             connection_generation &&
                         registry->access.authorization_generation ==
                             authorization_generation;
    if (matches) {
        registry->active = true;
    }
    unlock_registry(registry);
    return matches;
}

void aiqa_management_access_registry_revoke(
    aiqa_management_access_registry_t *registry)
{
    if (registry == NULL) {
        return;
    }
    lock_registry(registry);
    registry->active = false;
    registry->prepared = false;
    secure_zero(&registry->access, sizeof(registry->access));
    unlock_registry(registry);
}

bool aiqa_management_access_registry_authorize(
    aiqa_management_access_registry_t *registry,
    const aiqa_management_security_context_t *access)
{
    if (registry == NULL || !access_is_valid(access)) {
        return false;
    }
    lock_registry(registry);
    const bool authorized =
        registry->prepared && registry->active &&
        registry->access.connection_generation == access->connection_generation &&
        registry->access.authorization_generation == access->authorization_generation &&
        session_ids_match(registry->access.session_id, access->session_id);
    unlock_registry(registry);
    return authorized;
}

void aiqa_management_access_global_init(void)
{
    aiqa_management_access_registry_init(&s_global_registry);
    atomic_store_explicit(&s_global_initialized, true, memory_order_release);
}

bool aiqa_management_access_global_prepare(
    const aiqa_management_security_context_t *access)
{
    return atomic_load_explicit(&s_global_initialized, memory_order_acquire) &&
           aiqa_management_access_registry_prepare(&s_global_registry, access);
}

bool aiqa_management_access_global_activate(
    uint64_t connection_generation,
    uint64_t authorization_generation)
{
    return atomic_load_explicit(&s_global_initialized, memory_order_acquire) &&
           aiqa_management_access_registry_activate(
               &s_global_registry,
               connection_generation,
               authorization_generation);
}

void aiqa_management_access_global_revoke(void)
{
    if (atomic_load_explicit(&s_global_initialized, memory_order_acquire)) {
        aiqa_management_access_registry_revoke(&s_global_registry);
    }
}

bool aiqa_management_access_global_authorize(
    const aiqa_management_security_context_t *access)
{
    return atomic_load_explicit(&s_global_initialized, memory_order_acquire) &&
           aiqa_management_access_registry_authorize(&s_global_registry, access);
}
