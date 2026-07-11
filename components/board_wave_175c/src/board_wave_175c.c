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
#include <stddef.h>
#include <stdlib.h>

static const char *TAG = "board_175c";

#define WAVE_175C_LCD_HOST SPI2_HOST
#define WAVE_175C_LCD_QSPI_QUEUE_DEPTH 10
#define WAVE_175C_LCD_FLUSH_TIMEOUT_MS 2000
#define WAVE_175C_LCD_MAX_TRANSFER_BYTES \
    (WAVE_175C_LCD_WIDTH * WAVE_175C_LCD_HEIGHT * WAVE_175C_LCD_BITS_PER_PIXEL / 8)
#define WAVE_175C_FONT_WIDTH 5
#define WAVE_175C_FONT_HEIGHT 7
#define WAVE_175C_FONT_SPACING 1
#define WAVE_175C_MAX_TEXT_CHARS 48

#define RGB565_BLACK 0x0000
#define RGB565_WHITE 0xFFFF
#define RGB565_PANEL_BG 0x0841
#define RGB565_PANEL_SURFACE 0x1082
#define RGB565_PANEL_MUTED 0x8410
#define RGB565_PANEL_ERROR 0xF800

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

static const uint8_t *font5x7_for_char(char raw)
{
    static const uint8_t space[] = {0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t unknown[] = {0x02, 0x01, 0x51, 0x09, 0x06};

    if (raw >= 'a' && raw <= 'z') {
        raw = (char)(raw - 'a' + 'A');
    }

    switch (raw) {
    case ' ':
        return space;
    case '!': {
        static const uint8_t glyph[] = {0x00, 0x00, 0x5F, 0x00, 0x00};
        return glyph;
    }
    case '.': {
        static const uint8_t glyph[] = {0x00, 0x60, 0x60, 0x00, 0x00};
        return glyph;
    }
    case ':': {
        static const uint8_t glyph[] = {0x00, 0x36, 0x36, 0x00, 0x00};
        return glyph;
    }
    case '-': {
        static const uint8_t glyph[] = {0x08, 0x08, 0x08, 0x08, 0x08};
        return glyph;
    }
    case '_': {
        static const uint8_t glyph[] = {0x40, 0x40, 0x40, 0x40, 0x40};
        return glyph;
    }
    case '/': {
        static const uint8_t glyph[] = {0x20, 0x10, 0x08, 0x04, 0x02};
        return glyph;
    }
    case '0': {
        static const uint8_t glyph[] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
        return glyph;
    }
    case '1': {
        static const uint8_t glyph[] = {0x00, 0x42, 0x7F, 0x40, 0x00};
        return glyph;
    }
    case '2': {
        static const uint8_t glyph[] = {0x42, 0x61, 0x51, 0x49, 0x46};
        return glyph;
    }
    case '3': {
        static const uint8_t glyph[] = {0x21, 0x41, 0x45, 0x4B, 0x31};
        return glyph;
    }
    case '4': {
        static const uint8_t glyph[] = {0x18, 0x14, 0x12, 0x7F, 0x10};
        return glyph;
    }
    case '5': {
        static const uint8_t glyph[] = {0x27, 0x45, 0x45, 0x45, 0x39};
        return glyph;
    }
    case '6': {
        static const uint8_t glyph[] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
        return glyph;
    }
    case '7': {
        static const uint8_t glyph[] = {0x01, 0x71, 0x09, 0x05, 0x03};
        return glyph;
    }
    case '8': {
        static const uint8_t glyph[] = {0x36, 0x49, 0x49, 0x49, 0x36};
        return glyph;
    }
    case '9': {
        static const uint8_t glyph[] = {0x06, 0x49, 0x49, 0x29, 0x1E};
        return glyph;
    }
    case 'A': {
        static const uint8_t glyph[] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
        return glyph;
    }
    case 'B': {
        static const uint8_t glyph[] = {0x7F, 0x49, 0x49, 0x49, 0x36};
        return glyph;
    }
    case 'C': {
        static const uint8_t glyph[] = {0x3E, 0x41, 0x41, 0x41, 0x22};
        return glyph;
    }
    case 'D': {
        static const uint8_t glyph[] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
        return glyph;
    }
    case 'E': {
        static const uint8_t glyph[] = {0x7F, 0x49, 0x49, 0x49, 0x41};
        return glyph;
    }
    case 'F': {
        static const uint8_t glyph[] = {0x7F, 0x09, 0x09, 0x09, 0x01};
        return glyph;
    }
    case 'G': {
        static const uint8_t glyph[] = {0x3E, 0x41, 0x49, 0x49, 0x7A};
        return glyph;
    }
    case 'H': {
        static const uint8_t glyph[] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
        return glyph;
    }
    case 'I': {
        static const uint8_t glyph[] = {0x00, 0x41, 0x7F, 0x41, 0x00};
        return glyph;
    }
    case 'J': {
        static const uint8_t glyph[] = {0x20, 0x40, 0x41, 0x3F, 0x01};
        return glyph;
    }
    case 'K': {
        static const uint8_t glyph[] = {0x7F, 0x08, 0x14, 0x22, 0x41};
        return glyph;
    }
    case 'L': {
        static const uint8_t glyph[] = {0x7F, 0x40, 0x40, 0x40, 0x40};
        return glyph;
    }
    case 'M': {
        static const uint8_t glyph[] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
        return glyph;
    }
    case 'N': {
        static const uint8_t glyph[] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
        return glyph;
    }
    case 'O': {
        static const uint8_t glyph[] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
        return glyph;
    }
    case 'P': {
        static const uint8_t glyph[] = {0x7F, 0x09, 0x09, 0x09, 0x06};
        return glyph;
    }
    case 'Q': {
        static const uint8_t glyph[] = {0x3E, 0x41, 0x51, 0x21, 0x5E};
        return glyph;
    }
    case 'R': {
        static const uint8_t glyph[] = {0x7F, 0x09, 0x19, 0x29, 0x46};
        return glyph;
    }
    case 'S': {
        static const uint8_t glyph[] = {0x46, 0x49, 0x49, 0x49, 0x31};
        return glyph;
    }
    case 'T': {
        static const uint8_t glyph[] = {0x01, 0x01, 0x7F, 0x01, 0x01};
        return glyph;
    }
    case 'U': {
        static const uint8_t glyph[] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
        return glyph;
    }
    case 'V': {
        static const uint8_t glyph[] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
        return glyph;
    }
    case 'W': {
        static const uint8_t glyph[] = {0x3F, 0x40, 0x38, 0x40, 0x3F};
        return glyph;
    }
    case 'X': {
        static const uint8_t glyph[] = {0x63, 0x14, 0x08, 0x14, 0x63};
        return glyph;
    }
    case 'Y': {
        static const uint8_t glyph[] = {0x07, 0x08, 0x70, 0x08, 0x07};
        return glyph;
    }
    case 'Z': {
        static const uint8_t glyph[] = {0x61, 0x51, 0x49, 0x45, 0x43};
        return glyph;
    }
    default:
        return unknown;
    }
}

static size_t display_text_len(const char *text)
{
    if (text == NULL) {
        return 0;
    }

    size_t len = 0;
    while (text[len] != '\0' && len < WAVE_175C_MAX_TEXT_CHARS) {
        ++len;
    }
    return len;
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

static esp_err_t display_draw_text_line(
    int x,
    int y,
    const char *text,
    int scale,
    uint16_t foreground,
    uint16_t background)
{
    if (!s_lcd_ready || s_lcd_panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (text == NULL || scale <= 0 || x < 0 || y < 0 || x >= WAVE_175C_LCD_WIDTH || y >= WAVE_175C_LCD_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t char_count = display_text_len(text);
    const int char_stride = (WAVE_175C_FONT_WIDTH + WAVE_175C_FONT_SPACING) * scale;
    const int max_chars = (WAVE_175C_LCD_WIDTH - x + scale) / char_stride;
    if (max_chars <= 0) {
        return ESP_OK;
    }
    if (char_count > (size_t)max_chars) {
        char_count = (size_t)max_chars;
    }
    if (char_count == 0) {
        return ESP_OK;
    }

    const int width = (int)char_count * char_stride - WAVE_175C_FONT_SPACING * scale;
    const int height = WAVE_175C_FONT_HEIGHT * scale;
    if (x + width > WAVE_175C_LCD_WIDTH || y + height > WAVE_175C_LCD_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t *buffer = (uint16_t *)heap_caps_malloc((size_t)width * height * sizeof(uint16_t),
                                                    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buffer == NULL) {
        buffer = (uint16_t *)heap_caps_malloc((size_t)width * height * sizeof(uint16_t), MALLOC_CAP_DMA);
    }
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const uint16_t bg = rgb565_to_panel_wire(background);
    const uint16_t fg = rgb565_to_panel_wire(foreground);
    for (size_t i = 0; i < (size_t)width * height; ++i) {
        buffer[i] = bg;
    }

    for (size_t char_index = 0; char_index < char_count; ++char_index) {
        const uint8_t *glyph = font5x7_for_char(text[char_index]);
        const int char_x = (int)char_index * char_stride;
        for (int col = 0; col < WAVE_175C_FONT_WIDTH; ++col) {
            for (int row = 0; row < WAVE_175C_FONT_HEIGHT; ++row) {
                if ((glyph[col] & (1u << row)) == 0) {
                    continue;
                }
                for (int sy = 0; sy < scale; ++sy) {
                    const int pixel_y = row * scale + sy;
                    for (int sx = 0; sx < scale; ++sx) {
                        const int pixel_x = char_x + col * scale + sx;
                        buffer[(size_t)pixel_y * width + pixel_x] = fg;
                    }
                }
            }
        }
    }

    esp_err_t ret = esp_lcd_panel_draw_bitmap(s_lcd_panel, x, y, x + width, y + height, buffer);
    if (ret == ESP_OK) {
        ret = wait_for_lcd_flush();
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

esp_err_t board_wave_175c_display_show_page(const board_wave_175c_display_page_t *page)
{
    if (page == NULL || page->title == NULL || page->status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(board_wave_175c_display_init(), TAG, "LCD init failed");

    const uint16_t accent = page->accent_rgb565 == 0 ? RGB565_WHITE : page->accent_rgb565;
    const uint16_t status_color = page->is_error ? RGB565_PANEL_ERROR : accent;

    ESP_RETURN_ON_ERROR(display_draw_solid_rect(0, 0, WAVE_175C_LCD_WIDTH, WAVE_175C_LCD_HEIGHT, RGB565_PANEL_BG),
                        TAG, "LCD background draw failed");
    ESP_RETURN_ON_ERROR(display_draw_solid_rect(0, 0, WAVE_175C_LCD_WIDTH, 18, accent),
                        TAG, "LCD accent draw failed");
    ESP_RETURN_ON_ERROR(display_draw_solid_rect(28, 34, WAVE_175C_LCD_WIDTH - 28, WAVE_175C_LCD_HEIGHT - 34,
                                                RGB565_PANEL_SURFACE),
                        TAG, "LCD surface draw failed");
    ESP_RETURN_ON_ERROR(display_draw_solid_rect(44, 160, WAVE_175C_LCD_WIDTH - 44, 166, accent),
                        TAG, "LCD divider draw failed");

    ESP_RETURN_ON_ERROR(display_draw_text_line(44, 58, page->title, 5, RGB565_WHITE, RGB565_PANEL_SURFACE),
                        TAG, "LCD title draw failed");
    ESP_RETURN_ON_ERROR(display_draw_text_line(44, 118, page->status, 4, status_color, RGB565_PANEL_SURFACE),
                        TAG, "LCD status draw failed");

    if (page->detail != NULL) {
        ESP_RETURN_ON_ERROR(display_draw_text_line(44, 204, page->detail, 3, RGB565_WHITE, RGB565_PANEL_SURFACE),
                            TAG, "LCD detail draw failed");
    }
    if (page->hint != NULL) {
        ESP_RETURN_ON_ERROR(display_draw_text_line(44, 262, page->hint, 3, RGB565_PANEL_MUTED, RGB565_PANEL_SURFACE),
                            TAG, "LCD hint draw failed");
    }
    ESP_RETURN_ON_ERROR(display_draw_text_line(44, 374, "USB SERIAL READY", 2, RGB565_PANEL_MUTED, RGB565_PANEL_SURFACE),
                        TAG, "LCD footer draw failed");
    return ESP_OK;
}

bool board_wave_175c_display_is_ready(void)
{
    return s_lcd_ready;
}
