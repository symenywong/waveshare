#pragma once

#include "board_wave_175c_contract.h"
#include "board_wave_175c_pet_types.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t addresses[64];
    size_t address_count;
    uint32_t detected_mask;
    uint32_t missing_required_mask;
} board_wave_175c_i2c_scan_result_t;

typedef struct {
    const char *title;
    const char *status;
    const char *detail;
    const char *hint;
    uint16_t accent_rgb565;
    bool is_error;
    board_wave_175c_pet_expression_t expression;
} board_wave_175c_display_page_t;

esp_err_t board_wave_175c_init_minimal(void);
esp_err_t board_wave_175c_run_bringup_checks(void);
esp_err_t board_wave_175c_i2c_scan(board_wave_175c_i2c_scan_result_t *result);
esp_err_t board_wave_175c_boot_button_is_pressed(bool *pressed);
esp_err_t board_wave_175c_set_pa_enabled(bool enabled);
esp_err_t board_wave_175c_display_init(void);
esp_err_t board_wave_175c_display_draw_test_pattern(void);
esp_err_t board_wave_175c_display_fill_rgb565(uint16_t color);
esp_err_t board_wave_175c_display_show_page(const board_wave_175c_display_page_t *page);
bool board_wave_175c_display_is_ready(void);

#ifdef __cplusplus
}
#endif
