#include "board_wave_175c_pet_sprite.h"

#include <stddef.h>
#include <string.h>

#define PX_CLEAR 0x0000
#define PX_OUTLINE 0x086B
#define PX_CLOUD 0x6B5F
#define PX_CLOUD_LIGHT 0x8C7F
#define PX_CLOUD_SHADOW 0x4A3F
#define PX_FACE 0x094A
#define PX_FACE_DARK 0x0526
#define PX_CYAN 0x97FF
#define PX_CYAN_DIM 0x2D7F
#define PX_BODY 0x4A7F
#define PX_BODY_LIGHT 0x6B9F
#define PX_BLUSH 0xFC9F
#define PX_YELLOW 0xFFE0
#define PX_ORANGE 0xFD20
#define PX_TEAR 0x5DFF

#define SPRITE_CENTER_X 233
#define SPRITE_CENTER_Y 182
#define SPRITE_PIXELS (BOARD_WAVE_175C_PET_SPRITE_WIDTH * BOARD_WAVE_175C_PET_SPRITE_HEIGHT)

static const board_wave_175c_pet_sprite_t SPRITES[] = {
    {BOARD_WAVE_175C_PET_EXPRESSION_SLEEPY, SPRITE_PIXELS, 24, 24, 7, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_IDLE, SPRITE_PIXELS, 24, 24, 7, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_HAPPY, SPRITE_PIXELS, 24, 24, 7, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_LISTENING, SPRITE_PIXELS, 24, 24, 7, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_THINKING, SPRITE_PIXELS, 24, 24, 7, SPRITE_CENTER_X, SPRITE_CENTER_Y, 3},
    {BOARD_WAVE_175C_PET_EXPRESSION_SPEAKING, SPRITE_PIXELS, 24, 24, 7, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_SAD, SPRITE_PIXELS, 24, 24, 7, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_SHY, SPRITE_PIXELS, 24, 24, 7, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_FRUSTRATED, SPRITE_PIXELS, 24, 24, 7, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING, SPRITE_PIXELS, 24, 24, 7, SPRITE_CENTER_X, SPRITE_CENTER_Y, 4},
    {BOARD_WAVE_175C_PET_EXPRESSION_LAUGHING, SPRITE_PIXELS, 24, 24, 7, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_CRYING, SPRITE_PIXELS, 24, 24, 7, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_CURIOUS, SPRITE_PIXELS, 24, 24, 7, SPRITE_CENTER_X, SPRITE_CENTER_Y, 3},
    {BOARD_WAVE_175C_PET_EXPRESSION_WORRIED, SPRITE_PIXELS, 24, 24, 7, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
};

static void put_pixel(uint16_t *pixels, int x, int y, uint16_t color)
{
    if (pixels == NULL || x < 0 || y < 0 ||
        x >= BOARD_WAVE_175C_PET_SPRITE_WIDTH ||
        y >= BOARD_WAVE_175C_PET_SPRITE_HEIGHT) {
        return;
    }
    pixels[(size_t)y * BOARD_WAVE_175C_PET_SPRITE_WIDTH + (size_t)x] = color;
}

static void fill_rect(uint16_t *pixels, int x, int y, int width, int height, uint16_t color)
{
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            put_pixel(pixels, x + col, y + row, color);
        }
    }
}

static void fill_ellipse(uint16_t *pixels, int cx, int cy, int rx, int ry, uint16_t color)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    const int rx2 = rx * rx;
    const int ry2 = ry * ry;
    const int limit = rx2 * ry2;
    for (int y = cy - ry; y <= cy + ry; ++y) {
        for (int x = cx - rx; x <= cx + rx; ++x) {
            const int dx = x - cx;
            const int dy = y - cy;
            if (dx * dx * ry2 + dy * dy * rx2 <= limit) {
                put_pixel(pixels, x, y, color);
            }
        }
    }
}

static void draw_cloud(uint16_t *pixels, int y_shift)
{
    fill_ellipse(pixels, 12, 8 + y_shift, 9, 6, PX_OUTLINE);
    fill_ellipse(pixels, 7, 7 + y_shift, 4, 4, PX_OUTLINE);
    fill_ellipse(pixels, 12, 5 + y_shift, 5, 4, PX_OUTLINE);
    fill_ellipse(pixels, 17, 7 + y_shift, 4, 4, PX_OUTLINE);
    fill_ellipse(pixels, 5, 10 + y_shift, 3, 3, PX_OUTLINE);
    fill_ellipse(pixels, 19, 10 + y_shift, 3, 3, PX_OUTLINE);

    fill_ellipse(pixels, 12, 8 + y_shift, 8, 5, PX_CLOUD);
    fill_ellipse(pixels, 7, 7 + y_shift, 3, 3, PX_CLOUD);
    fill_ellipse(pixels, 12, 5 + y_shift, 4, 3, PX_CLOUD_LIGHT);
    fill_ellipse(pixels, 17, 7 + y_shift, 3, 3, PX_CLOUD);
    fill_ellipse(pixels, 5, 10 + y_shift, 2, 2, PX_CLOUD_SHADOW);
    fill_ellipse(pixels, 19, 10 + y_shift, 2, 2, PX_CLOUD_SHADOW);

    put_pixel(pixels, 9, 3 + y_shift, PX_CLOUD_LIGHT);
    put_pixel(pixels, 10, 3 + y_shift, PX_CLOUD_LIGHT);
    put_pixel(pixels, 15, 4 + y_shift, PX_CLOUD_LIGHT);
}

static void draw_face_panel(uint16_t *pixels, int y_shift)
{
    fill_rect(pixels, 5, 6 + y_shift, 14, 8, PX_OUTLINE);
    put_pixel(pixels, 5, 6 + y_shift, PX_CLOUD);
    put_pixel(pixels, 18, 6 + y_shift, PX_CLOUD);
    put_pixel(pixels, 5, 13 + y_shift, PX_CLOUD_SHADOW);
    put_pixel(pixels, 18, 13 + y_shift, PX_CLOUD_SHADOW);

    fill_rect(pixels, 6, 7 + y_shift, 12, 6, PX_FACE);
    fill_rect(pixels, 7, 8 + y_shift, 10, 4, PX_FACE_DARK);
}

static void draw_body(uint16_t *pixels, int y_shift, bool lifted)
{
    const int leg_y = lifted ? 20 + y_shift : 21 + y_shift;
    fill_rect(pixels, 9, 14 + y_shift, 6, 6, PX_OUTLINE);
    fill_rect(pixels, 10, 15 + y_shift, 4, 4, PX_BODY);
    put_pixel(pixels, 10, 15 + y_shift, PX_BODY_LIGHT);
    put_pixel(pixels, 13, 15 + y_shift, PX_BODY_LIGHT);

    fill_rect(pixels, 6, 15 + y_shift, 2, 5, PX_OUTLINE);
    fill_rect(pixels, 7, 16 + y_shift, 1, 3, PX_BODY);
    fill_rect(pixels, 16, 15 + y_shift, 2, 5, PX_OUTLINE);
    fill_rect(pixels, 16, 16 + y_shift, 1, 3, PX_BODY);
    fill_rect(pixels, 9, leg_y, 2, 2, PX_OUTLINE);
    fill_rect(pixels, 13, leg_y, 2, 2, PX_OUTLINE);

    put_pixel(pixels, 11, 16 + y_shift, PX_CYAN);
    put_pixel(pixels, 12, 17 + y_shift, PX_CYAN);
    put_pixel(pixels, 11, 18 + y_shift, PX_CYAN);
    put_pixel(pixels, 13, 17 + y_shift, PX_CYAN_DIM);
}

static void draw_eye_arrow(uint16_t *pixels, int x, int y, uint16_t color)
{
    put_pixel(pixels, x, y, color);
    put_pixel(pixels, x + 1, y + 1, color);
    put_pixel(pixels, x, y + 2, color);
}

static void draw_eye_caret(uint16_t *pixels, int x, int y, uint16_t color)
{
    put_pixel(pixels, x, y + 1, color);
    put_pixel(pixels, x + 1, y, color);
    put_pixel(pixels, x + 2, y + 1, color);
}

static void draw_eye_sad(uint16_t *pixels, int x, int y)
{
    put_pixel(pixels, x, y, PX_CYAN);
    put_pixel(pixels, x + 1, y + 1, PX_CYAN);
    put_pixel(pixels, x + 2, y + 1, PX_CYAN);
}

static void draw_expression_face(
    uint16_t *pixels,
    board_wave_175c_pet_expression_t expression,
    size_t frame,
    int y_shift)
{
    switch (expression) {
    case BOARD_WAVE_175C_PET_EXPRESSION_SLEEPY:
        put_pixel(pixels, 8, 10 + y_shift, PX_CYAN_DIM);
        put_pixel(pixels, 9, 10 + y_shift, PX_CYAN_DIM);
        put_pixel(pixels, 14, 10 + y_shift, PX_CYAN_DIM);
        put_pixel(pixels, 15, 10 + y_shift, PX_CYAN_DIM);
        put_pixel(pixels, 19, 3 + y_shift, frame == 0 ? PX_CYAN_DIM : PX_CYAN);
        put_pixel(pixels, 20, 2 + y_shift, PX_CYAN_DIM);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_LISTENING:
        draw_eye_arrow(pixels, 8, 8 + y_shift, PX_CYAN);
        draw_eye_arrow(pixels, 14, 8 + y_shift, PX_CYAN);
        if (frame == 0) {
            put_pixel(pixels, 2, 9 + y_shift, PX_CYAN);
            put_pixel(pixels, 21, 9 + y_shift, PX_CYAN);
        } else {
            put_pixel(pixels, 1, 8 + y_shift, PX_CYAN_DIM);
            put_pixel(pixels, 2, 10 + y_shift, PX_CYAN);
            put_pixel(pixels, 21, 10 + y_shift, PX_CYAN);
            put_pixel(pixels, 22, 8 + y_shift, PX_CYAN_DIM);
        }
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_THINKING:
        put_pixel(pixels, 8, 9 + y_shift, PX_CYAN_DIM);
        put_pixel(pixels, 9, 9 + y_shift, PX_CYAN_DIM);
        put_pixel(pixels, 15, 9 + y_shift, PX_CYAN);
        put_pixel(pixels, 16, 9 + y_shift, PX_CYAN);
        put_pixel(pixels, 18 + (int)frame, 3 + y_shift, PX_YELLOW);
        put_pixel(pixels, 20, 5 + y_shift, PX_YELLOW);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_SPEAKING:
        draw_eye_arrow(pixels, 8, 8 + y_shift, PX_CYAN);
        put_pixel(pixels, 14, 9 + y_shift, PX_CYAN);
        put_pixel(pixels, 15, 9 + y_shift, PX_CYAN);
        put_pixel(pixels, 12, 11 + y_shift, PX_CYAN);
        put_pixel(pixels, 13, 11 + y_shift, frame == 0 ? PX_CYAN : PX_FACE_DARK);
        put_pixel(pixels, 20, 8 + y_shift, frame == 0 ? PX_CYAN : PX_CYAN_DIM);
        put_pixel(pixels, 21, 10 + y_shift, frame == 0 ? PX_CYAN_DIM : PX_CYAN);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_SAD:
        draw_eye_sad(pixels, 8, 8 + y_shift);
        draw_eye_sad(pixels, 14, 8 + y_shift);
        put_pixel(pixels, 11, 11 + y_shift, PX_CYAN_DIM);
        put_pixel(pixels, 12, 10 + y_shift, PX_CYAN_DIM);
        put_pixel(pixels, 13, 10 + y_shift, PX_CYAN_DIM);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_SHY:
        put_pixel(pixels, 8, 9 + y_shift, PX_CYAN);
        put_pixel(pixels, 15, 9 + y_shift, PX_CYAN);
        put_pixel(pixels, 8, 11 + y_shift, PX_BLUSH);
        put_pixel(pixels, 9, 11 + y_shift, PX_BLUSH);
        put_pixel(pixels, 15, 11 + y_shift, PX_BLUSH);
        put_pixel(pixels, 16, 11 + y_shift, PX_BLUSH);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_FRUSTRATED:
        put_pixel(pixels, 8, 8 + y_shift, PX_ORANGE);
        put_pixel(pixels, 9, 9 + y_shift, PX_ORANGE);
        put_pixel(pixels, 16, 8 + y_shift, PX_ORANGE);
        put_pixel(pixels, 15, 9 + y_shift, PX_ORANGE);
        put_pixel(pixels, 12, 11 + y_shift, PX_CYAN_DIM);
        put_pixel(pixels, 13, 11 + y_shift, PX_CYAN_DIM);
        put_pixel(pixels, 20, 4 + y_shift, PX_ORANGE);
        put_pixel(pixels, 21, 3 + y_shift, PX_ORANGE);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_LAUGHING:
        draw_eye_caret(pixels, 7, 8 + y_shift, PX_CYAN);
        draw_eye_caret(pixels, 14, 8 + y_shift, PX_CYAN);
        fill_rect(pixels, 11, 11 + y_shift, 3, 1, PX_CYAN);
        put_pixel(pixels, 10, 10 + y_shift, PX_CYAN);
        put_pixel(pixels, 14, 10 + y_shift, PX_CYAN);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_CRYING:
        draw_eye_sad(pixels, 8, 8 + y_shift);
        draw_eye_sad(pixels, 14, 8 + y_shift);
        put_pixel(pixels, 9, 11 + y_shift, PX_TEAR);
        put_pixel(pixels, 9, 12 + y_shift + (int)(frame % 2), PX_TEAR);
        put_pixel(pixels, 16, 11 + y_shift, PX_TEAR);
        put_pixel(pixels, 16, 12 + y_shift + (int)(frame % 2), PX_TEAR);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_CURIOUS:
        draw_eye_arrow(pixels, 8, 8 + y_shift, PX_CYAN);
        put_pixel(pixels, 15, 9 + y_shift, PX_CYAN);
        put_pixel(pixels, 16, 9 + y_shift, PX_CYAN);
        put_pixel(pixels, 20, 4 + y_shift, PX_YELLOW);
        put_pixel(pixels, 20, 6 + y_shift, PX_YELLOW);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_WORRIED:
        draw_eye_sad(pixels, 8, 8 + y_shift);
        draw_eye_sad(pixels, 14, 8 + y_shift);
        put_pixel(pixels, 19, 4 + y_shift, PX_ORANGE);
        put_pixel(pixels, 20, 5 + y_shift, PX_ORANGE);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING:
    case BOARD_WAVE_175C_PET_EXPRESSION_HAPPY:
    case BOARD_WAVE_175C_PET_EXPRESSION_IDLE:
    default:
        draw_eye_arrow(pixels, 8, 8 + y_shift, PX_CYAN);
        put_pixel(pixels, 14, 9 + y_shift, PX_CYAN);
        put_pixel(pixels, 15, 9 + y_shift, PX_CYAN);
        if (expression == BOARD_WAVE_175C_PET_EXPRESSION_HAPPY) {
            put_pixel(pixels, 8, 11 + y_shift, PX_BLUSH);
            put_pixel(pixels, 16, 11 + y_shift, PX_BLUSH);
        }
        break;
    }
}

static int y_shift_for_expression(board_wave_175c_pet_expression_t expression, size_t frame)
{
    switch (expression) {
    case BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING:
        return frame == 1 ? -2 : (frame == 3 ? 1 : -1);
    case BOARD_WAVE_175C_PET_EXPRESSION_IDLE:
    case BOARD_WAVE_175C_PET_EXPRESSION_HAPPY:
        return frame == 1 ? 1 : 0;
    default:
        return 0;
    }
}

const board_wave_175c_pet_sprite_t *board_wave_175c_pet_sprite_for_expression(
    board_wave_175c_pet_expression_t expression)
{
    for (size_t index = 0; index < sizeof(SPRITES) / sizeof(SPRITES[0]); ++index) {
        if (SPRITES[index].expression == expression) {
            return &SPRITES[index];
        }
    }
    return &SPRITES[1];
}

bool board_wave_175c_pet_sprite_rect(
    const board_wave_175c_pet_sprite_t *sprite,
    board_wave_175c_display_rect_t *rect)
{
    if (sprite == NULL || rect == NULL ||
        sprite->width <= 0 || sprite->height <= 0 || sprite->scale <= 0 ||
        sprite->pixel_count != (size_t)sprite->width * (size_t)sprite->height ||
        sprite->frame_count == 0) {
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

bool board_wave_175c_pet_sprite_render(
    const board_wave_175c_pet_sprite_t *sprite,
    size_t frame_index,
    uint16_t *out_pixels,
    size_t out_pixel_count)
{
    if (sprite == NULL || out_pixels == NULL ||
        out_pixel_count < sprite->pixel_count ||
        sprite->width != BOARD_WAVE_175C_PET_SPRITE_WIDTH ||
        sprite->height != BOARD_WAVE_175C_PET_SPRITE_HEIGHT ||
        sprite->frame_count == 0) {
        return false;
    }

    (void)memset(out_pixels, 0, sprite->pixel_count * sizeof(out_pixels[0]));
    const size_t frame = frame_index % sprite->frame_count;
    const int y_shift = y_shift_for_expression(sprite->expression, frame);
    const bool lifted = sprite->expression == BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING && frame == 1;

    draw_cloud(out_pixels, y_shift);
    draw_face_panel(out_pixels, y_shift);
    draw_body(out_pixels, y_shift, lifted);
    draw_expression_face(out_pixels, sprite->expression, frame, y_shift);

    if (sprite->expression == BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING && frame == 1) {
        put_pixel(out_pixels, 7, 22, PX_CYAN_DIM);
        put_pixel(out_pixels, 17, 22, PX_CYAN_DIM);
    }
    return true;
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

#undef PX_CLEAR
#undef PX_OUTLINE
#undef PX_CLOUD
#undef PX_CLOUD_LIGHT
#undef PX_CLOUD_SHADOW
#undef PX_FACE
#undef PX_FACE_DARK
#undef PX_CYAN
#undef PX_CYAN_DIM
#undef PX_BODY
#undef PX_BODY_LIGHT
#undef PX_BLUSH
#undef PX_YELLOW
#undef PX_ORANGE
#undef PX_TEAR
#undef SPRITE_CENTER_X
#undef SPRITE_CENTER_Y
#undef SPRITE_PIXELS
