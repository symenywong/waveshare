#include "aiqa_pairing_lifecycle.h"

#include <stdlib.h>
#include <string.h>

#define AIQA_PAIRING_DIGIT_REJECTION_LIMIT 256U

struct aiqa_pairing_lifecycle {
  aiqa_pairing_lifecycle_policy_t policy;
  aiqa_pairing_lifecycle_ports_t ports;
  aiqa_pairing_lock_record_t lock_record;
  aiqa_pairing_lifecycle_state_t state;
  uint8_t code[AIQA_PAIRING_CODE_SIZE];
  bool code_visible;
  bool clock_initialized;
  uint64_t last_now_ms;
  uint64_t window_deadline_ms;
  uint64_t step_deadline_ms;
  uint64_t session_idle_deadline_ms;
  uint64_t session_absolute_deadline_ms;
  uint64_t connection_id;
  uint64_t handshake_id;
};

static void secure_zero(void *value, size_t value_size) {
  volatile uint8_t *bytes = value;
  while (value_size > 0) {
    *bytes++ = 0;
    value_size -= 1U;
  }
}

static uint64_t deadline_after(uint64_t now_ms, uint64_t duration_ms) {
  return now_ms > UINT64_MAX - duration_ms ? UINT64_MAX : now_ms + duration_ms;
}

static uint64_t earlier_deadline(uint64_t first, uint64_t second) {
  return first < second ? first : second;
}

static bool ports_are_valid(const aiqa_pairing_lifecycle_ports_t *ports) {
  return ports != NULL && ports->monotonic_ms != NULL &&
         ports->random_fill != NULL && ports->load_lock_record != NULL &&
         ports->store_lock_record != NULL && ports->show_code != NULL &&
         ports->clear_code != NULL && ports->consume_local_presence != NULL &&
         ports->revoke_connection != NULL;
}

aiqa_pairing_lifecycle_policy_t aiqa_pairing_lifecycle_default_policy(void) {
  return (aiqa_pairing_lifecycle_policy_t){
      .window_ms = AIQA_PAIRING_WINDOW_MS,
      .step_ms = AIQA_PAIRING_STEP_MS,
      .session_idle_ms = AIQA_PAIRING_SESSION_IDLE_MS,
      .session_absolute_ms = AIQA_PAIRING_SESSION_ABSOLUTE_MS,
      .max_attempts = AIQA_PAIRING_MAX_ATTEMPTS,
  };
}

bool aiqa_pairing_lifecycle_policy_is_safe(
    const aiqa_pairing_lifecycle_policy_t *policy) {
  return policy != NULL && policy->window_ms >= policy->step_ms &&
         policy->step_ms > 0 && policy->session_idle_ms > 0 &&
         policy->session_absolute_ms >= policy->session_idle_ms &&
         policy->window_ms <= AIQA_PAIRING_WINDOW_MS &&
         policy->step_ms <= AIQA_PAIRING_STEP_MS &&
         policy->session_idle_ms <= AIQA_PAIRING_SESSION_IDLE_MS &&
         policy->session_absolute_ms <= AIQA_PAIRING_SESSION_ABSOLUTE_MS &&
         policy->max_attempts > 0 &&
         policy->max_attempts <= AIQA_PAIRING_MAX_ATTEMPTS;
}

static bool clear_code(aiqa_pairing_lifecycle_t *lifecycle) {
  const bool cleared = !lifecycle->code_visible ||
                       lifecycle->ports.clear_code(lifecycle->ports.context);
  secure_zero(lifecycle->code, sizeof(lifecycle->code));
  if (cleared)
    lifecycle->code_visible = false;
  return cleared;
}

static void revoke_connection(aiqa_pairing_lifecycle_t *lifecycle) {
  if (lifecycle->connection_id != 0) {
    lifecycle->ports.revoke_connection(lifecycle->ports.context,
                                       lifecycle->connection_id);
  }
  lifecycle->connection_id = 0;
  lifecycle->handshake_id = 0;
}

static bool clear_transient(aiqa_pairing_lifecycle_t *lifecycle) {
  revoke_connection(lifecycle);
  lifecycle->window_deadline_ms = 0;
  lifecycle->step_deadline_ms = 0;
  lifecycle->session_idle_deadline_ms = 0;
  lifecycle->session_absolute_deadline_ms = 0;
  return clear_code(lifecycle);
}

static void settle_inactive_or_locked(aiqa_pairing_lifecycle_t *lifecycle) {
  lifecycle->state =
      lifecycle->lock_record.attempts_used >= lifecycle->policy.max_attempts
          ? AIQA_PAIRING_LIFECYCLE_LOCKED
          : AIQA_PAIRING_LIFECYCLE_INACTIVE;
}

static void enter_fault(aiqa_pairing_lifecycle_t *lifecycle) {
  (void)clear_transient(lifecycle);
  lifecycle->state = AIQA_PAIRING_LIFECYCLE_FAULT;
}

static aiqa_pairing_lifecycle_result_t
fault_after_clear_failure(aiqa_pairing_lifecycle_t *lifecycle) {
  revoke_connection(lifecycle);
  lifecycle->window_deadline_ms = 0;
  lifecycle->step_deadline_ms = 0;
  lifecycle->session_idle_deadline_ms = 0;
  lifecycle->session_absolute_deadline_ms = 0;
  lifecycle->state = AIQA_PAIRING_LIFECYCLE_FAULT;
  return AIQA_PAIRING_LIFECYCLE_DISPLAY_FAILED;
}

static aiqa_pairing_lifecycle_result_t
read_clock(aiqa_pairing_lifecycle_t *lifecycle, uint64_t *out_now_ms) {
  uint64_t now_ms = 0;
  if (!lifecycle->ports.monotonic_ms(lifecycle->ports.context, &now_ms) ||
      (lifecycle->clock_initialized && now_ms < lifecycle->last_now_ms)) {
    enter_fault(lifecycle);
    return AIQA_PAIRING_LIFECYCLE_CLOCK_FAILED;
  }
  lifecycle->clock_initialized = true;
  lifecycle->last_now_ms = now_ms;
  *out_now_ms = now_ms;
  return AIQA_PAIRING_LIFECYCLE_OK;
}

static aiqa_pairing_lifecycle_result_t
expire_if_needed(aiqa_pairing_lifecycle_t *lifecycle, uint64_t now_ms) {
  bool expired = false;
  if (lifecycle->state == AIQA_PAIRING_LIFECYCLE_AVAILABLE) {
    expired = now_ms >= lifecycle->window_deadline_ms;
  } else if (lifecycle->state == AIQA_PAIRING_LIFECYCLE_HANDSHAKE ||
             lifecycle->state == AIQA_PAIRING_LIFECYCLE_WAIT_FINISHED_SENT) {
    expired = now_ms >= lifecycle->window_deadline_ms ||
              now_ms >= lifecycle->step_deadline_ms;
  } else if (lifecycle->state == AIQA_PAIRING_LIFECYCLE_ACTIVE) {
    expired = now_ms >= lifecycle->session_idle_deadline_ms ||
              now_ms >= lifecycle->session_absolute_deadline_ms;
  }
  if (expired) {
    if (!clear_transient(lifecycle))
      return fault_after_clear_failure(lifecycle);
    settle_inactive_or_locked(lifecycle);
  }
  return AIQA_PAIRING_LIFECYCLE_OK;
}

static aiqa_pairing_lifecycle_result_t
refresh(aiqa_pairing_lifecycle_t *lifecycle, uint64_t *out_now_ms) {
  if (lifecycle == NULL)
    return AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT;
  if (lifecycle->state == AIQA_PAIRING_LIFECYCLE_FAULT) {
    if (out_now_ms != NULL)
      *out_now_ms = lifecycle->last_now_ms;
    return AIQA_PAIRING_LIFECYCLE_OK;
  }
  uint64_t now_ms = 0;
  const aiqa_pairing_lifecycle_result_t result = read_clock(lifecycle, &now_ms);
  if (result != AIQA_PAIRING_LIFECYCLE_OK)
    return result;
  const aiqa_pairing_lifecycle_result_t expiry_result =
      expire_if_needed(lifecycle, now_ms);
  if (expiry_result != AIQA_PAIRING_LIFECYCLE_OK)
    return expiry_result;
  if (out_now_ms != NULL)
    *out_now_ms = now_ms;
  return AIQA_PAIRING_LIFECYCLE_OK;
}

aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_create(aiqa_pairing_lifecycle_t **out_lifecycle,
                              const aiqa_pairing_lifecycle_policy_t *policy,
                              const aiqa_pairing_lifecycle_ports_t *ports) {
  if (out_lifecycle == NULL || *out_lifecycle != NULL ||
      !aiqa_pairing_lifecycle_policy_is_safe(policy) || !ports_are_valid(ports))
    return AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT;
  aiqa_pairing_lifecycle_t *lifecycle = calloc(1, sizeof(*lifecycle));
  if (lifecycle == NULL)
    return AIQA_PAIRING_LIFECYCLE_FAULTED;
  lifecycle->policy = *policy;
  lifecycle->ports = *ports;
  lifecycle->lock_record.version = AIQA_PAIRING_LOCK_RECORD_VERSION;
  lifecycle->state = AIQA_PAIRING_LIFECYCLE_INACTIVE;

  const aiqa_pairing_persistence_result_t load_result =
      ports->load_lock_record(ports->context, &lifecycle->lock_record);
  if (load_result == AIQA_PAIRING_PERSISTENCE_NOT_FOUND) {
    lifecycle->lock_record = (aiqa_pairing_lock_record_t){
        .version = AIQA_PAIRING_LOCK_RECORD_VERSION,
        .attempts_used = 0,
    };
  } else if (load_result != AIQA_PAIRING_PERSISTENCE_OK ||
             lifecycle->lock_record.version !=
                 AIQA_PAIRING_LOCK_RECORD_VERSION ||
             lifecycle->lock_record.attempts_used > policy->max_attempts) {
    lifecycle->state = AIQA_PAIRING_LIFECYCLE_FAULT;
  } else {
    settle_inactive_or_locked(lifecycle);
  }

  if (lifecycle->state != AIQA_PAIRING_LIFECYCLE_FAULT) {
    uint64_t now_ms = 0;
    if (read_clock(lifecycle, &now_ms) != AIQA_PAIRING_LIFECYCLE_OK)
      lifecycle->state = AIQA_PAIRING_LIFECYCLE_FAULT;
  }
  *out_lifecycle = lifecycle;
  return AIQA_PAIRING_LIFECYCLE_OK;
}

void aiqa_pairing_lifecycle_destroy(aiqa_pairing_lifecycle_t **lifecycle) {
  if (lifecycle == NULL || *lifecycle == NULL)
    return;
  (void)clear_transient(*lifecycle);
  secure_zero(*lifecycle, sizeof(**lifecycle));
  free(*lifecycle);
  *lifecycle = NULL;
}

static bool generate_code(aiqa_pairing_lifecycle_t *lifecycle) {
  unsigned rejected = 0;
  for (size_t index = 0; index < AIQA_PAIRING_CODE_SIZE; ++index) {
    uint8_t value = UINT8_MAX;
    do {
      if (rejected >= AIQA_PAIRING_DIGIT_REJECTION_LIMIT ||
          lifecycle->ports.random_fill(lifecycle->ports.context, &value, 1) !=
              0) {
        secure_zero(lifecycle->code, sizeof(lifecycle->code));
        return false;
      }
      if (value >= 250U)
        rejected += 1U;
    } while (value >= 250U);
    lifecycle->code[index] = (uint8_t)('0' + value % 10U);
  }
  return true;
}

aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_local_open(aiqa_pairing_lifecycle_t *lifecycle,
                                  uint64_t connection_id) {
  if (connection_id == 0)
    return AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT;
  uint64_t now_ms = 0;
  const aiqa_pairing_lifecycle_result_t refresh_result =
      refresh(lifecycle, &now_ms);
  if (refresh_result != AIQA_PAIRING_LIFECYCLE_OK)
    return refresh_result;
  if (lifecycle->state == AIQA_PAIRING_LIFECYCLE_FAULT)
    return AIQA_PAIRING_LIFECYCLE_FAULTED;
  if (lifecycle->state == AIQA_PAIRING_LIFECYCLE_LOCKED)
    return AIQA_PAIRING_LIFECYCLE_LOCKED_OUT;
  if (lifecycle->state != AIQA_PAIRING_LIFECYCLE_INACTIVE)
    return AIQA_PAIRING_LIFECYCLE_INVALID_STATE;
  if (!lifecycle->ports.consume_local_presence(lifecycle->ports.context,
                                               AIQA_PAIRING_LOCAL_START))
    return AIQA_PAIRING_LIFECYCLE_LOCAL_PRESENCE_REQUIRED;
  if (!generate_code(lifecycle))
    return AIQA_PAIRING_LIFECYCLE_RANDOM_FAILED;

  lifecycle->code_visible = true;
  lifecycle->connection_id = connection_id;
  if (!lifecycle->ports.show_code(lifecycle->ports.context, lifecycle->code)) {
    enter_fault(lifecycle);
    return AIQA_PAIRING_LIFECYCLE_DISPLAY_FAILED;
  }
  lifecycle->window_deadline_ms =
      deadline_after(now_ms, lifecycle->policy.window_ms);
  lifecycle->state = AIQA_PAIRING_LIFECYCLE_AVAILABLE;
  return AIQA_PAIRING_LIFECYCLE_OK;
}

static bool session_matches(const aiqa_pairing_lifecycle_t *lifecycle,
                            uint64_t connection_id, uint64_t handshake_id) {
  return lifecycle->connection_id == connection_id &&
         lifecycle->handshake_id == handshake_id;
}

aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_begin(aiqa_pairing_lifecycle_t *lifecycle,
                             uint64_t connection_id, uint64_t handshake_id,
                             aiqa_pairing_code_consumer_fn consume_code,
                             void *consumer_context) {
  if (connection_id == 0 || handshake_id == 0 || consume_code == NULL)
    return AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT;
  uint64_t now_ms = 0;
  const aiqa_pairing_lifecycle_result_t refresh_result =
      refresh(lifecycle, &now_ms);
  if (refresh_result != AIQA_PAIRING_LIFECYCLE_OK)
    return refresh_result;
  if (lifecycle->state == AIQA_PAIRING_LIFECYCLE_HANDSHAKE ||
      lifecycle->state == AIQA_PAIRING_LIFECYCLE_WAIT_FINISHED_SENT ||
      lifecycle->state == AIQA_PAIRING_LIFECYCLE_ACTIVE)
    return AIQA_PAIRING_LIFECYCLE_BUSY;
  if (lifecycle->state != AIQA_PAIRING_LIFECYCLE_AVAILABLE)
    return lifecycle->state == AIQA_PAIRING_LIFECYCLE_LOCKED
               ? AIQA_PAIRING_LIFECYCLE_LOCKED_OUT
               : AIQA_PAIRING_LIFECYCLE_INVALID_STATE;
  if (lifecycle->connection_id != connection_id)
    return AIQA_PAIRING_LIFECYCLE_INVALID_SESSION;

  const aiqa_pairing_lock_record_t next_record = {
      .version = AIQA_PAIRING_LOCK_RECORD_VERSION,
      .attempts_used = lifecycle->lock_record.attempts_used + 1U,
  };
  if (!lifecycle->ports.store_lock_record(lifecycle->ports.context,
                                          &next_record)) {
    enter_fault(lifecycle);
    return AIQA_PAIRING_LIFECYCLE_PERSISTENCE_ERROR;
  }
  lifecycle->lock_record = next_record;
  lifecycle->handshake_id = handshake_id;
  lifecycle->step_deadline_ms =
      earlier_deadline(deadline_after(now_ms, lifecycle->policy.step_ms),
                       lifecycle->window_deadline_ms);
  lifecycle->state = AIQA_PAIRING_LIFECYCLE_HANDSHAKE;
  const bool consumed = consume_code(consumer_context, lifecycle->code,
                                     connection_id, handshake_id);
  if (!clear_code(lifecycle))
    return fault_after_clear_failure(lifecycle);
  if (!consumed) {
    revoke_connection(lifecycle);
    settle_inactive_or_locked(lifecycle);
    return AIQA_PAIRING_LIFECYCLE_START_FAILED;
  }
  return AIQA_PAIRING_LIFECYCLE_OK;
}

aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_handshake_progress(aiqa_pairing_lifecycle_t *lifecycle,
                                          uint64_t connection_id,
                                          uint64_t handshake_id) {
  uint64_t now_ms = 0;
  const aiqa_pairing_lifecycle_result_t result = refresh(lifecycle, &now_ms);
  if (result != AIQA_PAIRING_LIFECYCLE_OK)
    return result;
  if (lifecycle->state != AIQA_PAIRING_LIFECYCLE_HANDSHAKE)
    return AIQA_PAIRING_LIFECYCLE_INVALID_STATE;
  if (!session_matches(lifecycle, connection_id, handshake_id))
    return AIQA_PAIRING_LIFECYCLE_INVALID_SESSION;
  lifecycle->step_deadline_ms =
      earlier_deadline(deadline_after(now_ms, lifecycle->policy.step_ms),
                       lifecycle->window_deadline_ms);
  return AIQA_PAIRING_LIFECYCLE_OK;
}

aiqa_pairing_lifecycle_result_t aiqa_pairing_lifecycle_client_finished_verified(
    aiqa_pairing_lifecycle_t *lifecycle, uint64_t connection_id,
    uint64_t handshake_id) {
  const aiqa_pairing_lifecycle_result_t progress_result =
      aiqa_pairing_lifecycle_handshake_progress(lifecycle, connection_id,
                                                handshake_id);
  if (progress_result != AIQA_PAIRING_LIFECYCLE_OK)
    return progress_result;
  lifecycle->state = AIQA_PAIRING_LIFECYCLE_WAIT_FINISHED_SENT;
  return AIQA_PAIRING_LIFECYCLE_OK;
}

aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_device_finished_sent(aiqa_pairing_lifecycle_t *lifecycle,
                                            uint64_t connection_id,
                                            uint64_t handshake_id) {
  uint64_t now_ms = 0;
  const aiqa_pairing_lifecycle_result_t result = refresh(lifecycle, &now_ms);
  if (result != AIQA_PAIRING_LIFECYCLE_OK)
    return result;
  if (lifecycle->state != AIQA_PAIRING_LIFECYCLE_WAIT_FINISHED_SENT)
    return AIQA_PAIRING_LIFECYCLE_INVALID_STATE;
  if (!session_matches(lifecycle, connection_id, handshake_id))
    return AIQA_PAIRING_LIFECYCLE_INVALID_SESSION;
  const aiqa_pairing_lock_record_t reset_record = {
      .version = AIQA_PAIRING_LOCK_RECORD_VERSION,
      .attempts_used = 0,
  };
  if (!lifecycle->ports.store_lock_record(lifecycle->ports.context,
                                          &reset_record)) {
    enter_fault(lifecycle);
    return AIQA_PAIRING_LIFECYCLE_PERSISTENCE_ERROR;
  }
  lifecycle->lock_record = reset_record;
  lifecycle->session_idle_deadline_ms =
      deadline_after(now_ms, lifecycle->policy.session_idle_ms);
  lifecycle->session_absolute_deadline_ms =
      deadline_after(now_ms, lifecycle->policy.session_absolute_ms);
  lifecycle->state = AIQA_PAIRING_LIFECYCLE_ACTIVE;
  return AIQA_PAIRING_LIFECYCLE_OK;
}

aiqa_pairing_lifecycle_result_t aiqa_pairing_lifecycle_authenticated_activity(
    aiqa_pairing_lifecycle_t *lifecycle, uint64_t connection_id) {
  uint64_t now_ms = 0;
  const aiqa_pairing_lifecycle_result_t result = refresh(lifecycle, &now_ms);
  if (result != AIQA_PAIRING_LIFECYCLE_OK)
    return result;
  if (lifecycle->state != AIQA_PAIRING_LIFECYCLE_ACTIVE)
    return AIQA_PAIRING_LIFECYCLE_INVALID_STATE;
  if (lifecycle->connection_id != connection_id)
    return AIQA_PAIRING_LIFECYCLE_INVALID_SESSION;
  lifecycle->session_idle_deadline_ms = earlier_deadline(
      deadline_after(now_ms, lifecycle->policy.session_idle_ms),
      lifecycle->session_absolute_deadline_ms);
  return AIQA_PAIRING_LIFECYCLE_OK;
}

aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_protocol_failed(aiqa_pairing_lifecycle_t *lifecycle,
                                       uint64_t connection_id,
                                       uint64_t handshake_id) {
  const aiqa_pairing_lifecycle_result_t result = refresh(lifecycle, NULL);
  if (result != AIQA_PAIRING_LIFECYCLE_OK)
    return result;
  if ((lifecycle->state != AIQA_PAIRING_LIFECYCLE_HANDSHAKE &&
       lifecycle->state != AIQA_PAIRING_LIFECYCLE_WAIT_FINISHED_SENT) ||
      !session_matches(lifecycle, connection_id, handshake_id))
    return AIQA_PAIRING_LIFECYCLE_INVALID_SESSION;
  if (!clear_transient(lifecycle))
    return fault_after_clear_failure(lifecycle);
  settle_inactive_or_locked(lifecycle);
  return AIQA_PAIRING_LIFECYCLE_OK;
}

aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_disconnect(aiqa_pairing_lifecycle_t *lifecycle,
                                  uint64_t connection_id) {
  const aiqa_pairing_lifecycle_result_t result = refresh(lifecycle, NULL);
  if (result != AIQA_PAIRING_LIFECYCLE_OK)
    return result;
  if (lifecycle->connection_id == 0 ||
      lifecycle->connection_id != connection_id)
    return AIQA_PAIRING_LIFECYCLE_INVALID_SESSION;
  if (!clear_transient(lifecycle))
    return fault_after_clear_failure(lifecycle);
  settle_inactive_or_locked(lifecycle);
  return AIQA_PAIRING_LIFECYCLE_OK;
}

aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_tick(aiqa_pairing_lifecycle_t *lifecycle) {
  return refresh(lifecycle, NULL);
}

aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_local_reset(aiqa_pairing_lifecycle_t *lifecycle) {
  if (lifecycle == NULL)
    return AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT;
  if (!lifecycle->ports.consume_local_presence(lifecycle->ports.context,
                                               AIQA_PAIRING_LOCAL_RESET))
    return AIQA_PAIRING_LIFECYCLE_LOCAL_PRESENCE_REQUIRED;
  if (!clear_transient(lifecycle))
    return fault_after_clear_failure(lifecycle);
  const aiqa_pairing_lock_record_t reset_record = {
      .version = AIQA_PAIRING_LOCK_RECORD_VERSION,
      .attempts_used = 0,
  };
  if (!lifecycle->ports.store_lock_record(lifecycle->ports.context,
                                          &reset_record)) {
    lifecycle->state = AIQA_PAIRING_LIFECYCLE_FAULT;
    return AIQA_PAIRING_LIFECYCLE_PERSISTENCE_ERROR;
  }
  lifecycle->lock_record = reset_record;
  lifecycle->state = AIQA_PAIRING_LIFECYCLE_INACTIVE;
  return AIQA_PAIRING_LIFECYCLE_OK;
}

aiqa_pairing_lifecycle_result_t
aiqa_pairing_lifecycle_get_status(aiqa_pairing_lifecycle_t *lifecycle,
                                  aiqa_pairing_lifecycle_status_t *out_status) {
  if (lifecycle == NULL || out_status == NULL)
    return AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT;
  if (lifecycle->state != AIQA_PAIRING_LIFECYCLE_FAULT) {
    const aiqa_pairing_lifecycle_result_t result = refresh(lifecycle, NULL);
    if (result != AIQA_PAIRING_LIFECYCLE_OK)
      return result;
  }
  uint64_t deadline_ms = 0;
  if (lifecycle->state == AIQA_PAIRING_LIFECYCLE_AVAILABLE)
    deadline_ms = lifecycle->window_deadline_ms;
  else if (lifecycle->state == AIQA_PAIRING_LIFECYCLE_HANDSHAKE ||
           lifecycle->state == AIQA_PAIRING_LIFECYCLE_WAIT_FINISHED_SENT)
    deadline_ms = earlier_deadline(lifecycle->window_deadline_ms,
                                   lifecycle->step_deadline_ms);
  else if (lifecycle->state == AIQA_PAIRING_LIFECYCLE_ACTIVE)
    deadline_ms = earlier_deadline(lifecycle->session_idle_deadline_ms,
                                   lifecycle->session_absolute_deadline_ms);
  *out_status = (aiqa_pairing_lifecycle_status_t){
      .state = lifecycle->state,
      .attempts_remaining =
          lifecycle->lock_record.attempts_used >= lifecycle->policy.max_attempts
              ? 0
              : lifecycle->policy.max_attempts -
                    lifecycle->lock_record.attempts_used,
      .remaining_ms = deadline_ms > lifecycle->last_now_ms
                          ? deadline_ms - lifecycle->last_now_ms
                          : 0,
  };
  return AIQA_PAIRING_LIFECYCLE_OK;
}
