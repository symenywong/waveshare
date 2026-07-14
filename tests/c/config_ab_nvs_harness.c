#include "aiqa_config_nvs.h"
#include "aiqa_config_transaction.h"
#include "fake_nvs.h"
#include "nvs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static aiqa_config_record_t make_record(uint32_t revision, aiqa_config_slot_t slot, const char *ssid)
{
    aiqa_config_record_t record = {
        .config = aiqa_config_default(),
        .revision = revision,
        .active_slot = slot,
    };
    (void)snprintf(record.secrets.wifi_ssid, sizeof(record.secrets.wifi_ssid), "%s", ssid);
    (void)snprintf(record.secrets.wifi_password, sizeof(record.secrets.wifi_password), "%s", "test-password");
    (void)snprintf(record.secrets.chat_api_key, sizeof(record.secrets.chat_api_key), "%s", "test-chat-key");
    (void)snprintf(record.secrets.asr_api_key, sizeof(record.secrets.asr_api_key), "%s", "test-asr-key");
    return record;
}

static void set_string(nvs_handle_t handle, const char *key, const char *value)
{
    assert(nvs_set_str(handle, key, value) == ESP_OK);
}

static void provision_legacy(const aiqa_config_record_t *record)
{
    nvs_handle_t handle = 0;
    assert(nvs_open("aiqa", NVS_READWRITE, &handle) == ESP_OK);
    assert(nvs_set_u32(handle, "version", (uint32_t)record->config.config_version) == ESP_OK);
    set_string(handle, "provider", record->config.active_provider);
    set_string(handle, "model", record->config.model);
    set_string(handle, "base_url", record->config.base_url);
    set_string(handle, "asr_provider", record->config.asr_provider);
    set_string(handle, "asr_model", record->config.asr_model);
    set_string(handle, "asr_base_url", record->config.asr_base_url);
    set_string(handle, "tts_provider", record->config.tts_provider);
    set_string(handle, "tts_model", record->config.tts_model);
    set_string(handle, "tts_base_url", record->config.tts_base_url);
    set_string(handle, "tts_voice", record->config.tts_voice);
    assert(nvs_set_u8(handle, "stream", record->config.stream ? 1 : 0) == ESP_OK);
    assert(nvs_set_u8(handle, "hide_reason", record->config.hide_reasoning ? 1 : 0) == ESP_OK);
    assert(nvs_set_i32(handle, "max_tokens", record->config.max_completion_tokens) == ESP_OK);
    set_string(handle, "wifi_ssid", record->secrets.wifi_ssid);
    set_string(handle, "wifi_pass", record->secrets.wifi_password);
    set_string(handle, "chat_key", record->secrets.chat_api_key);
    set_string(handle, "asr_key", record->secrets.asr_api_key);
    assert(nvs_set_u8(handle, "volume", 37) == ESP_OK);
    set_string(handle, "assistant_name", "Mochi");
    set_string(handle, "assistant_gender", "female");
    assert(nvs_commit(handle) == ESP_OK);
    nvs_close(handle);
}

static void assert_same_record(const aiqa_config_record_t *actual, const aiqa_config_record_t *expected)
{
    assert(actual->revision == expected->revision);
    assert(actual->active_slot == expected->active_slot);
    assert(memcmp(&actual->config, &expected->config, sizeof(actual->config)) == 0);
    assert(memcmp(&actual->secrets, &expected->secrets, sizeof(actual->secrets)) == 0);
}

static aiqa_config_record_t load_active(void)
{
    aiqa_config_record_t loaded = {0};
    bool found = false;
    assert(aiqa_config_nvs_load_active_record(&loaded, &found) == ESP_OK);
    assert(found);
    return loaded;
}

static void stage_and_activate(
    const aiqa_config_record_t *record,
    aiqa_config_slot_t expected_slot,
    uint32_t expected_revision)
{
    assert(aiqa_config_nvs_stage_record(record->active_slot, record) == ESP_OK);
    assert(aiqa_config_nvs_verify_record(record->active_slot, record) == ESP_OK);
    assert(aiqa_config_nvs_activate_record(record, expected_slot, expected_revision) ==
           AIQA_CONFIG_ACTIVATION_COMMITTED);
}

static void run_legacy(void)
{
    fake_nvs_reset();
    aiqa_config_record_t legacy = make_record(1, AIQA_CONFIG_SLOT_LEGACY, "legacy-wifi");
    provision_legacy(&legacy);
    aiqa_config_record_t loaded = load_active();
    aiqa_config_record_t migrated = legacy;
    migrated.active_slot = AIQA_CONFIG_SLOT_A;
    assert_same_record(&loaded, &migrated);
    assert(fake_nvs_namespace_has_key("aiqa_cfg_a", "layout"));
    assert(fake_nvs_namespace_has_key("aiqa_meta", "head"));

    aiqa_config_snapshot_t snapshot;
    assert(aiqa_config_load_from_nvs(&snapshot) == ESP_OK);
    assert(snapshot.namespace_found);
    assert(snapshot.revision == 1);
    assert(snapshot.active_slot == AIQA_CONFIG_SLOT_A);
    assert(snapshot.user_prefs.volume_percent == 37);
    assert(strcmp(snapshot.user_prefs.assistant_profile.name, "Mochi") == 0);
}

static void run_ab_cycle(void)
{
    fake_nvs_reset();
    aiqa_config_record_t a = make_record(1, AIQA_CONFIG_SLOT_A, "wifi-a");
    stage_and_activate(&a, AIQA_CONFIG_SLOT_LEGACY, 1);
    fake_nvs_power_cut();
    aiqa_config_record_t loaded = load_active();
    assert_same_record(&loaded, &a);

    aiqa_config_record_t b = make_record(2, AIQA_CONFIG_SLOT_B, "wifi-b");
    stage_and_activate(&b, AIQA_CONFIG_SLOT_A, 1);
    fake_nvs_power_cut();
    loaded = load_active();
    assert_same_record(&loaded, &b);
}

static void run_orphan(void)
{
    fake_nvs_reset();
    aiqa_config_record_t a = make_record(1, AIQA_CONFIG_SLOT_A, "active-a");
    stage_and_activate(&a, AIQA_CONFIG_SLOT_LEGACY, 1);
    aiqa_config_record_t orphan = make_record(99, AIQA_CONFIG_SLOT_B, "orphan-b");
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_B, &orphan) == ESP_OK);
    fake_nvs_power_cut();
    aiqa_config_record_t loaded = load_active();
    assert_same_record(&loaded, &a);
}

static void run_stage_commit(void)
{
    fake_nvs_reset();
    aiqa_config_record_t a = make_record(1, AIQA_CONFIG_SLOT_A, "active-a");
    stage_and_activate(&a, AIQA_CONFIG_SLOT_LEGACY, 1);
    aiqa_config_record_t b = make_record(2, AIQA_CONFIG_SLOT_B, "candidate-b");

    fake_nvs_set_next_commit_mode(FAKE_NVS_COMMIT_NOT_APPLIED_ERROR);
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_B, &b) != ESP_OK);
    fake_nvs_power_cut();
    aiqa_config_record_t loaded = load_active();
    assert_same_record(&loaded, &a);

    fake_nvs_set_next_commit_mode(FAKE_NVS_COMMIT_APPLIED_ERROR);
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_B, &b) == ESP_OK);
    assert(aiqa_config_nvs_verify_record(AIQA_CONFIG_SLOT_B, &b) == ESP_OK);
}

static void run_head_commit(void)
{
    fake_nvs_reset();
    aiqa_config_record_t a = make_record(1, AIQA_CONFIG_SLOT_A, "active-a");
    stage_and_activate(&a, AIQA_CONFIG_SLOT_LEGACY, 1);
    aiqa_config_record_t b = make_record(2, AIQA_CONFIG_SLOT_B, "candidate-b");
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_B, &b) == ESP_OK);

    fake_nvs_set_next_commit_mode(FAKE_NVS_COMMIT_NOT_APPLIED_ERROR);
    assert(aiqa_config_nvs_activate_record(&b, AIQA_CONFIG_SLOT_A, 1) ==
           AIQA_CONFIG_ACTIVATION_NOT_COMMITTED);
    aiqa_config_record_t loaded = load_active();
    assert_same_record(&loaded, &a);

    fake_nvs_set_next_commit_mode(FAKE_NVS_COMMIT_APPLIED_ERROR);
    assert(aiqa_config_nvs_activate_record(&b, AIQA_CONFIG_SLOT_A, 1) ==
           AIQA_CONFIG_ACTIVATION_COMMITTED);
    loaded = load_active();
    assert_same_record(&loaded, &b);
}

static void run_head_indeterminate(void)
{
    fake_nvs_reset();
    aiqa_config_record_t a = make_record(1, AIQA_CONFIG_SLOT_A, "active-a");
    stage_and_activate(&a, AIQA_CONFIG_SLOT_LEGACY, 1);
    aiqa_config_record_t b = make_record(2, AIQA_CONFIG_SLOT_B, "candidate-b");
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_B, &b) == ESP_OK);
    fake_nvs_set_next_commit_mode(FAKE_NVS_COMMIT_APPLIED_ERROR_READBACK_FAIL);
    assert(aiqa_config_nvs_activate_record(&b, AIQA_CONFIG_SLOT_A, 1) ==
           AIQA_CONFIG_ACTIVATION_INDETERMINATE);
}

static void run_reset(void)
{
    fake_nvs_reset();
    aiqa_config_record_t legacy = make_record(1, AIQA_CONFIG_SLOT_LEGACY, "legacy");
    provision_legacy(&legacy);
    aiqa_config_record_t migrated = load_active();
    assert(migrated.active_slot == AIQA_CONFIG_SLOT_A);
    assert(migrated.revision == 1);
    aiqa_config_record_t b = make_record(2, AIQA_CONFIG_SLOT_B, "b");
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_B, &b) == ESP_OK);
    assert(aiqa_config_nvs_discard_record(AIQA_CONFIG_SLOT_B));
    assert(!fake_nvs_namespace_has_key("aiqa_cfg_b", "layout"));

    assert(aiqa_config_erase_nvs_namespace() == ESP_OK);
    assert(!fake_nvs_namespace_has_key("aiqa", "wifi_ssid"));
    assert(!fake_nvs_namespace_has_key("aiqa_cfg_a", "layout"));
    assert(!fake_nvs_namespace_has_key("aiqa_cfg_b", "layout"));
    assert(!fake_nvs_namespace_has_key("aiqa_meta", "head"));
    bool found = true;
    aiqa_config_record_t loaded = {0};
    assert(aiqa_config_nvs_load_active_record(&loaded, &found) == ESP_OK);
    assert(!found);
}

static void run_field_failures(void)
{
    static const char *keys[] = {
        "layout", "revision", "version", "provider", "model", "base_url",
        "asr_provider", "asr_model", "asr_base_url", "tts_provider", "tts_model",
        "tts_base_url", "tts_voice", "stream", "hide_reason", "max_tokens",
        "wifi_ssid", "wifi_pass", "chat_key", "asr_key",
    };
    aiqa_config_record_t record = make_record(1, AIQA_CONFIG_SLOT_A, "wifi-a");
    for (int failure_index = 0; failure_index < 20; ++failure_index) {
        fake_nvs_reset();
        fake_nvs_fail_set_after(failure_index);
        assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_A, &record) != ESP_OK);
    }
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        fake_nvs_reset();
        assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_A, &record) == ESP_OK);
        assert(fake_nvs_remove_durable_key("aiqa_cfg_a", keys[i]));
        assert(aiqa_config_nvs_verify_record(AIQA_CONFIG_SLOT_A, &record) != ESP_OK);
    }
}

static void set_raw_head(uint64_t head)
{
    nvs_handle_t handle = 0;
    assert(nvs_open("aiqa_meta", NVS_READWRITE, &handle) == ESP_OK);
    assert(nvs_set_u64(handle, "head", head) == ESP_OK);
    assert(nvs_commit(handle) == ESP_OK);
    nvs_close(handle);
}

static void expect_verify_mismatch(
    const aiqa_config_record_t *stored,
    const aiqa_config_record_t *expected)
{
    assert(aiqa_config_nvs_verify_record(stored->active_slot, expected) != ESP_OK);
}

static void run_api_edges(void)
{
    fake_nvs_reset();
    bool found = true;
    aiqa_config_record_t loaded = {0};
    assert(aiqa_config_nvs_load_active_record(&loaded, &found) == ESP_OK);
    assert(!found);
    assert(aiqa_config_nvs_load_active_record(NULL, &found) == ESP_ERR_INVALID_ARG);
    assert(aiqa_config_nvs_load_active_record(&loaded, NULL) == ESP_ERR_INVALID_ARG);

    aiqa_config_record_t record = make_record(1, AIQA_CONFIG_SLOT_A, "wifi-a");
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_LEGACY, &record) == ESP_ERR_INVALID_ARG);
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_A, NULL) == ESP_ERR_INVALID_ARG);
    aiqa_config_record_t invalid = record;
    invalid.active_slot = AIQA_CONFIG_SLOT_B;
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_A, &invalid) == ESP_ERR_INVALID_ARG);
    invalid = record;
    invalid.revision = 0;
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_A, &invalid) == ESP_ERR_INVALID_ARG);
    invalid = record;
    invalid.config.config_version = 99;
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_A, &invalid) == ESP_ERR_INVALID_ARG);
    invalid = record;
    invalid.secrets.wifi_ssid[0] = '\0';
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_A, &invalid) == ESP_ERR_INVALID_ARG);
    assert(aiqa_config_nvs_verify_record(AIQA_CONFIG_SLOT_LEGACY, &record) == ESP_ERR_INVALID_ARG);
    assert(aiqa_config_nvs_verify_record(AIQA_CONFIG_SLOT_A, NULL) == ESP_ERR_INVALID_ARG);
    assert(aiqa_config_nvs_activate_record(NULL, AIQA_CONFIG_SLOT_LEGACY, 1) ==
           AIQA_CONFIG_ACTIVATION_INDETERMINATE);
    invalid = record;
    invalid.revision = 0;
    assert(aiqa_config_nvs_activate_record(&invalid, AIQA_CONFIG_SLOT_LEGACY, 1) ==
           AIQA_CONFIG_ACTIVATION_INDETERMINATE);
    assert(!aiqa_config_nvs_discard_record(AIQA_CONFIG_SLOT_LEGACY));

    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_A, &record) == ESP_OK);
    assert(aiqa_config_nvs_activate_record(&record, AIQA_CONFIG_SLOT_LEGACY, 1) ==
           AIQA_CONFIG_ACTIVATION_COMMITTED);
    assert(!aiqa_config_nvs_discard_record(AIQA_CONFIG_SLOT_A));
    aiqa_config_record_t rollback_b = make_record(1, AIQA_CONFIG_SLOT_B, "rollback-b");
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_B, &rollback_b) == ESP_OK);
    assert(aiqa_config_nvs_activate_record(&rollback_b, AIQA_CONFIG_SLOT_A, 1) ==
           AIQA_CONFIG_ACTIVATION_NOT_COMMITTED);
    assert(aiqa_config_nvs_discard_record(AIQA_CONFIG_SLOT_B));
    aiqa_config_record_t skipped_b = make_record(3, AIQA_CONFIG_SLOT_B, "skipped-b");
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_B, &skipped_b) == ESP_OK);
    assert(aiqa_config_nvs_activate_record(&skipped_b, AIQA_CONFIG_SLOT_A, 1) ==
           AIQA_CONFIG_ACTIVATION_NOT_COMMITTED);
    assert(aiqa_config_nvs_discard_record(AIQA_CONFIG_SLOT_B));
    aiqa_config_record_t candidate_b = make_record(2, AIQA_CONFIG_SLOT_B, "wifi-b");
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_B, &candidate_b) == ESP_OK);
    assert(aiqa_config_nvs_activate_record(&candidate_b, AIQA_CONFIG_SLOT_B, 1) ==
           AIQA_CONFIG_ACTIVATION_NOT_COMMITTED);
    assert(fake_nvs_remove_durable_key("aiqa_cfg_b", "wifi_pass"));
    assert(aiqa_config_nvs_activate_record(&candidate_b, AIQA_CONFIG_SLOT_A, 1) ==
           AIQA_CONFIG_ACTIVATION_NOT_COMMITTED);
#define EXPECT_FIELD_MISMATCH(statement) \
    do {                                  \
        invalid = record;                 \
        statement;                        \
        expect_verify_mismatch(&record, &invalid); \
    } while (0)
    EXPECT_FIELD_MISMATCH(invalid.revision += 1);
    EXPECT_FIELD_MISMATCH(invalid.config.config_version += 1);
    EXPECT_FIELD_MISMATCH(invalid.config.stream = !invalid.config.stream);
    EXPECT_FIELD_MISMATCH(invalid.config.hide_reasoning = !invalid.config.hide_reasoning);
    EXPECT_FIELD_MISMATCH(invalid.config.max_completion_tokens += 1);
    EXPECT_FIELD_MISMATCH(invalid.config.active_provider[0] = 'x');
    EXPECT_FIELD_MISMATCH(invalid.config.model[0] = 'x');
    EXPECT_FIELD_MISMATCH(invalid.config.base_url[8] = 'x');
    EXPECT_FIELD_MISMATCH(invalid.config.asr_provider[0] = 'x');
    EXPECT_FIELD_MISMATCH(invalid.config.asr_model[0] = 'x');
    EXPECT_FIELD_MISMATCH(invalid.config.asr_base_url[8] = 'x');
    EXPECT_FIELD_MISMATCH(invalid.config.tts_provider[0] = 'x');
    EXPECT_FIELD_MISMATCH(invalid.config.tts_model[0] = 'x');
    EXPECT_FIELD_MISMATCH(invalid.config.tts_base_url[8] = 'x');
    EXPECT_FIELD_MISMATCH(invalid.config.tts_voice[0] = 'x');
    EXPECT_FIELD_MISMATCH(invalid.secrets.wifi_ssid[0] = 'x');
    EXPECT_FIELD_MISMATCH(invalid.secrets.wifi_password[0] = 'x');
    EXPECT_FIELD_MISMATCH(invalid.secrets.chat_api_key[0] = 'x');
    EXPECT_FIELD_MISMATCH(invalid.secrets.asr_api_key[0] = 'x');
#undef EXPECT_FIELD_MISMATCH

    fake_nvs_reset();
    set_raw_head(0);
    assert(aiqa_config_nvs_load_active_record(&loaded, &found) == ESP_ERR_INVALID_STATE);
    fake_nvs_reset();
    set_raw_head(UINT64_MAX);
    assert(aiqa_config_nvs_load_active_record(&loaded, &found) == ESP_ERR_INVALID_STATE);

    fake_nvs_reset();
    record = make_record(5, AIQA_CONFIG_SLOT_A, "wifi-a");
    assert(aiqa_config_nvs_stage_record(AIQA_CONFIG_SLOT_A, &record) == ESP_OK);
    set_raw_head((uint64_t)6 << 1U);
    assert(aiqa_config_nvs_load_active_record(&loaded, &found) == ESP_ERR_INVALID_STATE);

    fake_nvs_reset();
    nvs_handle_t handle = 0;
    assert(nvs_open("aiqa_meta", NVS_READWRITE, &handle) == ESP_OK);
    assert(nvs_set_u8(handle, "reset", 1) == ESP_OK);
    assert(nvs_commit(handle) == ESP_OK);
    nvs_close(handle);
    found = true;
    assert(aiqa_config_nvs_load_active_record(&loaded, &found) == ESP_OK);
    assert(!found);
}

static void run_storage_ports(void)
{
    fake_nvs_reset();
    aiqa_config_record_t record = make_record(1, AIQA_CONFIG_SLOT_A, "wifi-a");
    aiqa_config_storage_ports_t ports = aiqa_config_nvs_storage_ports();
    assert(ports.context == NULL);
    assert(ports.stage(ports.context, AIQA_CONFIG_SLOT_A, &record));
    assert(ports.verify(ports.context, AIQA_CONFIG_SLOT_A, &record));
    assert(ports.activate(ports.context, &record, AIQA_CONFIG_SLOT_LEGACY, 1) ==
           AIQA_CONFIG_ACTIVATION_COMMITTED);
    assert(ports.discard(ports.context, AIQA_CONFIG_SLOT_B));
}

static void run_reset_tombstone(void)
{
    fake_nvs_reset();
    aiqa_config_record_t legacy = make_record(1, AIQA_CONFIG_SLOT_LEGACY, "must-stay-hidden");
    provision_legacy(&legacy);
    nvs_handle_t handle = 0;
    assert(nvs_open("aiqa_meta", NVS_READWRITE, &handle) == ESP_OK);
    assert(nvs_set_u8(handle, "reset", 1) == ESP_OK);
    assert(nvs_commit(handle) == ESP_OK);
    nvs_close(handle);

    aiqa_config_snapshot_t snapshot;
    assert(aiqa_config_load_from_nvs(&snapshot) == ESP_OK);
    assert(!snapshot.namespace_found);
    assert(snapshot.secrets.wifi_ssid[0] == '\0');
    assert(snapshot.secrets.wifi_password[0] == '\0');
    assert(snapshot.secret_status != AIQA_SECRET_OK);
}

static void run_missing_head_after_migration(void)
{
    fake_nvs_reset();
    aiqa_config_record_t legacy =
        make_record(1, AIQA_CONFIG_SLOT_LEGACY, "must-not-resurrect");
    provision_legacy(&legacy);

    aiqa_config_record_t migrated = load_active();
    assert(migrated.active_slot == AIQA_CONFIG_SLOT_A);
    assert(fake_nvs_remove_durable_key("aiqa_meta", "head"));
    fake_nvs_power_cut();

    aiqa_config_record_t loaded = {0};
    bool found = true;
    assert(aiqa_config_nvs_load_active_record(&loaded, &found) == ESP_ERR_INVALID_STATE);
    assert(!found);
    assert(loaded.secrets.wifi_ssid[0] == '\0');

    aiqa_config_snapshot_t snapshot = {
        .secrets = legacy.secrets,
        .namespace_found = true,
    };
    assert(aiqa_config_load_from_nvs(&snapshot) == ESP_ERR_INVALID_STATE);
    assert(!snapshot.namespace_found);
    assert(snapshot.secrets.wifi_ssid[0] == '\0');
    assert(snapshot.secrets.wifi_password[0] == '\0');
}

static void run_migration_activation_not_committed(void)
{
    fake_nvs_reset();
    aiqa_config_record_t legacy = make_record(1, AIQA_CONFIG_SLOT_LEGACY, "legacy-retry");
    provision_legacy(&legacy);
    fake_nvs_set_commit_mode_after(1, FAKE_NVS_COMMIT_NOT_APPLIED_ERROR);

    aiqa_config_record_t loaded = load_active();
    assert(loaded.active_slot == AIQA_CONFIG_SLOT_LEGACY);
    assert(!fake_nvs_namespace_has_key("aiqa_cfg_a", "layout"));
    fake_nvs_power_cut();

    loaded = load_active();
    assert(loaded.active_slot == AIQA_CONFIG_SLOT_A);
    assert(strcmp(loaded.secrets.wifi_ssid, "legacy-retry") == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "legacy") == 0) {
        run_legacy();
    } else if (strcmp(argv[1], "ab_cycle") == 0) {
        run_ab_cycle();
    } else if (strcmp(argv[1], "orphan") == 0) {
        run_orphan();
    } else if (strcmp(argv[1], "stage_commit") == 0) {
        run_stage_commit();
    } else if (strcmp(argv[1], "head_commit") == 0) {
        run_head_commit();
    } else if (strcmp(argv[1], "head_indeterminate") == 0) {
        run_head_indeterminate();
    } else if (strcmp(argv[1], "reset") == 0) {
        run_reset();
    } else if (strcmp(argv[1], "field_failures") == 0) {
        run_field_failures();
    } else if (strcmp(argv[1], "api_edges") == 0) {
        run_api_edges();
    } else if (strcmp(argv[1], "storage_ports") == 0) {
        run_storage_ports();
    } else if (strcmp(argv[1], "reset_tombstone") == 0) {
        run_reset_tombstone();
    } else if (strcmp(argv[1], "missing_head_after_migration") == 0) {
        run_missing_head_after_migration();
    } else if (strcmp(argv[1], "migration_activation_not_committed") == 0) {
        run_migration_activation_not_committed();
    } else {
        return 2;
    }
    return 0;
}
