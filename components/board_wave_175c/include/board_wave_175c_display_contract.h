#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_WAVE_175C_DISPLAY_SAFE_MARGIN 28
#define BOARD_WAVE_175C_FONT_WIDTH 5
#define BOARD_WAVE_175C_FONT_HEIGHT 7
#define BOARD_WAVE_175C_FONT_SPACING 1
#define BOARD_WAVE_175C_MAX_TEXT_CHARS 48

#define BOARD_WAVE_175C_PET_FACE_CENTER_X 233
#define BOARD_WAVE_175C_PET_FACE_CENTER_Y 148
#define BOARD_WAVE_175C_PET_FACE_RADIUS 74

#define BOARD_WAVE_175C_PET_TITLE_Y 42
#define BOARD_WAVE_175C_PET_STATUS_Y 280
#define BOARD_WAVE_175C_PET_DETAIL_Y 334
#define BOARD_WAVE_175C_PET_HINT_Y 372

typedef struct {
    int x;
    int y;
    int width;
    int height;
} board_wave_175c_display_rect_t;

int board_wave_175c_display_safe_center_x(void);
int board_wave_175c_display_safe_center_y(void);
int board_wave_175c_display_safe_radius(void);
int board_wave_175c_display_text_width(size_t char_count, int scale);
bool board_wave_175c_display_rect_inside_safe_circle(int x, int y, int width, int height);
bool board_wave_175c_display_centered_text_rect(
    size_t char_count,
    int scale,
    int y,
    board_wave_175c_display_rect_t *rect);
bool board_wave_175c_pet_face_rect_inside_safe_circle(void);
bool board_wave_175c_pet_layout_is_circle_safe(void);

#ifdef __cplusplus
}
#endif
