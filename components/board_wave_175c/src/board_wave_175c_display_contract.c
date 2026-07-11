#include "board_wave_175c_display_contract.h"

#include "board_wave_175c_pins.h"

static int square(int value)
{
    return value * value;
}

int board_wave_175c_display_safe_center_x(void)
{
    return WAVE_175C_LCD_WIDTH / 2;
}

int board_wave_175c_display_safe_center_y(void)
{
    return WAVE_175C_LCD_HEIGHT / 2;
}

int board_wave_175c_display_safe_radius(void)
{
    return (WAVE_175C_LCD_WIDTH / 2) - BOARD_WAVE_175C_DISPLAY_SAFE_MARGIN;
}

int board_wave_175c_display_text_width(size_t char_count, int scale)
{
    if (char_count == 0 || scale <= 0) {
        return 0;
    }
    return ((int)char_count * (BOARD_WAVE_175C_FONT_WIDTH + BOARD_WAVE_175C_FONT_SPACING) -
            BOARD_WAVE_175C_FONT_SPACING) *
           scale;
}

bool board_wave_175c_display_rect_inside_safe_circle(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    const int cx = board_wave_175c_display_safe_center_x();
    const int cy = board_wave_175c_display_safe_center_y();
    const int r2 = square(board_wave_175c_display_safe_radius());
    const int left = x;
    const int right = x + width - 1;
    const int top = y;
    const int bottom = y + height - 1;

    return square(left - cx) + square(top - cy) <= r2 &&
           square(right - cx) + square(top - cy) <= r2 &&
           square(left - cx) + square(bottom - cy) <= r2 &&
           square(right - cx) + square(bottom - cy) <= r2;
}

bool board_wave_175c_display_centered_text_rect(
    size_t char_count,
    int scale,
    int y,
    board_wave_175c_display_rect_t *rect)
{
    if (rect == NULL || scale <= 0 || y < 0) {
        return false;
    }

    const int width = board_wave_175c_display_text_width(char_count, scale);
    const int height = BOARD_WAVE_175C_FONT_HEIGHT * scale;
    if (width <= 0 || height <= 0 || width > WAVE_175C_LCD_WIDTH || y + height > WAVE_175C_LCD_HEIGHT) {
        return false;
    }

    *rect = (board_wave_175c_display_rect_t){
        .x = (WAVE_175C_LCD_WIDTH - width) / 2,
        .y = y,
        .width = width,
        .height = height,
    };
    return board_wave_175c_display_rect_inside_safe_circle(rect->x, rect->y, rect->width, rect->height);
}

bool board_wave_175c_pet_face_rect_inside_safe_circle(void)
{
    const int diameter = BOARD_WAVE_175C_PET_FACE_RADIUS * 2;
    const int x = BOARD_WAVE_175C_PET_FACE_CENTER_X - BOARD_WAVE_175C_PET_FACE_RADIUS;
    const int y = BOARD_WAVE_175C_PET_FACE_CENTER_Y - BOARD_WAVE_175C_PET_FACE_RADIUS;
    return board_wave_175c_display_rect_inside_safe_circle(x, y, diameter, diameter);
}

bool board_wave_175c_pet_layout_is_circle_safe(void)
{
    board_wave_175c_display_rect_t rect = {0};
    return board_wave_175c_pet_face_rect_inside_safe_circle() &&
           board_wave_175c_display_centered_text_rect(6, 2, BOARD_WAVE_175C_PET_TITLE_Y, &rect) &&
           board_wave_175c_display_centered_text_rect(14, 4, BOARD_WAVE_175C_PET_STATUS_Y, &rect) &&
           board_wave_175c_display_centered_text_rect(23, 2, BOARD_WAVE_175C_PET_DETAIL_Y, &rect) &&
           board_wave_175c_display_centered_text_rect(18, 2, BOARD_WAVE_175C_PET_HINT_Y, &rect);
}
