#include "board_wave_175c.h"

#include "board_wave_175c_pins.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <stdlib.h>

static const char *TAG = "board_175c";

#define WAVE_175C_LCD_HOST SPI2_HOST
#define WAVE_175C_LCD_QSPI_QUEUE_DEPTH 10
#define WAVE_175C_LCD_FLUSH_TIMEOUT_MS 2000
#define WAVE_175C_LCD_MAX_TRANSFER_BYTES \
    (WAVE_175C_LCD_WIDTH * WAVE_175C_LCD_HEIGHT * WAVE_175C_LCD_BITS_PER_PIXEL / 8)

static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_panel_handle_t s_lcd_panel;
static esp_lcd_panel_io_handle_t s_lcd_io;
static SemaphoreHandle_t s_lcd_flush_done;
static bool s_lcd_ready;

// Matches the Waveshare ESP32-S3-Touch-AMOLED-1.75 BSP CO5300 init sequence.
static const co5300_lcd_init_cmd_t LCD_INIT_CMDS[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

static bool IRAM_ATTR lcd_flush_done_cb(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *edata,
    void *user_ctx)
{
    (void)panel_io;
    (void)edata;

    SemaphoreHandle_t flush_done = (SemaphoreHandle_t)user_ctx;
    if (flush_done == NULL) {
        return false;
    }

    BaseType_t need_yield = pdFALSE;
    xSemaphoreGiveFromISR(flush_done, &need_yield);
    return need_yield == pdTRUE;
}

static uint16_t rgb565_to_panel_wire(uint16_t color)
{
    return (uint16_t)SPI_SWAP_DATA_TX(color, WAVE_175C_LCD_BITS_PER_PIXEL);
}

static esp_err_t wait_for_lcd_flush(void)
{
    if (s_lcd_flush_done == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lcd_flush_done, pdMS_TO_TICKS(WAVE_175C_LCD_FLUSH_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t display_draw_solid_rect(int x_start, int y_start, int x_end, int y_end, uint16_t color)
{
    if (!s_lcd_ready || s_lcd_panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (x_start < 0 || y_start < 0 || x_start >= x_end || y_start >= y_end ||
        x_end > WAVE_175C_LCD_WIDTH || y_end > WAVE_175C_LCD_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    const int width = x_end - x_start;
    const int chunk_rows = WAVE_175C_LCD_TRANSFER_ROWS;
    const size_t chunk_pixels = (size_t)width * chunk_rows;
    uint16_t *buffer = (uint16_t *)heap_caps_malloc(chunk_pixels * sizeof(uint16_t),
                                                    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buffer == NULL) {
        buffer = (uint16_t *)heap_caps_malloc(chunk_pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    }
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = ESP_OK;
    const uint16_t panel_color = rgb565_to_panel_wire(color);
    for (int y = y_start; y < y_end; y += chunk_rows) {
        const int rows = (y + chunk_rows <= y_end) ? chunk_rows : (y_end - y);
        const size_t pixels = (size_t)width * rows;
        for (size_t i = 0; i < pixels; ++i) {
            buffer[i] = panel_color;
        }

        ret = esp_lcd_panel_draw_bitmap(s_lcd_panel, x_start, y, x_end, y + rows, buffer);
        if (ret != ESP_OK) {
            break;
        }
        ret = wait_for_lcd_flush();
        if (ret != ESP_OK) {
            break;
        }
    }

    free(buffer);
    return ret;
}

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

esp_err_t board_wave_175c_display_init(void)
{
    if (s_lcd_ready) {
        return ESP_OK;
    }

    s_lcd_flush_done = xSemaphoreCreateBinary();
    if (s_lcd_flush_done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    bool spi_bus_ready = false;
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Initialize CO5300 AMOLED over QSPI: CS=%d SCLK=%d D0=%d D1=%d D2=%d D3=%d RST=%d",
             WAVE_175C_LCD_CS,
             WAVE_175C_LCD_SCLK,
             WAVE_175C_LCD_SDIO0,
             WAVE_175C_LCD_SDIO1,
             WAVE_175C_LCD_SDIO2,
             WAVE_175C_LCD_SDIO3,
             WAVE_175C_LCD_RESET);

    const spi_bus_config_t bus_config = CO5300_PANEL_BUS_QSPI_CONFIG(
        WAVE_175C_LCD_SCLK,
        WAVE_175C_LCD_SDIO0,
        WAVE_175C_LCD_SDIO1,
        WAVE_175C_LCD_SDIO2,
        WAVE_175C_LCD_SDIO3,
        WAVE_175C_LCD_MAX_TRANSFER_BYTES);
    ret = spi_bus_initialize(WAVE_175C_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD SPI bus init failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    spi_bus_ready = true;

    esp_lcd_panel_io_spi_config_t io_config = CO5300_PANEL_IO_QSPI_CONFIG(
        WAVE_175C_LCD_CS,
        lcd_flush_done_cb,
        s_lcd_flush_done);
    io_config.trans_queue_depth = WAVE_175C_LCD_QSPI_QUEUE_DEPTH;
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)WAVE_175C_LCD_HOST, &io_config, &s_lcd_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD panel IO init failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    co5300_vendor_config_t vendor_config = {
        .init_cmds = LCD_INIT_CMDS,
        .init_cmds_size = sizeof(LCD_INIT_CMDS) / sizeof(LCD_INIT_CMDS[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = WAVE_175C_LCD_RESET,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = WAVE_175C_LCD_BITS_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ret = esp_lcd_new_panel_co5300(s_lcd_io, &panel_config, &s_lcd_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CO5300 panel create failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    ESP_GOTO_ON_ERROR(esp_lcd_panel_set_gap(s_lcd_panel, WAVE_175C_LCD_GAP_X, WAVE_175C_LCD_GAP_Y),
                      fail, TAG, "LCD set gap failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(s_lcd_panel), fail, TAG, "LCD reset failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(s_lcd_panel), fail, TAG, "LCD init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(s_lcd_panel, true), fail, TAG, "LCD display-on failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_co5300_set_brightness(s_lcd_panel, 100),
                      fail, TAG, "LCD brightness failed");

    s_lcd_ready = true;
    ESP_LOGI(TAG, "CO5300 AMOLED is ready (%dx%d RGB565)",
             WAVE_175C_LCD_WIDTH,
             WAVE_175C_LCD_HEIGHT);
    return ESP_OK;

fail:
    if (s_lcd_panel != NULL) {
        (void)esp_lcd_panel_del(s_lcd_panel);
        s_lcd_panel = NULL;
    }
    if (s_lcd_io != NULL) {
        (void)esp_lcd_panel_io_del(s_lcd_io);
        s_lcd_io = NULL;
    }
    if (spi_bus_ready) {
        (void)spi_bus_free(WAVE_175C_LCD_HOST);
    }
    if (s_lcd_flush_done != NULL) {
        vSemaphoreDelete(s_lcd_flush_done);
        s_lcd_flush_done = NULL;
    }
    s_lcd_ready = false;
    return ret;
}

esp_err_t board_wave_175c_display_draw_test_pattern(void)
{
    ESP_RETURN_ON_ERROR(board_wave_175c_display_init(), TAG, "LCD init failed");

    static const uint16_t COLORS[] = {
        0xF800,
        0x07E0,
        0x001F,
        0xFFE0,
        0x07FF,
        0xF81F,
    };
    const int color_count = sizeof(COLORS) / sizeof(COLORS[0]);
    for (int i = 0; i < color_count; ++i) {
        const int y_start = (WAVE_175C_LCD_HEIGHT * i) / color_count;
        const int y_end = (WAVE_175C_LCD_HEIGHT * (i + 1)) / color_count;
        ESP_RETURN_ON_ERROR(display_draw_solid_rect(0, y_start, WAVE_175C_LCD_WIDTH, y_end, COLORS[i]),
                            TAG, "LCD color-bar draw failed");
    }
    return ESP_OK;
}

esp_err_t board_wave_175c_display_fill_rgb565(uint16_t color)
{
    ESP_RETURN_ON_ERROR(board_wave_175c_display_init(), TAG, "LCD init failed");
    return display_draw_solid_rect(0, 0, WAVE_175C_LCD_WIDTH, WAVE_175C_LCD_HEIGHT, color);
}

bool board_wave_175c_display_is_ready(void)
{
    return s_lcd_ready;
}
