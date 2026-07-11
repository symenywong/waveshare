#pragma once

#include "board_wave_175c_display_contract.h"
#include "board_wave_175c_pet_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_WAVE_175C_PET_SPRITE_WIDTH 16
#define BOARD_WAVE_175C_PET_SPRITE_HEIGHT 16
#define BOARD_WAVE_175C_PET_SPRITE_SCALE 9

typedef struct {
    board_wave_175c_pet_expression_t expression;
    const uint16_t *pixels;
    size_t pixel_count;
    int width;
    int height;
    int scale;
    int center_x;
    int center_y;
} board_wave_175c_pet_sprite_t;

const board_wave_175c_pet_sprite_t *board_wave_175c_pet_sprite_for_expression(
    board_wave_175c_pet_expression_t expression);

bool board_wave_175c_pet_sprite_rect(
    const board_wave_175c_pet_sprite_t *sprite,
    board_wave_175c_display_rect_t *rect);

bool board_wave_175c_pet_sprite_layout_is_circle_safe(void);

#ifdef __cplusplus
}
#endif
