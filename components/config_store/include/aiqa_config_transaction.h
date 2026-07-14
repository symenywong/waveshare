#pragma once

#include "aiqa_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AIQA_CONFIG_SLOT_LEGACY = 0,
    AIQA_CONFIG_SLOT_A,
    AIQA_CONFIG_SLOT_B,
} aiqa_config_slot_t;

typedef struct {
    aiqa_config_t config;
    aiqa_secret_config_t secrets;
    uint32_t revision;
    aiqa_config_slot_t active_slot;
} aiqa_config_record_t;

typedef enum {
    /* The adapter cannot prove which head is durable; both slots must remain. */
    AIQA_CONFIG_ACTIVATION_INDETERMINATE = 0,
    /* The new head was durably committed and read back as slot/revision. */
    AIQA_CONFIG_ACTIVATION_COMMITTED = 1,
    /* A read-back durably confirmed that the previous head is still active. */
    AIQA_CONFIG_ACTIVATION_NOT_COMMITTED = 2,
} aiqa_config_activation_result_t;

/*
 * Storage callbacks must not retain record/secret pointers after returning.
 * stage=true means the complete candidate is durable in the requested inactive
 * slot. stage=false may represent a partial write and is always followed by
 * discard. verify=true means a durable read-back exactly matches the candidate.
 * discard=true means the candidate is durably removed or made unreadable.
 * Any ambiguous head commit must return AIQA_CONFIG_ACTIVATION_INDETERMINATE.
 */
typedef struct {
    void *context;
    bool (*stage)(void *context, aiqa_config_slot_t slot, const aiqa_config_record_t *candidate);
    bool (*verify)(void *context, aiqa_config_slot_t slot, const aiqa_config_record_t *candidate);
    aiqa_config_activation_result_t (*activate)(
        void *context,
        const aiqa_config_record_t *candidate,
        aiqa_config_slot_t expected_slot,
        uint32_t expected_revision);
    bool (*discard)(void *context, aiqa_config_slot_t slot);
} aiqa_config_storage_ports_t;

typedef struct {
    void *context;
    bool (*trial_connect)(void *context, const aiqa_secret_config_t *secrets);
    bool (*restore_connect)(void *context, const aiqa_secret_config_t *secrets);
    /* Disconnects all candidate networking when durable activation is unknown. */
    void (*quarantine)(void *context);
} aiqa_config_network_ports_t;

typedef struct {
    aiqa_config_storage_ports_t storage;
    aiqa_config_network_ports_t network;
} aiqa_config_transaction_ports_t;

typedef enum {
    AIQA_CONFIG_TRANSACTION_OK = 0,
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
    AIQA_CONFIG_TRANSACTION_ERR_RECOVERY_REQUIRED,
} aiqa_config_transaction_status_t;

typedef enum {
    AIQA_CONFIG_TRANSACTION_READ_OK = 0,
    AIQA_CONFIG_TRANSACTION_READ_INVALID_ARGUMENT,
    AIQA_CONFIG_TRANSACTION_READ_BUSY,
    AIQA_CONFIG_TRANSACTION_READ_RECOVERY_REQUIRED,
} aiqa_config_transaction_read_status_t;

#define AIQA_CONFIG_TRANSACTION_PRIVATE_SIZE \
    (sizeof(aiqa_config_record_t) + sizeof(aiqa_config_transaction_ports_t) + 64U)

typedef union {
    max_align_t _alignment;
    unsigned char _private[AIQA_CONFIG_TRANSACTION_PRIVATE_SIZE];
} aiqa_config_transaction_t;

bool aiqa_config_transaction_init(
    aiqa_config_transaction_t *transaction,
    const aiqa_config_record_t *active,
    const aiqa_config_transaction_ports_t *ports);

/* The caller owns a successful secret copy and must clear it after use. */
aiqa_config_transaction_read_status_t aiqa_config_transaction_copy_active(
    aiqa_config_transaction_t *transaction,
    aiqa_config_record_t *out_active);

void aiqa_config_record_secure_clear(aiqa_config_record_t *record);
void aiqa_config_transaction_secure_clear(aiqa_config_transaction_t *transaction);

aiqa_config_transaction_status_t aiqa_config_transaction_apply_wifi(
    aiqa_config_transaction_t *transaction,
    const aiqa_wifi_update_t *request,
    aiqa_public_wifi_config_t *out_view);

const char *aiqa_config_transaction_status_name(aiqa_config_transaction_status_t status);

#ifdef __cplusplus
}
#endif
