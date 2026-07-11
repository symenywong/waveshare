#include "board_wave_175c_contract.h"

static const board_wave_175c_i2c_device_t EXPECTED_DEVICES[] = {
    {.name = "AXP2101", .address = 0x34, .flag = BOARD_WAVE_175C_I2C_AXP2101, .required_for_ptt = true},
    {.name = "ES7210", .address = 0x40, .flag = BOARD_WAVE_175C_I2C_ES7210, .required_for_ptt = true},
    {.name = "ES8311", .address = 0x18, .flag = BOARD_WAVE_175C_I2C_ES8311, .required_for_ptt = true},
    {.name = "CST9217", .address = 0x5A, .flag = BOARD_WAVE_175C_I2C_TOUCH, .required_for_ptt = false},
    {.name = "QMI8658", .address = 0x6B, .flag = BOARD_WAVE_175C_I2C_QMI8658, .required_for_ptt = false},
};

const board_wave_175c_i2c_device_t *board_wave_175c_expected_i2c_devices(void)
{
    return EXPECTED_DEVICES;
}

size_t board_wave_175c_expected_i2c_device_count(void)
{
    return sizeof(EXPECTED_DEVICES) / sizeof(EXPECTED_DEVICES[0]);
}

uint32_t board_wave_175c_required_i2c_mask(void)
{
    uint32_t mask = 0;
    const size_t count = board_wave_175c_expected_i2c_device_count();
    for (size_t i = 0; i < count; ++i) {
        if (EXPECTED_DEVICES[i].required_for_ptt) {
            mask |= EXPECTED_DEVICES[i].flag;
        }
    }
    return mask;
}

uint32_t board_wave_175c_detected_mask_from_addresses(const uint8_t *addresses, size_t count)
{
    if (addresses == 0) {
        return 0;
    }

    uint32_t mask = 0;
    const size_t expected_count = board_wave_175c_expected_i2c_device_count();
    for (size_t address_index = 0; address_index < count; ++address_index) {
        for (size_t expected_index = 0; expected_index < expected_count; ++expected_index) {
            if (addresses[address_index] == EXPECTED_DEVICES[expected_index].address) {
                mask |= EXPECTED_DEVICES[expected_index].flag;
            }
        }
    }
    return mask;
}

uint32_t board_wave_175c_missing_required_mask(uint32_t detected_mask)
{
    return board_wave_175c_required_i2c_mask() & ~detected_mask;
}

bool board_wave_175c_has_required_i2c_devices(uint32_t detected_mask)
{
    return board_wave_175c_missing_required_mask(detected_mask) == 0;
}

const char *board_wave_175c_i2c_flag_name(uint32_t flag)
{
    switch (flag) {
    case BOARD_WAVE_175C_I2C_AXP2101:
        return "AXP2101";
    case BOARD_WAVE_175C_I2C_ES7210:
        return "ES7210";
    case BOARD_WAVE_175C_I2C_ES8311:
        return "ES8311";
    case BOARD_WAVE_175C_I2C_TOUCH:
        return "CST9217";
    case BOARD_WAVE_175C_I2C_QMI8658:
        return "QMI8658";
    default:
        return "UNKNOWN";
    }
}
