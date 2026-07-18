#include "fake_nvs.h"
#include "nvs.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define FAKE_NVS_MAX_NAMESPACES 8
#define FAKE_NVS_MAX_ENTRIES 32
#define FAKE_NVS_MAX_HANDLES 8
#define FAKE_NVS_MAX_NAME 16
#define FAKE_NVS_MAX_STRING 200

typedef enum {
    ENTRY_NONE = 0,
    ENTRY_STRING,
    ENTRY_U8,
    ENTRY_U32,
    ENTRY_I32,
    ENTRY_U64,
} entry_type_t;

typedef struct {
    bool used;
    char key[FAKE_NVS_MAX_NAME];
    entry_type_t type;
    union {
        char string_value[FAKE_NVS_MAX_STRING];
        uint8_t u8_value;
        uint32_t u32_value;
        int32_t i32_value;
        uint64_t u64_value;
    } value;
} fake_entry_t;

typedef struct {
    bool used;
    char name[FAKE_NVS_MAX_NAME];
    fake_entry_t entries[FAKE_NVS_MAX_ENTRIES];
} fake_namespace_t;

typedef struct {
    bool used;
    nvs_open_mode_t mode;
    int durable_index;
    fake_namespace_t working;
} fake_handle_t;

static fake_namespace_t s_durable[FAKE_NVS_MAX_NAMESPACES];
static fake_handle_t s_handles[FAKE_NVS_MAX_HANDLES];
static fake_nvs_commit_mode_t s_next_commit_mode;
static fake_nvs_commit_mode_t s_scheduled_commit_mode;
static int s_commit_mode_countdown = -1;
static bool s_fail_next_get_u64;
static int s_set_failure_countdown = -1;

static int find_namespace(const char *name)
{
    for (int i = 0; i < FAKE_NVS_MAX_NAMESPACES; ++i) {
        if (s_durable[i].used && strcmp(s_durable[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int allocate_namespace(const char *name)
{
    for (int i = 0; i < FAKE_NVS_MAX_NAMESPACES; ++i) {
        if (!s_durable[i].used) {
            s_durable[i].used = true;
            (void)strncpy(s_durable[i].name, name, sizeof(s_durable[i].name) - 1);
            return i;
        }
    }
    return -1;
}

static fake_handle_t *lookup_handle(nvs_handle_t handle)
{
    if (handle == 0 || handle > FAKE_NVS_MAX_HANDLES || !s_handles[handle - 1].used) {
        return NULL;
    }
    return &s_handles[handle - 1];
}

static fake_namespace_t *handle_namespace(fake_handle_t *handle)
{
    if (handle->mode == NVS_READWRITE) {
        return &handle->working;
    }
    if (handle->durable_index < 0) {
        return NULL;
    }
    return &s_durable[handle->durable_index];
}

static fake_entry_t *find_entry(fake_namespace_t *space, const char *key)
{
    if (space == NULL) {
        return NULL;
    }
    for (int i = 0; i < FAKE_NVS_MAX_ENTRIES; ++i) {
        if (space->entries[i].used && strcmp(space->entries[i].key, key) == 0) {
            return &space->entries[i];
        }
    }
    return NULL;
}

static fake_entry_t *upsert_entry(fake_handle_t *handle, const char *key, entry_type_t type)
{
    if (handle == NULL || handle->mode != NVS_READWRITE || key == NULL) {
        return NULL;
    }
    if (s_set_failure_countdown == 0) {
        s_set_failure_countdown = -1;
        return NULL;
    }
    if (s_set_failure_countdown > 0) {
        --s_set_failure_countdown;
    }
    fake_entry_t *entry = find_entry(&handle->working, key);
    if (entry == NULL) {
        for (int i = 0; i < FAKE_NVS_MAX_ENTRIES; ++i) {
            if (!handle->working.entries[i].used) {
                entry = &handle->working.entries[i];
                entry->used = true;
                (void)strncpy(entry->key, key, sizeof(entry->key) - 1);
                break;
            }
        }
    }
    if (entry != NULL) {
        entry->type = type;
    }
    return entry;
}

void fake_nvs_reset(void)
{
    (void)memset(s_durable, 0, sizeof(s_durable));
    (void)memset(s_handles, 0, sizeof(s_handles));
    s_next_commit_mode = FAKE_NVS_COMMIT_OK;
    s_scheduled_commit_mode = FAKE_NVS_COMMIT_OK;
    s_commit_mode_countdown = -1;
    s_fail_next_get_u64 = false;
    s_set_failure_countdown = -1;
}

void fake_nvs_power_cut(void)
{
    (void)memset(s_handles, 0, sizeof(s_handles));
}

void fake_nvs_set_next_commit_mode(fake_nvs_commit_mode_t mode)
{
    s_next_commit_mode = mode;
}

void fake_nvs_set_commit_mode_after(int successful_commits, fake_nvs_commit_mode_t mode)
{
    s_commit_mode_countdown = successful_commits;
    s_scheduled_commit_mode = mode;
}

void fake_nvs_fail_set_after(int successful_calls)
{
    s_set_failure_countdown = successful_calls;
}

bool fake_nvs_remove_durable_key(const char *namespace_name, const char *key)
{
    int index = find_namespace(namespace_name);
    fake_entry_t *entry = index >= 0 ? find_entry(&s_durable[index], key) : NULL;
    if (entry == NULL) {
        return false;
    }
    (void)memset(entry, 0, sizeof(*entry));
    return true;
}

bool fake_nvs_namespace_has_key(const char *namespace_name, const char *key)
{
    int index = find_namespace(namespace_name);
    return index >= 0 && find_entry(&s_durable[index], key) != NULL;
}

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{
    if (namespace_name == NULL || out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    int durable_index = find_namespace(namespace_name);
    if (open_mode == NVS_READONLY && durable_index < 0) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    for (int i = 0; i < FAKE_NVS_MAX_HANDLES; ++i) {
        if (!s_handles[i].used) {
            fake_handle_t *handle = &s_handles[i];
            (void)memset(handle, 0, sizeof(*handle));
            handle->used = true;
            handle->mode = open_mode;
            handle->durable_index = durable_index;
            if (durable_index >= 0) {
                handle->working = s_durable[durable_index];
            } else {
                handle->working.used = true;
                (void)strncpy(handle->working.name, namespace_name,
                              sizeof(handle->working.name) - 1);
            }
            *out_handle = (nvs_handle_t)(i + 1);
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

void nvs_close(nvs_handle_t handle)
{
    fake_handle_t *found = lookup_handle(handle);
    if (found != NULL) {
        (void)memset(found, 0, sizeof(*found));
    }
}

esp_err_t nvs_commit(nvs_handle_t handle_value)
{
    fake_handle_t *handle = lookup_handle(handle_value);
    if (handle == NULL) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    if (handle->mode != NVS_READWRITE) {
        return ESP_ERR_NVS_READ_ONLY;
    }
    fake_nvs_commit_mode_t mode = s_next_commit_mode;
    s_next_commit_mode = FAKE_NVS_COMMIT_OK;
    if (s_commit_mode_countdown == 0) {
        mode = s_scheduled_commit_mode;
        s_scheduled_commit_mode = FAKE_NVS_COMMIT_OK;
        s_commit_mode_countdown = -1;
    } else if (s_commit_mode_countdown > 0) {
        --s_commit_mode_countdown;
    }
    if (mode == FAKE_NVS_COMMIT_NOT_APPLIED_ERROR) {
        return ESP_FAIL;
    }
    int index = handle->durable_index;
    if (index < 0) {
        index = allocate_namespace(handle->working.name);
        if (index < 0) {
            return ESP_ERR_NO_MEM;
        }
        handle->durable_index = index;
    }
    s_durable[index] = handle->working;
    if (mode == FAKE_NVS_COMMIT_APPLIED_ERROR_READBACK_FAIL) {
        s_fail_next_get_u64 = true;
        return ESP_FAIL;
    }
    return mode == FAKE_NVS_COMMIT_APPLIED_ERROR ? ESP_FAIL : ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t handle_value)
{
    fake_handle_t *handle = lookup_handle(handle_value);
    if (handle == NULL) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    if (handle->mode != NVS_READWRITE) {
        return ESP_ERR_NVS_READ_ONLY;
    }
    (void)memset(handle->working.entries, 0, sizeof(handle->working.entries));
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle_value, const char *key)
{
    fake_handle_t *handle = lookup_handle(handle_value);
    if (handle == NULL) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->mode != NVS_READWRITE) {
        return ESP_ERR_NVS_READ_ONLY;
    }
    fake_entry_t *entry = find_entry(&handle->working, key);
    if (entry == NULL) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    (void)memset(entry, 0, sizeof(*entry));
    return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t handle_value, const char *key, const char *value)
{
    fake_handle_t *handle = lookup_handle(handle_value);
    if (value == NULL || strlen(value) >= FAKE_NVS_MAX_STRING) {
        return ESP_ERR_INVALID_ARG;
    }
    fake_entry_t *entry = upsert_entry(handle, key, ENTRY_STRING);
    if (entry == NULL) {
        return handle == NULL ? ESP_ERR_NVS_INVALID_HANDLE : ESP_ERR_NVS_READ_ONLY;
    }
    (void)strcpy(entry->value.string_value, value);
    return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t handle_value, const char *key, char *out_value, size_t *length)
{
    fake_handle_t *handle = lookup_handle(handle_value);
    if (handle == NULL || length == NULL) {
        return handle == NULL ? ESP_ERR_NVS_INVALID_HANDLE : ESP_ERR_INVALID_ARG;
    }
    fake_entry_t *entry = find_entry(handle_namespace(handle), key);
    if (entry == NULL) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (entry->type != ENTRY_STRING) {
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }
    const size_t required = strlen(entry->value.string_value) + 1;
    if (out_value == NULL) {
        *length = required;
        return ESP_OK;
    }
    if (*length < required) {
        *length = required;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    (void)memcpy(out_value, entry->value.string_value, required);
    *length = required;
    return ESP_OK;
}

#define DEFINE_INT_ACCESSORS(suffix, c_type, entry_kind, member)                        \
    esp_err_t nvs_set_##suffix(nvs_handle_t handle_value, const char *key, c_type value) \
    {                                                                                    \
        fake_handle_t *handle = lookup_handle(handle_value);                             \
        fake_entry_t *entry = upsert_entry(handle, key, entry_kind);                     \
        if (entry == NULL) {                                                             \
            return handle == NULL ? ESP_ERR_NVS_INVALID_HANDLE : ESP_ERR_NVS_READ_ONLY;  \
        }                                                                                \
        entry->value.member = value;                                                     \
        return ESP_OK;                                                                   \
    }                                                                                    \
    esp_err_t nvs_get_##suffix(nvs_handle_t handle_value, const char *key, c_type *out)  \
    {                                                                                    \
        fake_handle_t *handle = lookup_handle(handle_value);                             \
        if (handle == NULL || out == NULL) {                                             \
            return handle == NULL ? ESP_ERR_NVS_INVALID_HANDLE : ESP_ERR_INVALID_ARG;    \
        }                                                                                \
        fake_entry_t *entry = find_entry(handle_namespace(handle), key);                 \
        if (entry == NULL) {                                                             \
            return ESP_ERR_NVS_NOT_FOUND;                                                \
        }                                                                                \
        if (entry->type != entry_kind) {                                                 \
            return ESP_ERR_NVS_TYPE_MISMATCH;                                            \
        }                                                                                \
        *out = entry->value.member;                                                      \
        return ESP_OK;                                                                   \
    }

DEFINE_INT_ACCESSORS(u8, uint8_t, ENTRY_U8, u8_value)
DEFINE_INT_ACCESSORS(u32, uint32_t, ENTRY_U32, u32_value)
DEFINE_INT_ACCESSORS(i32, int32_t, ENTRY_I32, i32_value)

#undef DEFINE_INT_ACCESSORS

esp_err_t nvs_set_u64(nvs_handle_t handle_value, const char *key, uint64_t value)
{
    fake_handle_t *handle = lookup_handle(handle_value);
    fake_entry_t *entry = upsert_entry(handle, key, ENTRY_U64);
    if (entry == NULL) {
        return handle == NULL ? ESP_ERR_NVS_INVALID_HANDLE : ESP_ERR_NVS_READ_ONLY;
    }
    entry->value.u64_value = value;
    return ESP_OK;
}

esp_err_t nvs_get_u64(nvs_handle_t handle_value, const char *key, uint64_t *out_value)
{
    if (s_fail_next_get_u64) {
        s_fail_next_get_u64 = false;
        return ESP_FAIL;
    }
    fake_handle_t *handle = lookup_handle(handle_value);
    if (handle == NULL || out_value == NULL) {
        return handle == NULL ? ESP_ERR_NVS_INVALID_HANDLE : ESP_ERR_INVALID_ARG;
    }
    fake_entry_t *entry = find_entry(handle_namespace(handle), key);
    if (entry == NULL) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (entry->type != ENTRY_U64) {
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }
    *out_value = entry->value.u64_value;
    return ESP_OK;
}
