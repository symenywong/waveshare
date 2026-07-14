#pragma once

#include <stdbool.h>

typedef enum {
    FAKE_NVS_COMMIT_OK = 0,
    FAKE_NVS_COMMIT_NOT_APPLIED_ERROR,
    FAKE_NVS_COMMIT_APPLIED_ERROR,
    FAKE_NVS_COMMIT_APPLIED_ERROR_READBACK_FAIL,
} fake_nvs_commit_mode_t;

void fake_nvs_reset(void);
void fake_nvs_power_cut(void);
void fake_nvs_set_next_commit_mode(fake_nvs_commit_mode_t mode);
void fake_nvs_set_commit_mode_after(int successful_commits, fake_nvs_commit_mode_t mode);
void fake_nvs_fail_set_after(int successful_calls);
bool fake_nvs_remove_durable_key(const char *namespace_name, const char *key);
bool fake_nvs_namespace_has_key(const char *namespace_name, const char *key);
