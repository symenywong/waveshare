#include "board_wave_175c_power.h"

#define AXP2101_STATUS_VBUS_GOOD_BIT 0x08u
#define AXP2101_STATUS_BATTERY_PRESENT_BIT 0x20u
#define AXP2101_CHARGE_STATE_MASK 0x07u
#define AXP2101_SOC_MAX_PERCENT 100u

bool board_wave_175c_power_decode_axp2101_status(
    uint8_t status_reg0,
    uint8_t charge_reg1,
    uint8_t soc_reg,
    board_wave_175c_power_status_t *out_status)
{
    if (out_status == 0) {
        return false;
    }

    const bool battery_present = (status_reg0 & AXP2101_STATUS_BATTERY_PRESENT_BIT) != 0;
    const bool vbus_good = (status_reg0 & AXP2101_STATUS_VBUS_GOOD_BIT) != 0;
    const uint8_t charge_state = charge_reg1 & AXP2101_CHARGE_STATE_MASK;
    board_wave_175c_charging_state_t charging = BOARD_WAVE_175C_CHARGING_UNKNOWN;
    if (!battery_present) {
        charging = vbus_good ? BOARD_WAVE_175C_CHARGING_DONE : BOARD_WAVE_175C_CHARGING_UNKNOWN;
    } else if (!vbus_good) {
        charging = BOARD_WAVE_175C_CHARGING_DISCHARGING;
    } else if (charge_state == 0x04 || soc_reg >= AXP2101_SOC_MAX_PERCENT) {
        charging = BOARD_WAVE_175C_CHARGING_DONE;
    } else if (charge_state != 0x00) {
        charging = BOARD_WAVE_175C_CHARGING_ACTIVE;
    } else {
        charging = BOARD_WAVE_175C_CHARGING_DISCHARGING;
    }

    *out_status = (board_wave_175c_power_status_t){
        .battery_present = battery_present,
        .vbus_good = vbus_good,
        .percent = battery_present && soc_reg <= AXP2101_SOC_MAX_PERCENT ? soc_reg : 0,
        .charging_state = charging,
    };
    return true;
}

const char *board_wave_175c_charging_state_name(board_wave_175c_charging_state_t state)
{
    switch (state) {
    case BOARD_WAVE_175C_CHARGING_DISCHARGING:
        return "discharging";
    case BOARD_WAVE_175C_CHARGING_ACTIVE:
        return "charging";
    case BOARD_WAVE_175C_CHARGING_DONE:
        return "done";
    case BOARD_WAVE_175C_CHARGING_UNKNOWN:
    default:
        return "unknown";
    }
}
