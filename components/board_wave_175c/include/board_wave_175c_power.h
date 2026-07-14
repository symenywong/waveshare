#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_WAVE_175C_CHARGING_UNKNOWN = 0,
    BOARD_WAVE_175C_CHARGING_DISCHARGING,
    BOARD_WAVE_175C_CHARGING_ACTIVE,
    BOARD_WAVE_175C_CHARGING_DONE,
} board_wave_175c_charging_state_t;

typedef struct {
    bool battery_present;
    bool vbus_good;
    uint8_t percent;
    board_wave_175c_charging_state_t charging_state;
} board_wave_175c_power_status_t;

bool board_wave_175c_power_decode_axp2101_status(
    uint8_t status_reg0,
    uint8_t charge_reg1,
    uint8_t soc_reg,
    board_wave_175c_power_status_t *out_status);

const char *board_wave_175c_charging_state_name(board_wave_175c_charging_state_t state);

#ifdef __cplusplus
}
#endif
