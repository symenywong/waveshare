#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t connection_generation;
    uint64_t authorization_generation;
    uint8_t session_id[8];
} aiqa_management_security_context_t;

typedef struct {
    atomic_flag lock;
    bool prepared;
    bool active;
    aiqa_management_security_context_t access;
} aiqa_management_access_registry_t;

void aiqa_management_access_registry_init(
    aiqa_management_access_registry_t *registry);
bool aiqa_management_access_registry_prepare(
    aiqa_management_access_registry_t *registry,
    const aiqa_management_security_context_t *access);
bool aiqa_management_access_registry_activate(
    aiqa_management_access_registry_t *registry,
    uint64_t connection_generation,
    uint64_t authorization_generation);
void aiqa_management_access_registry_revoke(
    aiqa_management_access_registry_t *registry);
bool aiqa_management_access_registry_authorize(
    aiqa_management_access_registry_t *registry,
    const aiqa_management_security_context_t *access);

/* Process-wide registry shared by the USB owner and runtime authorization. */
void aiqa_management_access_global_init(void);
bool aiqa_management_access_global_prepare(
    const aiqa_management_security_context_t *access);
bool aiqa_management_access_global_activate(
    uint64_t connection_generation,
    uint64_t authorization_generation);
void aiqa_management_access_global_revoke(void);
bool aiqa_management_access_global_authorize(
    const aiqa_management_security_context_t *access);

#ifdef __cplusplus
}
#endif
