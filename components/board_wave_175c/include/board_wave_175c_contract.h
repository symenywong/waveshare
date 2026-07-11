#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_WAVE_175C_I2C_AXP2101 = 1u << 0,
    BOARD_WAVE_175C_I2C_ES7210 = 1u << 1,
    BOARD_WAVE_175C_I2C_ES8311 = 1u << 2,
    BOARD_WAVE_175C_I2C_TOUCH = 1u << 3,
    BOARD_WAVE_175C_I2C_QMI8658 = 1u << 4,
} board_wave_175c_i2c_device_flag_t;

typedef struct {
    const char *name;
    uint8_t address;
    uint32_t flag;
    bool required_for_ptt;
} board_wave_175c_i2c_device_t;

const board_wave_175c_i2c_device_t *board_wave_175c_expected_i2c_devices(void);
size_t board_wave_175c_expected_i2c_device_count(void);
uint32_t board_wave_175c_required_i2c_mask(void);
uint32_t board_wave_175c_detected_mask_from_addresses(const uint8_t *addresses, size_t count);
uint32_t board_wave_175c_missing_required_mask(uint32_t detected_mask);
bool board_wave_175c_has_required_i2c_devices(uint32_t detected_mask);
const char *board_wave_175c_i2c_flag_name(uint32_t flag);

#ifdef __cplusplus
}
#endif
