#pragma once

#include "aiqa_pairing_crypto.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIQA_PAIRING_WINDOW_MS UINT64_C(120000)
#define AIQA_PAIRING_STEP_MS UINT64_C(15000)
#define AIQA_PAIRING_SESSION_IDLE_MS UINT64_C(300000)
#define AIQA_PAIRING_SESSION_ABSOLUTE_MS UINT64_C(1800000)
#define AIQA_PAIRING_MAX_ATTEMPTS 5U
#define AIQA_PAIRING_LOCK_RECORD_VERSION 1U

typedef enum {
  AIQA_PAIRING_LIFECYCLE_INACTIVE = 0,
  AIQA_PAIRING_LIFECYCLE_AVAILABLE,
  AIQA_PAIRING_LIFECYCLE_HANDSHAKE,
  AIQA_PAIRING_LIFECYCLE_WAIT_FINISHED_SENT,
  AIQA_PAIRING_LIFECYCLE_ACTIVE,
  AIQA_PAIRING_LIFECYCLE_LOCKED,
  AIQA_PAIRING_LIFECYCLE_FAULT,
} aiqa_pairing_lifecycle_state_t;

typedef enum {
  AIQA_PAIRING_LIFECYCLE_OK = 0,
  AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT,
  AIQA_PAIRING_LIFECYCLE_INVALID_STATE,
  AIQA_PAIRING_LIFECYCLE_INVALID_SESSION,
  AIQA_PAIRING_LIFECYCLE_LOCAL_PRESENCE_REQUIRED,
  AIQA_PAIRING_LIFECYCLE_LOCKED_OUT,
  AIQA_PAIRING_LIFECYCLE_BUSY,
  AIQA_PAIRING_LIFECYCLE_RANDOM_FAILED,
  AIQA_PAIRING_LIFECYCLE_DISPLAY_FAILED,
  AIQA_PAIRING_LIFECYCLE_PERSISTENCE_ERROR,
  AIQA_PAIRING_LIFECYCLE_CLOCK_FAILED,
  AIQA_PAIRING_LIFECYCLE_START_FAILED,
  AIQA_PAIRING_LIFECYCLE_FAULTED,
} aiqa_pairing_lifecycle_result_t;

typedef enum {
  AIQA_PAIRING_PERSISTENCE_OK = 0,
  AIQA_PAIRING_PERSISTENCE_NOT_FOUND,
  AIQA_PAIRING_PERSISTENCE_ERROR,
} aiqa_pairing_persistence_result_t;

typedef enum {
  AIQA_PAIRING_LOCAL_START = 1,
  AIQA_PAIRING_LOCAL_RESET = 2,
} aiqa_pairing_local_action_t;

typedef struct {
  uint32_t version;
  uint32_t attempts_used;
} aiqa_pairing_lock_record_t;

typedef struct {
  uint64_t window_ms;
  uint64_t step_ms;
  uint64_t session_idle_ms;
  uint64_t session_absolute_ms;
  uint32_t max_attempts;
} aiqa_pairing_lifecycle_policy_t;

typedef bool (*aiqa_pairing_code_consumer_fn)(
    void *context, const uint8_t code[AIQA_PAIRING_CODE_SIZE],
    uint64_t connection_id, uint64_t handshake_id);

/*
 * All lifecycle APIs and callbacks run synchronously on one owner task. The
 * ports context must outlive the lifecycle and callbacks must never re-enter
 * it. Code pointers are valid only for the callback duration: adapters must
 * neither retain nor log them and must copy them only into volatile crypto
 * state.
 */
typedef struct {
  void *context;
  bool (*monotonic_ms)(void *context, uint64_t *out_now_ms);
  aiqa_pairing_random_fn random_fill;
  /* Load/store must synchronously complete an atomic durable commit/readback.
   */
  aiqa_pairing_persistence_result_t (*load_lock_record)(
      void *context, aiqa_pairing_lock_record_t *out_record);
  bool (*store_lock_record)(void *context,
                            const aiqa_pairing_lock_record_t *record);
  bool (*show_code)(void *context, const uint8_t code[AIQA_PAIRING_CODE_SIZE]);
  bool (*clear_code)(void *context);
  bool (*consume_local_presence)(void *context,
                                 aiqa_pairing_local_action_t action);
  /*
   * Synchronously and infallibly invalidate this connection's crypto context,
   * channel and authorization-registry generation before returning. It must
   * not enqueue deferred cleanup.
   */
  void (*revoke_connection)(void *context, uint64_t connection_id);
} aiqa_pairing_lifecycle_ports_t;

typedef struct {
  aiqa_pairing_lifecycle_state_t state;
  uint32_t attempts_remaining;
  uint64_t remaining_ms;
} aiqa_pairing_lifecycle_status_t;

typedef struct aiqa_pairing_lifecycle aiqa_pairing_lifecycle_t;

aiqa_pairing_lifecycle_policy_t aiqa_pairing_lifecycle_default_policy(void);
bool aiqa_pairing_lifecycle_policy_is_safe(
    const aiqa_pairing_lifecycle_policy_t *policy);
aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_create(aiqa_pairing_lifecycle_t **out_lifecycle,
                              const aiqa_pairing_lifecycle_policy_t *policy,
                              const aiqa_pairing_lifecycle_ports_t *ports);
void aiqa_pairing_lifecycle_destroy(aiqa_pairing_lifecycle_t **lifecycle);

aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_local_open(aiqa_pairing_lifecycle_t *lifecycle,
                                  uint64_t connection_id);
aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_begin(aiqa_pairing_lifecycle_t *lifecycle,
                             uint64_t connection_id, uint64_t handshake_id,
                             aiqa_pairing_code_consumer_fn consume_code,
                             void *consumer_context);
aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_handshake_progress(aiqa_pairing_lifecycle_t *lifecycle,
                                          uint64_t connection_id,
                                          uint64_t handshake_id);
/*
 * These Finished transitions are trusted owner notifications, not proof
 * verification APIs. Runtime authorization must additionally require a
 * matching installed secure-channel generation; ACTIVE alone is insufficient.
 */
aiqa_pairing_lifecycle_result_t aiqa_pairing_lifecycle_client_finished_verified(
    aiqa_pairing_lifecycle_t *lifecycle, uint64_t connection_id,
    uint64_t handshake_id);
aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_device_finished_sent(aiqa_pairing_lifecycle_t *lifecycle,
                                            uint64_t connection_id,
                                            uint64_t handshake_id);
aiqa_pairing_lifecycle_result_t aiqa_pairing_lifecycle_authenticated_activity(
    aiqa_pairing_lifecycle_t *lifecycle, uint64_t connection_id);
aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_protocol_failed(aiqa_pairing_lifecycle_t *lifecycle,
                                       uint64_t connection_id,
                                       uint64_t handshake_id);
aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_disconnect(aiqa_pairing_lifecycle_t *lifecycle,
                                  uint64_t connection_id);
aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_tick(aiqa_pairing_lifecycle_t *lifecycle);
aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_local_reset(aiqa_pairing_lifecycle_t *lifecycle);
aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_get_status(aiqa_pairing_lifecycle_t *lifecycle,
                                  aiqa_pairing_lifecycle_status_t *out_status);

#ifdef __cplusplus
}
#endif
