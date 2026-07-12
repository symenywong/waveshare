#include "board_wave_175c.h"

#include "board_wave_175c_i2c_bus.h"
#include "board_wave_175c_pins.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

#include <inttypes.h>

static const char *TAG = "board_175c";

#define WAVE_175C_AXP2101_ADDR 0x34
#define WAVE_175C_I2C_SPEED_HZ 400000

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_axp2101_dev;

static esp_err_t ensure_i2c_bus(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = WAVE_175C_I2C_SDA,
        .scl_io_num = WAVE_175C_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus_config, &s_i2c_bus);
}

static esp_err_t ensure_axp2101_device(void)
{
    if (s_axp2101_dev != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ensure_i2c_bus(), TAG, "I2C bus init failed");
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = WAVE_175C_AXP2101_ADDR,
        .scl_speed_hz = WAVE_175C_I2C_SPEED_HZ,
    };
    return i2c_master_bus_add_device(s_i2c_bus, &device_config, &s_axp2101_dev);
}

static esp_err_t axp2101_write_reg(uint8_t reg, uint8_t value)
{
    ESP_RETURN_ON_ERROR(ensure_axp2101_device(), TAG, "AXP2101 device unavailable");
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(s_axp2101_dev, data, sizeof(data), 100);
}

static esp_err_t init_axp2101_power(void)
{
    ESP_RETURN_ON_ERROR(axp2101_write_reg(0x22, 0x06), TAG, "AXP2101 power key source config failed");
    ESP_RETURN_ON_ERROR(axp2101_write_reg(0x27, 0x10), TAG, "AXP2101 power-off hold config failed");
    ESP_RETURN_ON_ERROR(axp2101_write_reg(0x80, 0x01), TAG, "AXP2101 DC enable config failed");
    ESP_RETURN_ON_ERROR(axp2101_write_reg(0x90, 0x00), TAG, "AXP2101 LDO0 disable failed");
    ESP_RETURN_ON_ERROR(axp2101_write_reg(0x91, 0x00), TAG, "AXP2101 LDO1 disable failed");
    ESP_RETURN_ON_ERROR(axp2101_write_reg(0x82, (uint8_t)((3300 - 1500) / 100)),
                        TAG,
                        "AXP2101 DC1 voltage config failed");
    ESP_RETURN_ON_ERROR(axp2101_write_reg(0x92, (uint8_t)((3300 - 500) / 100)),
                        TAG,
                        "AXP2101 ALDO1 voltage config failed");
    ESP_RETURN_ON_ERROR(axp2101_write_reg(0x90, 0x01), TAG, "AXP2101 ALDO1 enable failed");
    ESP_RETURN_ON_ERROR(axp2101_write_reg(0x64, 0x02), TAG, "AXP2101 charger voltage config failed");
    ESP_RETURN_ON_ERROR(axp2101_write_reg(0x61, 0x02), TAG, "AXP2101 precharge current config failed");
    ESP_RETURN_ON_ERROR(axp2101_write_reg(0x62, 0x08), TAG, "AXP2101 charge current config failed");
    ESP_RETURN_ON_ERROR(axp2101_write_reg(0x63, 0x01), TAG, "AXP2101 termination current config failed");
    ESP_LOGI(TAG, "AXP2101 power rails initialized for 1.75C audio/display domains");
    return ESP_OK;
}

esp_err_t board_wave_175c_init_minimal(void)
{
    gpio_config_t boot_config = {
        .pin_bit_mask = 1ULL << WAVE_175C_BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&boot_config), TAG, "BOOT gpio config failed");

    gpio_config_t pa_config = {
        .pin_bit_mask = 1ULL << WAVE_175C_PA,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pa_config), TAG, "PA gpio config failed");
    ESP_RETURN_ON_ERROR(board_wave_175c_set_pa_enabled(false), TAG, "PA safe-off failed");
    ESP_RETURN_ON_ERROR(ensure_i2c_bus(), TAG, "I2C bus init failed");
    ESP_RETURN_ON_ERROR(init_axp2101_power(), TAG, "AXP2101 power init failed");

    ESP_LOGI(TAG, "Board constants loaded: LCD %dx%d, I2C SDA=%d SCL=%d",
             WAVE_175C_LCD_WIDTH,
             WAVE_175C_LCD_HEIGHT,
             WAVE_175C_I2C_SDA,
             WAVE_175C_I2C_SCL);
    ESP_LOGI(TAG, "ES7210 BCLK=%d LRCK=%d DIN=%d MCLK=%d, ES8311 DOUT=%d, PA=%d",
             WAVE_175C_ES7210_BCLK,
             WAVE_175C_ES7210_LRCK,
             WAVE_175C_ES7210_DIN,
             WAVE_175C_ES7210_MCLK,
             WAVE_175C_ES8311_DOUT,
             WAVE_175C_PA);
    ESP_LOGW(TAG, "Hardware drivers are intentionally staged; run bring-up before enabling real ASR");
    return ESP_OK;
}

esp_err_t board_wave_175c_run_bringup_checks(void)
{
    board_wave_175c_i2c_scan_result_t scan = {0};
    ESP_RETURN_ON_ERROR(board_wave_175c_i2c_scan(&scan), TAG, "I2C scan failed");

    ESP_LOGI(TAG, "I2C scan found %u addresses; detected_mask=0x%08" PRIx32 " missing_required=0x%08" PRIx32,
             (unsigned)scan.address_count,
             scan.detected_mask,
             scan.missing_required_mask);
    for (size_t i = 0; i < scan.address_count; ++i) {
        ESP_LOGI(TAG, "I2C device at 0x%02x", scan.addresses[i]);
    }
    if (scan.missing_required_mask != 0) {
        ESP_LOGE(TAG, "Required I2C devices are missing; mask=0x%08" PRIx32, scan.missing_required_mask);
        return ESP_ERR_NOT_FOUND;
    }

    bool boot_pressed = false;
    ESP_RETURN_ON_ERROR(board_wave_175c_boot_button_is_pressed(&boot_pressed), TAG, "BOOT read failed");
    ESP_LOGI(TAG, "BOOT button runtime level: %s", boot_pressed ? "pressed" : "released");
    ESP_LOGI(TAG, "Next hardware checks: PMU/PWR IRQ, AMOLED init sequence, ES7210 RMS/peak, ES8311+PA tone");
    return ESP_OK;
}

esp_err_t board_wave_175c_i2c_scan(board_wave_175c_i2c_scan_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(ensure_i2c_bus(), TAG, "I2C bus init failed");

    *result = (board_wave_175c_i2c_scan_result_t){0};
    for (uint8_t address = 0x08; address <= 0x77; ++address) {
        esp_err_t probe_result = i2c_master_probe(s_i2c_bus, address, 50);
        if (probe_result == ESP_OK &&
            result->address_count < (sizeof(result->addresses) / sizeof(result->addresses[0]))) {
            result->addresses[result->address_count++] = address;
        }
    }

    result->detected_mask = board_wave_175c_detected_mask_from_addresses(result->addresses, result->address_count);
    result->missing_required_mask = board_wave_175c_missing_required_mask(result->detected_mask);
    return ESP_OK;
}

esp_err_t board_wave_175c_i2c_bus_handle(i2c_master_bus_handle_t *out_bus)
{
    if (out_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(ensure_i2c_bus(), TAG, "I2C bus init failed");
    *out_bus = s_i2c_bus;
    return ESP_OK;
}

esp_err_t board_wave_175c_boot_button_is_pressed(bool *pressed)
{
    if (pressed == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *pressed = gpio_get_level(WAVE_175C_BOOT_BUTTON) == 0;
    return ESP_OK;
}

esp_err_t board_wave_175c_set_pa_enabled(bool enabled)
{
    return gpio_set_level(WAVE_175C_PA, enabled ? 1 : 0);
}
