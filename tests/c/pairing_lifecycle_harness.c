#include "aiqa_pairing_lifecycle.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  uint64_t now_ms;
  bool clock_ok;
  aiqa_pairing_persistence_result_t load_result;
  aiqa_pairing_lock_record_t record;
  bool store_ok;
  unsigned store_calls;
  uint8_t random_bytes[64];
  size_t random_length;
  size_t random_offset;
  bool random_ok;
  bool show_ok;
  bool clear_ok;
  uint8_t shown_code[AIQA_PAIRING_CODE_SIZE];
  unsigned show_calls;
  unsigned clear_calls;
  unsigned start_presence;
  unsigned reset_presence;
  unsigned revoke_calls;
  uint64_t revoked_connection;
  unsigned callback_sequence;
  unsigned clear_sequence;
  unsigned revoke_sequence;
  unsigned consume_calls;
  bool consume_ok;
  uint32_t attempts_seen_by_consumer;
} fake_ports_t;

static bool fake_now(void *context, uint64_t *out_now_ms) {
  fake_ports_t *fake = context;
  if (!fake->clock_ok)
    return false;
  *out_now_ms = fake->now_ms;
  return true;
}

static int fake_random(void *context, unsigned char *output,
                       size_t output_length) {
  fake_ports_t *fake = context;
  if (!fake->random_ok ||
      output_length > fake->random_length - fake->random_offset)
    return -1;
  (void)memcpy(output, fake->random_bytes + fake->random_offset, output_length);
  fake->random_offset += output_length;
  return 0;
}

static aiqa_pairing_persistence_result_t
fake_load(void *context, aiqa_pairing_lock_record_t *out_record) {
  fake_ports_t *fake = context;
  if (fake->load_result == AIQA_PAIRING_PERSISTENCE_OK)
    *out_record = fake->record;
  return fake->load_result;
}

static bool fake_store(void *context,
                       const aiqa_pairing_lock_record_t *record) {
  fake_ports_t *fake = context;
  fake->store_calls += 1U;
  if (!fake->store_ok)
    return false;
  fake->record = *record;
  fake->load_result = AIQA_PAIRING_PERSISTENCE_OK;
  return true;
}

static bool fake_show(void *context,
                      const uint8_t code[AIQA_PAIRING_CODE_SIZE]) {
  fake_ports_t *fake = context;
  fake->show_calls += 1U;
  (void)memcpy(fake->shown_code, code, AIQA_PAIRING_CODE_SIZE);
  return fake->show_ok;
}

static bool fake_clear(void *context) {
  fake_ports_t *fake = context;
  fake->clear_sequence = ++fake->callback_sequence;
  fake->clear_calls += 1U;
  if (fake->clear_ok)
    (void)memset(fake->shown_code, 0, sizeof(fake->shown_code));
  return fake->clear_ok;
}

static bool fake_presence(void *context, aiqa_pairing_local_action_t action) {
  fake_ports_t *fake = context;
  unsigned *remaining = action == AIQA_PAIRING_LOCAL_START
                            ? &fake->start_presence
                            : &fake->reset_presence;
  if (*remaining == 0)
    return false;
  *remaining -= 1U;
  return true;
}

static void fake_revoke(void *context, uint64_t connection_id) {
  fake_ports_t *fake = context;
  fake->revoke_sequence = ++fake->callback_sequence;
  fake->revoke_calls += 1U;
  fake->revoked_connection = connection_id;
}

static bool fake_consume_code(void *context,
                              const uint8_t code[AIQA_PAIRING_CODE_SIZE],
                              uint64_t connection_id, uint64_t handshake_id) {
  fake_ports_t *fake = context;
  assert(connection_id != 0 && handshake_id != 0);
  for (size_t index = 0; index < AIQA_PAIRING_CODE_SIZE; ++index)
    assert(code[index] >= '0' && code[index] <= '9');
  fake->consume_calls += 1U;
  fake->attempts_seen_by_consumer = fake->record.attempts_used;
  return fake->consume_ok;
}

static fake_ports_t default_fake(void) {
  fake_ports_t fake = {
      .clock_ok = true,
      .load_result = AIQA_PAIRING_PERSISTENCE_NOT_FOUND,
      .store_ok = true,
      .random_ok = true,
      .show_ok = true,
      .clear_ok = true,
      .consume_ok = true,
  };
  for (size_t index = 0; index < sizeof(fake.random_bytes); ++index)
    fake.random_bytes[index] = (uint8_t)index;
  fake.random_length = sizeof(fake.random_bytes);
  return fake;
}

static aiqa_pairing_lifecycle_ports_t ports_for(fake_ports_t *fake) {
  return (aiqa_pairing_lifecycle_ports_t){
      .context = fake,
      .monotonic_ms = fake_now,
      .random_fill = fake_random,
      .load_lock_record = fake_load,
      .store_lock_record = fake_store,
      .show_code = fake_show,
      .clear_code = fake_clear,
      .consume_local_presence = fake_presence,
      .revoke_connection = fake_revoke,
  };
}

static aiqa_pairing_lifecycle_t *create_lifecycle(fake_ports_t *fake) {
  aiqa_pairing_lifecycle_t *lifecycle = NULL;
  const aiqa_pairing_lifecycle_ports_t ports = ports_for(fake);
  const aiqa_pairing_lifecycle_policy_t policy =
      aiqa_pairing_lifecycle_default_policy();
  assert(aiqa_pairing_lifecycle_create(&lifecycle, &policy, &ports) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  assert(lifecycle != NULL);
  return lifecycle;
}

static aiqa_pairing_lifecycle_status_t
status_of(aiqa_pairing_lifecycle_t *lifecycle) {
  aiqa_pairing_lifecycle_status_t status = {0};
  assert(aiqa_pairing_lifecycle_get_status(lifecycle, &status) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  return status;
}

static void open_window(aiqa_pairing_lifecycle_t *lifecycle, fake_ports_t *fake,
                        uint64_t connection_id) {
  fake->start_presence += 1U;
  assert(aiqa_pairing_lifecycle_local_open(lifecycle, connection_id) ==
         AIQA_PAIRING_LIFECYCLE_OK);
}

static void run_open_expiry(void) {
  fake_ports_t fake = default_fake();
  fake.random_bytes[0] = 250U;
  fake.random_bytes[1] = 0U;
  fake.random_bytes[2] = 0U;
  fake.random_bytes[3] = 0U;
  fake.random_bytes[4] = 0U;
  fake.random_bytes[5] = 0U;
  fake.random_bytes[6] = 0U;
  fake.random_bytes[7] = 4U;
  fake.random_bytes[8] = 2U;
  aiqa_pairing_lifecycle_t *lifecycle = create_lifecycle(&fake);

  assert(aiqa_pairing_lifecycle_local_open(lifecycle, 41) ==
         AIQA_PAIRING_LIFECYCLE_LOCAL_PRESENCE_REQUIRED);
  assert(fake.show_calls == 0 && fake.random_offset == 0);
  open_window(lifecycle, &fake, 41);
  assert(memcmp(fake.shown_code, "00000042", AIQA_PAIRING_CODE_SIZE) == 0);
  aiqa_pairing_lifecycle_status_t status = status_of(lifecycle);
  assert(status.state == AIQA_PAIRING_LIFECYCLE_AVAILABLE);
  assert(status.remaining_ms == AIQA_PAIRING_WINDOW_MS);
  assert(status.attempts_remaining == AIQA_PAIRING_MAX_ATTEMPTS);

  fake.now_ms = AIQA_PAIRING_WINDOW_MS - 1U;
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_AVAILABLE);
  fake.now_ms = AIQA_PAIRING_WINDOW_MS;
  assert(aiqa_pairing_lifecycle_tick(lifecycle) == AIQA_PAIRING_LIFECYCLE_OK);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_INACTIVE);
  assert(fake.clear_calls == 1);
  aiqa_pairing_lifecycle_destroy(&lifecycle);
  assert(lifecycle == NULL);
}

static void run_attempt_lock(void) {
  fake_ports_t fake = default_fake();
  aiqa_pairing_lifecycle_t *lifecycle = create_lifecycle(&fake);

  for (uint32_t attempt = 1; attempt <= AIQA_PAIRING_MAX_ATTEMPTS; ++attempt) {
    open_window(lifecycle, &fake, attempt);
    assert(aiqa_pairing_lifecycle_begin(lifecycle, attempt, 100U + attempt,
                                        fake_consume_code,
                                        &fake) == AIQA_PAIRING_LIFECYCLE_OK);
    assert(fake.record.attempts_used == attempt);
    assert(fake.attempts_seen_by_consumer == attempt);
    assert(aiqa_pairing_lifecycle_disconnect(lifecycle, attempt) ==
           AIQA_PAIRING_LIFECYCLE_OK);
  }
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_LOCKED);
  assert(fake.revoke_calls == AIQA_PAIRING_MAX_ATTEMPTS);
  aiqa_pairing_lifecycle_destroy(&lifecycle);

  lifecycle = create_lifecycle(&fake);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_LOCKED);
  const size_t random_offset = fake.random_offset;
  fake.start_presence = 1;
  assert(aiqa_pairing_lifecycle_local_open(lifecycle, 1) ==
         AIQA_PAIRING_LIFECYCLE_LOCKED_OUT);
  assert(fake.random_offset == random_offset);

  assert(aiqa_pairing_lifecycle_local_reset(lifecycle) ==
         AIQA_PAIRING_LIFECYCLE_LOCAL_PRESENCE_REQUIRED);
  fake.reset_presence = 1;
  assert(aiqa_pairing_lifecycle_local_reset(lifecycle) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  assert(fake.record.attempts_used == 0);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_INACTIVE);
  aiqa_pairing_lifecycle_destroy(&lifecycle);
}

static void run_success_session(void) {
  fake_ports_t fake = default_fake();
  aiqa_pairing_lifecycle_t *lifecycle = create_lifecycle(&fake);
  open_window(lifecycle, &fake, 7);
  assert(aiqa_pairing_lifecycle_begin(lifecycle, 7, 9, fake_consume_code,
                                      &fake) == AIQA_PAIRING_LIFECYCLE_OK);
  assert(aiqa_pairing_lifecycle_client_finished_verified(lifecycle, 7, 9) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  assert(status_of(lifecycle).state ==
         AIQA_PAIRING_LIFECYCLE_WAIT_FINISHED_SENT);
  assert(aiqa_pairing_lifecycle_device_finished_sent(lifecycle, 7, 9) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  assert(fake.record.attempts_used == 0);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_ACTIVE);
  assert(aiqa_pairing_lifecycle_device_finished_sent(lifecycle, 7, 9) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_STATE);

  fake.now_ms = AIQA_PAIRING_SESSION_IDLE_MS - 1U;
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_ACTIVE);
  fake.now_ms = AIQA_PAIRING_SESSION_IDLE_MS;
  assert(aiqa_pairing_lifecycle_tick(lifecycle) == AIQA_PAIRING_LIFECYCLE_OK);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_INACTIVE);
  assert(fake.revoke_calls == 1 && fake.revoked_connection == 7);
  aiqa_pairing_lifecycle_destroy(&lifecycle);
}

static void run_faults(void) {
  fake_ports_t fake = default_fake();
  fake.load_result = AIQA_PAIRING_PERSISTENCE_ERROR;
  aiqa_pairing_lifecycle_t *lifecycle = create_lifecycle(&fake);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_FAULT);
  fake.start_presence = 1;
  assert(aiqa_pairing_lifecycle_local_open(lifecycle, 1) ==
         AIQA_PAIRING_LIFECYCLE_FAULTED);
  fake.reset_presence = 1;
  fake.store_ok = false;
  assert(aiqa_pairing_lifecycle_local_reset(lifecycle) ==
         AIQA_PAIRING_LIFECYCLE_PERSISTENCE_ERROR);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_FAULT);
  fake.reset_presence = 1;
  fake.store_ok = true;
  assert(aiqa_pairing_lifecycle_local_reset(lifecycle) ==
         AIQA_PAIRING_LIFECYCLE_OK);

  fake.random_ok = false;
  fake.start_presence = 1;
  assert(aiqa_pairing_lifecycle_local_open(lifecycle, 1) ==
         AIQA_PAIRING_LIFECYCLE_RANDOM_FAILED);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_INACTIVE);
  fake.random_ok = true;
  fake.show_ok = false;
  fake.start_presence = 1;
  assert(aiqa_pairing_lifecycle_local_open(lifecycle, 1) ==
         AIQA_PAIRING_LIFECYCLE_DISPLAY_FAILED);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_FAULT);
  fake.reset_presence = 1;
  assert(aiqa_pairing_lifecycle_local_reset(lifecycle) ==
         AIQA_PAIRING_LIFECYCLE_OK);

  fake.show_ok = true;
  open_window(lifecycle, &fake, 1);
  fake.store_ok = false;
  assert(
      aiqa_pairing_lifecycle_begin(lifecycle, 1, 1, fake_consume_code, &fake) ==
      AIQA_PAIRING_LIFECYCLE_PERSISTENCE_ERROR);
  assert(fake.consume_calls == 0);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_FAULT);
  aiqa_pairing_lifecycle_destroy(&lifecycle);

  fake = default_fake();
  fake.now_ms = 1000;
  lifecycle = create_lifecycle(&fake);
  open_window(lifecycle, &fake, 1);
  fake.now_ms = 999;
  assert(aiqa_pairing_lifecycle_tick(lifecycle) ==
         AIQA_PAIRING_LIFECYCLE_CLOCK_FAILED);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_FAULT);
  aiqa_pairing_lifecycle_destroy(&lifecycle);
}

static void run_single_handshake(void) {
  fake_ports_t fake = default_fake();
  aiqa_pairing_lifecycle_t *lifecycle = create_lifecycle(&fake);
  open_window(lifecycle, &fake, 11);
  assert(aiqa_pairing_lifecycle_begin(lifecycle, 11, 12, fake_consume_code,
                                      &fake) == AIQA_PAIRING_LIFECYCLE_OK);
  assert(aiqa_pairing_lifecycle_begin(lifecycle, 13, 14, fake_consume_code,
                                      &fake) == AIQA_PAIRING_LIFECYCLE_BUSY);
  assert(aiqa_pairing_lifecycle_handshake_progress(lifecycle, 13, 14) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_SESSION);
  fake.now_ms = AIQA_PAIRING_STEP_MS - 1U;
  assert(aiqa_pairing_lifecycle_handshake_progress(lifecycle, 11, 12) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  fake.now_ms += AIQA_PAIRING_STEP_MS - 1U;
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_HANDSHAKE);
  fake.now_ms += 1U;
  assert(aiqa_pairing_lifecycle_tick(lifecycle) == AIQA_PAIRING_LIFECYCLE_OK);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_INACTIVE);
  assert(fake.record.attempts_used == 1);
  assert(fake.revoke_calls == 1 && fake.revoked_connection == 11);
  aiqa_pairing_lifecycle_destroy(&lifecycle);
}

static void run_boundaries(void) {
  aiqa_pairing_lifecycle_status_t unused_status = {0};
  assert(aiqa_pairing_lifecycle_tick(NULL) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT);
  assert(aiqa_pairing_lifecycle_local_reset(NULL) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT);
  assert(aiqa_pairing_lifecycle_get_status(NULL, &unused_status) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT);
  aiqa_pairing_lifecycle_policy_t policy =
      aiqa_pairing_lifecycle_default_policy();
  assert(!aiqa_pairing_lifecycle_policy_is_safe(NULL));
  policy.step_ms = 0;
  assert(!aiqa_pairing_lifecycle_policy_is_safe(&policy));
  policy = aiqa_pairing_lifecycle_default_policy();
  policy.window_ms = policy.step_ms - 1U;
  assert(!aiqa_pairing_lifecycle_policy_is_safe(&policy));
  policy = aiqa_pairing_lifecycle_default_policy();
  policy.session_idle_ms = 0;
  assert(!aiqa_pairing_lifecycle_policy_is_safe(&policy));
  policy = aiqa_pairing_lifecycle_default_policy();
  policy.session_absolute_ms = policy.session_idle_ms - 1U;
  assert(!aiqa_pairing_lifecycle_policy_is_safe(&policy));
  policy = aiqa_pairing_lifecycle_default_policy();
  policy.max_attempts = 0;
  assert(!aiqa_pairing_lifecycle_policy_is_safe(&policy));
  policy.max_attempts = UINT8_MAX + 1U;
  assert(!aiqa_pairing_lifecycle_policy_is_safe(&policy));
  policy = aiqa_pairing_lifecycle_default_policy();
  policy.max_attempts = AIQA_PAIRING_MAX_ATTEMPTS + 1U;
  assert(!aiqa_pairing_lifecycle_policy_is_safe(&policy));
  policy = aiqa_pairing_lifecycle_default_policy();
  policy.window_ms = AIQA_PAIRING_WINDOW_MS + 1U;
  assert(!aiqa_pairing_lifecycle_policy_is_safe(&policy));
  policy = aiqa_pairing_lifecycle_default_policy();
  policy.step_ms = AIQA_PAIRING_STEP_MS + 1U;
  assert(!aiqa_pairing_lifecycle_policy_is_safe(&policy));
  policy = aiqa_pairing_lifecycle_default_policy();
  policy.session_idle_ms = AIQA_PAIRING_SESSION_IDLE_MS + 1U;
  assert(!aiqa_pairing_lifecycle_policy_is_safe(&policy));
  policy = aiqa_pairing_lifecycle_default_policy();
  policy.session_absolute_ms = AIQA_PAIRING_SESSION_ABSOLUTE_MS + 1U;
  assert(!aiqa_pairing_lifecycle_policy_is_safe(&policy));

  fake_ports_t fake = default_fake();
  fake.load_result = AIQA_PAIRING_PERSISTENCE_OK;
  fake.record.version = AIQA_PAIRING_LOCK_RECORD_VERSION + 1U;
  aiqa_pairing_lifecycle_t *lifecycle = create_lifecycle(&fake);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_FAULT);
  aiqa_pairing_lifecycle_destroy(&lifecycle);
  aiqa_pairing_lifecycle_destroy(&lifecycle);

  fake = default_fake();
  lifecycle = create_lifecycle(&fake);
  assert(aiqa_pairing_lifecycle_get_status(lifecycle, NULL) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT);
  assert(aiqa_pairing_lifecycle_handshake_progress(lifecycle, 1, 1) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_STATE);
  assert(aiqa_pairing_lifecycle_authenticated_activity(lifecycle, 1) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_STATE);
  open_window(lifecycle, &fake, 1);
  fake.start_presence = 1;
  assert(aiqa_pairing_lifecycle_local_open(lifecycle, 1) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_STATE);
  assert(
      aiqa_pairing_lifecycle_begin(lifecycle, 0, 1, fake_consume_code, &fake) ==
      AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT);
  assert(
      aiqa_pairing_lifecycle_begin(lifecycle, 1, 0, fake_consume_code, &fake) ==
      AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT);
  assert(aiqa_pairing_lifecycle_begin(lifecycle, 1, 1, NULL, &fake) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT);
  fake.consume_ok = false;
  assert(
      aiqa_pairing_lifecycle_begin(lifecycle, 1, 1, fake_consume_code, &fake) ==
      AIQA_PAIRING_LIFECYCLE_START_FAILED);
  assert(fake.record.attempts_used == 1 && fake.revoke_calls == 1);

  fake.consume_ok = true;
  open_window(lifecycle, &fake, 2);
  assert(aiqa_pairing_lifecycle_begin(lifecycle, 2, 2, fake_consume_code,
                                      &fake) == AIQA_PAIRING_LIFECYCLE_OK);
  assert(aiqa_pairing_lifecycle_protocol_failed(lifecycle, 9, 9) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_SESSION);
  assert(aiqa_pairing_lifecycle_protocol_failed(lifecycle, 2, 2) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  assert(fake.record.attempts_used == 2);

  open_window(lifecycle, &fake, 3);
  assert(aiqa_pairing_lifecycle_begin(lifecycle, 3, 3, fake_consume_code,
                                      &fake) == AIQA_PAIRING_LIFECYCLE_OK);
  assert(aiqa_pairing_lifecycle_client_finished_verified(lifecycle, 8, 8) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_SESSION);
  assert(aiqa_pairing_lifecycle_client_finished_verified(lifecycle, 3, 3) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  assert(aiqa_pairing_lifecycle_device_finished_sent(lifecycle, 8, 8) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_SESSION);
  fake.store_ok = false;
  assert(aiqa_pairing_lifecycle_device_finished_sent(lifecycle, 3, 3) ==
         AIQA_PAIRING_LIFECYCLE_PERSISTENCE_ERROR);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_FAULT);
  aiqa_pairing_lifecycle_destroy(&lifecycle);

  fake = default_fake();
  lifecycle = create_lifecycle(&fake);
  open_window(lifecycle, &fake, 4);
  assert(aiqa_pairing_lifecycle_begin(lifecycle, 4, 4, fake_consume_code,
                                      &fake) == AIQA_PAIRING_LIFECYCLE_OK);
  assert(aiqa_pairing_lifecycle_client_finished_verified(lifecycle, 4, 4) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  assert(aiqa_pairing_lifecycle_device_finished_sent(lifecycle, 4, 4) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  assert(aiqa_pairing_lifecycle_authenticated_activity(lifecycle, 99) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_SESSION);
  fake.now_ms = 1000;
  assert(aiqa_pairing_lifecycle_authenticated_activity(lifecycle, 4) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  assert(status_of(lifecycle).remaining_ms == AIQA_PAIRING_SESSION_IDLE_MS);
  assert(aiqa_pairing_lifecycle_disconnect(lifecycle, 99) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_SESSION);
  assert(aiqa_pairing_lifecycle_disconnect(lifecycle, 4) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_INACTIVE);

  open_window(lifecycle, &fake, 5);
  fake.clock_ok = false;
  assert(
      aiqa_pairing_lifecycle_begin(lifecycle, 5, 5, fake_consume_code, &fake) ==
      AIQA_PAIRING_LIFECYCLE_CLOCK_FAILED);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_FAULT);
  aiqa_pairing_lifecycle_destroy(&lifecycle);
}

static void run_temporal_ceilings(void) {
  fake_ports_t fake = default_fake();
  aiqa_pairing_lifecycle_t *lifecycle = create_lifecycle(&fake);

  open_window(lifecycle, &fake, 31);
  assert(aiqa_pairing_lifecycle_begin(lifecycle, 31, 1, fake_consume_code,
                                      &fake) == AIQA_PAIRING_LIFECYCLE_OK);
  for (fake.now_ms = 14000; fake.now_ms < AIQA_PAIRING_WINDOW_MS;
       fake.now_ms += 14000) {
    assert(aiqa_pairing_lifecycle_handshake_progress(lifecycle, 31, 1) ==
           AIQA_PAIRING_LIFECYCLE_OK);
  }
  fake.now_ms = AIQA_PAIRING_WINDOW_MS - 1U;
  assert(aiqa_pairing_lifecycle_handshake_progress(lifecycle, 31, 1) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  fake.now_ms = AIQA_PAIRING_WINDOW_MS;
  assert(aiqa_pairing_lifecycle_tick(lifecycle) == AIQA_PAIRING_LIFECYCLE_OK);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_INACTIVE);
  assert(fake.record.attempts_used == 1);
  aiqa_pairing_lifecycle_destroy(&lifecycle);

  fake = default_fake();
  lifecycle = create_lifecycle(&fake);
  open_window(lifecycle, &fake, 32);
  assert(aiqa_pairing_lifecycle_begin(lifecycle, 32, 2, fake_consume_code,
                                      &fake) == AIQA_PAIRING_LIFECYCLE_OK);
  assert(aiqa_pairing_lifecycle_client_finished_verified(lifecycle, 32, 2) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  assert(aiqa_pairing_lifecycle_device_finished_sent(lifecycle, 32, 2) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  for (fake.now_ms = AIQA_PAIRING_SESSION_IDLE_MS - 1U;
       fake.now_ms < AIQA_PAIRING_SESSION_ABSOLUTE_MS;
       fake.now_ms += AIQA_PAIRING_SESSION_IDLE_MS - 1U) {
    assert(aiqa_pairing_lifecycle_authenticated_activity(lifecycle, 32) ==
           AIQA_PAIRING_LIFECYCLE_OK);
  }
  fake.now_ms = AIQA_PAIRING_SESSION_ABSOLUTE_MS - 1U;
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_ACTIVE);
  fake.now_ms = AIQA_PAIRING_SESSION_ABSOLUTE_MS;
  assert(aiqa_pairing_lifecycle_tick(lifecycle) == AIQA_PAIRING_LIFECYCLE_OK);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_INACTIVE);
  aiqa_pairing_lifecycle_destroy(&lifecycle);

  fake = default_fake();
  fake.now_ms = UINT64_MAX - 1000U;
  lifecycle = create_lifecycle(&fake);
  open_window(lifecycle, &fake, 33);
  assert(status_of(lifecycle).remaining_ms == 1000U);
  fake.now_ms = UINT64_MAX;
  assert(aiqa_pairing_lifecycle_tick(lifecycle) == AIQA_PAIRING_LIFECYCLE_OK);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_INACTIVE);
  aiqa_pairing_lifecycle_destroy(&lifecycle);
}

static void run_clear_failure(void) {
  fake_ports_t fake = default_fake();
  aiqa_pairing_lifecycle_t *lifecycle = create_lifecycle(&fake);

  open_window(lifecycle, &fake, 51);
  fake.clear_ok = false;
  fake.now_ms = AIQA_PAIRING_WINDOW_MS;
  assert(aiqa_pairing_lifecycle_tick(lifecycle) ==
         AIQA_PAIRING_LIFECYCLE_DISPLAY_FAILED);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_FAULT);
  assert(fake.revoke_calls == 1 && fake.revoked_connection == 51);
  assert(fake.revoke_sequence < fake.clear_sequence);
  assert(memcmp(fake.shown_code, "01234567", AIQA_PAIRING_CODE_SIZE) == 0);

  fake.reset_presence = 1;
  assert(aiqa_pairing_lifecycle_local_reset(lifecycle) ==
         AIQA_PAIRING_LIFECYCLE_DISPLAY_FAILED);
  assert(fake.store_calls == 0);
  fake.clear_ok = true;
  fake.reset_presence = 1;
  assert(aiqa_pairing_lifecycle_local_reset(lifecycle) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_INACTIVE);
  aiqa_pairing_lifecycle_destroy(&lifecycle);

  fake = default_fake();
  lifecycle = create_lifecycle(&fake);
  open_window(lifecycle, &fake, 52);
  fake.clear_ok = false;
  assert(aiqa_pairing_lifecycle_begin(lifecycle, 52, 1, fake_consume_code,
                                      &fake) ==
         AIQA_PAIRING_LIFECYCLE_DISPLAY_FAILED);
  assert(fake.record.attempts_used == 1 && fake.consume_calls == 1);
  assert(fake.revoke_calls == 1 && fake.revoked_connection == 52);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_FAULT);
  fake.clear_ok = true;
  aiqa_pairing_lifecycle_destroy(&lifecycle);
}

static void run_connection_binding(void) {
  fake_ports_t fake = default_fake();
  aiqa_pairing_lifecycle_t *lifecycle = create_lifecycle(&fake);

  fake.start_presence = 1;
  assert(aiqa_pairing_lifecycle_local_open(lifecycle, 0) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_ARGUMENT);
  assert(fake.start_presence == 1 && fake.show_calls == 0);

  open_window(lifecycle, &fake, 21);
  assert(aiqa_pairing_lifecycle_begin(lifecycle, 22, 1, fake_consume_code,
                                      &fake) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_SESSION);
  assert(fake.store_calls == 0 && fake.consume_calls == 0);
  assert(aiqa_pairing_lifecycle_disconnect(lifecycle, 22) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_SESSION);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_AVAILABLE);
  assert(aiqa_pairing_lifecycle_disconnect(lifecycle, 21) ==
         AIQA_PAIRING_LIFECYCLE_OK);
  assert(status_of(lifecycle).state == AIQA_PAIRING_LIFECYCLE_INACTIVE);
  assert(fake.clear_calls == 1 && fake.revoked_connection == 21);

  assert(aiqa_pairing_lifecycle_begin(lifecycle, 22, 1, fake_consume_code,
                                      &fake) ==
         AIQA_PAIRING_LIFECYCLE_INVALID_STATE);
  assert(fake.store_calls == 0 && fake.consume_calls == 0);
  aiqa_pairing_lifecycle_destroy(&lifecycle);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  if (strcmp(argv[1], "open-expiry") == 0)
    run_open_expiry();
  else if (strcmp(argv[1], "attempt-lock") == 0)
    run_attempt_lock();
  else if (strcmp(argv[1], "success-session") == 0)
    run_success_session();
  else if (strcmp(argv[1], "faults") == 0)
    run_faults();
  else if (strcmp(argv[1], "single-handshake") == 0)
    run_single_handshake();
  else if (strcmp(argv[1], "boundaries") == 0)
    run_boundaries();
  else if (strcmp(argv[1], "connection-binding") == 0)
    run_connection_binding();
  else if (strcmp(argv[1], "temporal-ceilings") == 0)
    run_temporal_ceilings();
  else if (strcmp(argv[1], "clear-failure") == 0)
    run_clear_failure();
  else
    return 2;
  return 0;
}
