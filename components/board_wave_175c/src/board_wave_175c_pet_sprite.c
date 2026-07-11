#include "board_wave_175c_pet_sprite.h"

#include <stddef.h>

#define C0 0x0000
#define C1 0xFEE0
#define C2 0xFC9F
#define C3 0xFFFF
#define C4 0xFD20
#define C5 0x07FF
#define C6 0x07E0

#define SPRITE_CENTER_X 233
#define SPRITE_CENTER_Y 214

static const uint16_t SPRITE_HAPPY[] = {
    0, 0, 0, C1, C1, 0, 0, 0, 0, 0, 0, C1, C1, 0, 0, 0,
    0, 0, C1, C1, C1, C1, 0, 0, 0, 0, C1, C1, C1, C1, 0, 0,
    0, 0, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, 0, 0,
    0, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, 0,
    C1, C1, C1, C0, C0, C1, C1, C1, C1, C1, C0, C0, C1, C1, C1, C1,
    C1, C1, C1, C0, C3, C1, C1, C1, C1, C1, C0, C3, C1, C1, C1, C1,
    C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1,
    C1, C2, C2, C1, C1, C1, C1, C1, C1, C1, C1, C1, C2, C2, C1, C1,
    C1, C1, C1, C1, C0, C1, C1, C1, C1, C1, C1, C0, C1, C1, C1, C1,
    C1, C1, C1, C1, C1, C0, C1, C1, C1, C1, C0, C1, C1, C1, C1, C1,
    0, C1, C1, C1, C1, C1, C0, C0, C0, C0, C1, C1, C1, C1, C1, 0,
    0, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, 0,
    0, 0, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, 0, 0,
    0, 0, 0, C1, C1, C1, C1, C1, C1, C1, C1, C1, C1, 0, 0, 0,
    0, 0, 0, 0, C1, C1, C1, 0, 0, C1, C1, C1, 0, 0, 0, 0,
    0, 0, 0, C0, C0, 0, 0, 0, 0, 0, 0, C0, C0, 0, 0, 0,
};

static const uint16_t SPRITE_LISTENING[] = {
    0, 0, 0, C5, C5, 0, 0, 0, 0, 0, 0, C5, C5, 0, 0, 0,
    0, 0, C5, C5, C5, C5, 0, 0, 0, 0, C5, C5, C5, C5, 0, 0,
    0, 0, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, 0, 0,
    0, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, 0,
    C5, C5, C5, C0, C0, C5, C5, C5, C5, C5, C0, C0, C5, C5, C5, C5,
    C5, C5, C5, C0, C3, C5, C5, C5, C5, C5, C0, C3, C5, C5, C5, C5,
    C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5,
    C5, C2, C2, C5, C5, C5, C5, C5, C5, C5, C5, C5, C2, C2, C5, C5,
    C5, C5, C5, C5, C5, C5, C0, C0, C0, C0, C5, C5, C5, C5, C5, C5,
    C5, C5, C5, C5, C5, C0, C1, C1, C1, C1, C0, C5, C5, C5, C5, C5,
    0, C5, C5, C5, C5, C0, C1, C1, C1, C1, C0, C5, C5, C5, C5, 0,
    0, C5, C5, C5, C5, C5, C0, C0, C0, C0, C5, C5, C5, C5, C5, 0,
    0, 0, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, 0, 0,
    0, 0, 0, C5, C5, C5, C5, C5, C5, C5, C5, C5, C5, 0, 0, 0,
    0, 0, 0, 0, C5, C5, C5, 0, 0, C5, C5, C5, 0, 0, 0, 0,
    0, 0, 0, C0, C0, 0, 0, 0, 0, 0, 0, C0, C0, 0, 0, 0,
};

static const uint16_t SPRITE_THINKING[] = {
    0, 0, 0, C4, C4, 0, 0, 0, 0, 0, 0, C4, C4, 0, 0, 0,
    0, 0, C4, C4, C4, C4, 0, 0, 0, 0, C4, C4, C4, C4, 0, 0,
    0, 0, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, 0, 0,
    0, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, 0,
    C4, C4, C4, C0, C0, C4, C4, C4, C4, C4, C0, C0, C4, C4, C4, C4,
    C4, C4, C4, C4, C0, C4, C4, C4, C4, C4, C4, C0, C4, C4, C4, C4,
    C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4,
    C4, C2, C2, C4, C4, C4, C4, C4, C4, C4, C4, C4, C2, C2, C4, C4,
    C4, C4, C4, C4, C4, C4, C0, C0, C0, C0, C4, C4, C4, C4, C4, C4,
    C4, C4, C4, C4, C4, C0, C4, C4, C4, C4, C0, C4, C4, C4, C4, C4,
    0, C4, C4, C4, C4, C4, C0, C0, C0, C0, C4, C4, C4, C4, C4, 0,
    0, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, 0,
    0, 0, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, 0, 0,
    0, 0, 0, C4, C4, C4, C4, C4, C4, C4, C4, C4, C4, 0, 0, 0,
    0, 0, 0, 0, C4, C4, C4, 0, 0, C4, C4, C4, 0, 0, 0, 0,
    0, 0, 0, C0, C0, 0, 0, 0, 0, 0, 0, C0, C0, 0, 0, 0,
};

static const uint16_t SPRITE_WORRIED[] = {
    0, 0, 0, C6, C6, 0, 0, 0, 0, 0, 0, C6, C6, 0, 0, 0,
    0, 0, C6, C6, C6, C6, 0, 0, 0, 0, C6, C6, C6, C6, 0, 0,
    0, 0, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, 0, 0,
    0, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, 0,
    C6, C6, C6, C0, C0, C6, C6, C6, C6, C6, C0, C0, C6, C6, C6, C6,
    C6, C6, C6, C6, C0, C6, C6, C6, C6, C6, C6, C0, C6, C6, C6, C6,
    C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6,
    C6, C2, C2, C6, C6, C6, C6, C6, C6, C6, C6, C6, C2, C2, C6, C6,
    C6, C6, C6, C6, C6, C0, C0, C0, C0, C0, C0, C6, C6, C6, C6, C6,
    C6, C6, C6, C6, C0, C6, C6, C6, C6, C6, C6, C0, C6, C6, C6, C6,
    0, C6, C6, C0, C6, C6, C6, C6, C6, C6, C6, C6, C0, C6, C6, 0,
    0, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, 0,
    0, 0, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, 0, 0,
    0, 0, 0, C6, C6, C6, C6, C6, C6, C6, C6, C6, C6, 0, 0, 0,
    0, 0, 0, 0, C6, C6, C6, 0, 0, C6, C6, C6, 0, 0, 0, 0,
    0, 0, 0, C0, C0, 0, 0, 0, 0, 0, 0, C0, C0, 0, 0, 0,
};

static const board_wave_175c_pet_sprite_t SPRITES[] = {
    {BOARD_WAVE_175C_PET_EXPRESSION_SLEEPY, SPRITE_THINKING, 256, 16, 16, 9, SPRITE_CENTER_X, SPRITE_CENTER_Y},
    {BOARD_WAVE_175C_PET_EXPRESSION_CURIOUS, SPRITE_THINKING, 256, 16, 16, 9, SPRITE_CENTER_X, SPRITE_CENTER_Y},
    {BOARD_WAVE_175C_PET_EXPRESSION_HAPPY, SPRITE_HAPPY, 256, 16, 16, 9, SPRITE_CENTER_X, SPRITE_CENTER_Y},
    {BOARD_WAVE_175C_PET_EXPRESSION_LISTENING, SPRITE_LISTENING, 256, 16, 16, 9, SPRITE_CENTER_X, SPRITE_CENTER_Y},
    {BOARD_WAVE_175C_PET_EXPRESSION_THINKING, SPRITE_THINKING, 256, 16, 16, 9, SPRITE_CENTER_X, SPRITE_CENTER_Y},
    {BOARD_WAVE_175C_PET_EXPRESSION_WORRIED, SPRITE_WORRIED, 256, 16, 16, 9, SPRITE_CENTER_X, SPRITE_CENTER_Y},
};

const board_wave_175c_pet_sprite_t *board_wave_175c_pet_sprite_for_expression(
    board_wave_175c_pet_expression_t expression)
{
    for (size_t index = 0; index < sizeof(SPRITES) / sizeof(SPRITES[0]); ++index) {
        if (SPRITES[index].expression == expression) {
            return &SPRITES[index];
        }
    }
    return &SPRITES[2];
}

bool board_wave_175c_pet_sprite_rect(
    const board_wave_175c_pet_sprite_t *sprite,
    board_wave_175c_display_rect_t *rect)
{
    if (sprite == NULL || rect == NULL || sprite->pixels == NULL ||
        sprite->width <= 0 || sprite->height <= 0 || sprite->scale <= 0 ||
        sprite->pixel_count != (size_t)sprite->width * (size_t)sprite->height) {
        return false;
    }

    const int width = sprite->width * sprite->scale;
    const int height = sprite->height * sprite->scale;
    *rect = (board_wave_175c_display_rect_t){
        .x = sprite->center_x - width / 2,
        .y = sprite->center_y - height / 2,
        .width = width,
        .height = height,
    };
    return board_wave_175c_display_rect_inside_safe_circle(rect->x, rect->y, rect->width, rect->height);
}

bool board_wave_175c_pet_sprite_layout_is_circle_safe(void)
{
    board_wave_175c_display_rect_t rect = {0};
    for (size_t index = 0; index < sizeof(SPRITES) / sizeof(SPRITES[0]); ++index) {
        if (!board_wave_175c_pet_sprite_rect(&SPRITES[index], &rect)) {
            return false;
        }
    }
    return true;
}

#undef C0
#undef C1
#undef C2
#undef C3
#undef C4
#undef C5
#undef C6
