#include "board_wave_175c_pet_sprite.h"

#include <stddef.h>
#include <string.h>

#define PX_CLEAR 0x0000
#define PX_OUTLINE 0x114A
#define PX_OUTLINE_SOFT 0x1A50
#define PX_FUR_DARK 0x22B4
#define PX_FUR 0x3BF9
#define PX_FUR_LIGHT 0x6D5C
#define PX_FUR_HILITE 0xB6DE
#define PX_FUR_SHADOW 0x1A91
#define PX_EAR_INNER 0xF6D4
#define PX_FACE 0x85DC
#define PX_FACE_SHADOW 0x5CDB
#define PX_MUZZLE 0xF6D4
#define PX_EYE 0x08C8
#define PX_EYE_DIM 0x85DC
#define PX_MOUTH 0x08C8
#define PX_NOSE 0x0866
#define PX_CHEST 0xF6D4
#define PX_BLUSH 0xFC9F
#define PX_BODY 0x3BF9
#define PX_BODY_LIGHT 0x6D5C
#define PX_BODY_SHADOW 0x22B4
#define PX_TAIL_CORAL 0xFB8D
#define PX_YELLOW 0xFFE0
#define PX_ORANGE 0xFD20
#define PX_TEAR 0x7DDF

#define SPRITE_CENTER_X 233
#define SPRITE_CENTER_Y 182
#define SPRITE_PIXELS (BOARD_WAVE_175C_PET_SPRITE_WIDTH * BOARD_WAVE_175C_PET_SPRITE_HEIGHT)

typedef struct {
    int left_hand_x;
    int left_hand_y;
    int right_hand_x;
    int right_hand_y;
    int left_foot_x;
    int left_foot_y;
    int right_foot_x;
    int right_foot_y;
} limb_pose_t;

static const board_wave_175c_pet_sprite_t SPRITES[] = {
    {BOARD_WAVE_175C_PET_EXPRESSION_SLEEPY, SPRITE_PIXELS, BOARD_WAVE_175C_PET_SPRITE_WIDTH, BOARD_WAVE_175C_PET_SPRITE_HEIGHT, BOARD_WAVE_175C_PET_SPRITE_SCALE, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_IDLE, SPRITE_PIXELS, BOARD_WAVE_175C_PET_SPRITE_WIDTH, BOARD_WAVE_175C_PET_SPRITE_HEIGHT, BOARD_WAVE_175C_PET_SPRITE_SCALE, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_HAPPY, SPRITE_PIXELS, BOARD_WAVE_175C_PET_SPRITE_WIDTH, BOARD_WAVE_175C_PET_SPRITE_HEIGHT, BOARD_WAVE_175C_PET_SPRITE_SCALE, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_LISTENING, SPRITE_PIXELS, BOARD_WAVE_175C_PET_SPRITE_WIDTH, BOARD_WAVE_175C_PET_SPRITE_HEIGHT, BOARD_WAVE_175C_PET_SPRITE_SCALE, SPRITE_CENTER_X, SPRITE_CENTER_Y, 3},
    {BOARD_WAVE_175C_PET_EXPRESSION_THINKING, SPRITE_PIXELS, BOARD_WAVE_175C_PET_SPRITE_WIDTH, BOARD_WAVE_175C_PET_SPRITE_HEIGHT, BOARD_WAVE_175C_PET_SPRITE_SCALE, SPRITE_CENTER_X, SPRITE_CENTER_Y, 3},
    {BOARD_WAVE_175C_PET_EXPRESSION_SPEAKING, SPRITE_PIXELS, BOARD_WAVE_175C_PET_SPRITE_WIDTH, BOARD_WAVE_175C_PET_SPRITE_HEIGHT, BOARD_WAVE_175C_PET_SPRITE_SCALE, SPRITE_CENTER_X, SPRITE_CENTER_Y, 3},
    {BOARD_WAVE_175C_PET_EXPRESSION_SAD, SPRITE_PIXELS, BOARD_WAVE_175C_PET_SPRITE_WIDTH, BOARD_WAVE_175C_PET_SPRITE_HEIGHT, BOARD_WAVE_175C_PET_SPRITE_SCALE, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_SHY, SPRITE_PIXELS, BOARD_WAVE_175C_PET_SPRITE_WIDTH, BOARD_WAVE_175C_PET_SPRITE_HEIGHT, BOARD_WAVE_175C_PET_SPRITE_SCALE, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_FRUSTRATED, SPRITE_PIXELS, BOARD_WAVE_175C_PET_SPRITE_WIDTH, BOARD_WAVE_175C_PET_SPRITE_HEIGHT, BOARD_WAVE_175C_PET_SPRITE_SCALE, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING, SPRITE_PIXELS, BOARD_WAVE_175C_PET_SPRITE_WIDTH, BOARD_WAVE_175C_PET_SPRITE_HEIGHT, BOARD_WAVE_175C_PET_SPRITE_SCALE, SPRITE_CENTER_X, SPRITE_CENTER_Y, 4},
    {BOARD_WAVE_175C_PET_EXPRESSION_LAUGHING, SPRITE_PIXELS, BOARD_WAVE_175C_PET_SPRITE_WIDTH, BOARD_WAVE_175C_PET_SPRITE_HEIGHT, BOARD_WAVE_175C_PET_SPRITE_SCALE, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
    {BOARD_WAVE_175C_PET_EXPRESSION_CRYING, SPRITE_PIXELS, BOARD_WAVE_175C_PET_SPRITE_WIDTH, BOARD_WAVE_175C_PET_SPRITE_HEIGHT, BOARD_WAVE_175C_PET_SPRITE_SCALE, SPRITE_CENTER_X, SPRITE_CENTER_Y, 3},
    {BOARD_WAVE_175C_PET_EXPRESSION_CURIOUS, SPRITE_PIXELS, BOARD_WAVE_175C_PET_SPRITE_WIDTH, BOARD_WAVE_175C_PET_SPRITE_HEIGHT, BOARD_WAVE_175C_PET_SPRITE_SCALE, SPRITE_CENTER_X, SPRITE_CENTER_Y, 3},
    {BOARD_WAVE_175C_PET_EXPRESSION_WORRIED, SPRITE_PIXELS, BOARD_WAVE_175C_PET_SPRITE_WIDTH, BOARD_WAVE_175C_PET_SPRITE_HEIGHT, BOARD_WAVE_175C_PET_SPRITE_SCALE, SPRITE_CENTER_X, SPRITE_CENTER_Y, 2},
};

static int iabs(int value)
{
    return value < 0 ? -value : value;
}

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

static void fill_rounded_rect(uint16_t *pixels, int x, int y, int width, int height, int radius, uint16_t color)
{
    if (width <= 0 || height <= 0 || radius < 0) {
        return;
    }
    fill_rect(pixels, x + radius, y, width - 2 * radius, height, color);
    fill_rect(pixels, x, y + radius, width, height - 2 * radius, color);
    fill_ellipse(pixels, x + radius, y + radius, radius, radius, color);
    fill_ellipse(pixels, x + width - radius - 1, y + radius, radius, radius, color);
    fill_ellipse(pixels, x + radius, y + height - radius - 1, radius, radius, color);
    fill_ellipse(pixels, x + width - radius - 1, y + height - radius - 1, radius, radius, color);
}

static void fill_triangle(uint16_t *pixels, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    int min_x = x0 < x1 ? x0 : x1;
    min_x = min_x < x2 ? min_x : x2;
    int max_x = x0 > x1 ? x0 : x1;
    max_x = max_x > x2 ? max_x : x2;
    int min_y = y0 < y1 ? y0 : y1;
    min_y = min_y < y2 ? min_y : y2;
    int max_y = y0 > y1 ? y0 : y1;
    max_y = max_y > y2 ? max_y : y2;

    const int area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (area == 0) {
        return;
    }
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const int w0 = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
            const int w1 = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
            const int w2 = (x0 - x2) * (y - y2) - (y0 - y2) * (x - x2);
            if ((area > 0 && w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                (area < 0 && w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                put_pixel(pixels, x, y, color);
            }
        }
    }
}

static void draw_thick_line(uint16_t *pixels, int x0, int y0, int x1, int y1, int thickness, uint16_t color)
{
    const int dx = x1 - x0;
    const int dy = y1 - y0;
    const int steps = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
    if (steps <= 0) {
        fill_ellipse(pixels, x0, y0, thickness, thickness, color);
        return;
    }
    for (int step = 0; step <= steps; ++step) {
        const int x = x0 + (dx * step) / steps;
        const int y = y0 + (dy * step) / steps;
        fill_ellipse(pixels, x, y, thickness, thickness, color);
    }
}

static void draw_four_point_star(
    uint16_t *pixels,
    int center_x,
    int center_y,
    int radius,
    uint16_t color)
{
    const int shoulder = radius / 3;
    fill_triangle(pixels,
                  center_x, center_y - radius,
                  center_x - shoulder, center_y - shoulder,
                  center_x + shoulder, center_y - shoulder,
                  color);
    fill_triangle(pixels,
                  center_x + radius, center_y,
                  center_x + shoulder, center_y - shoulder,
                  center_x + shoulder, center_y + shoulder,
                  color);
    fill_triangle(pixels,
                  center_x, center_y + radius,
                  center_x - shoulder, center_y + shoulder,
                  center_x + shoulder, center_y + shoulder,
                  color);
    fill_triangle(pixels,
                  center_x - radius, center_y,
                  center_x - shoulder, center_y - shoulder,
                  center_x - shoulder, center_y + shoulder,
                  color);
    fill_ellipse(pixels, center_x, center_y, shoulder + 1, shoulder + 1, color);
}

static int y_shift_for_expression(board_wave_175c_pet_expression_t expression, size_t frame)
{
    switch (expression) {
    case BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING:
        return frame == 1 ? -10 : (frame == 3 ? 5 : -3);
    case BOARD_WAVE_175C_PET_EXPRESSION_LAUGHING:
        return frame == 1 ? -2 : 0;
    case BOARD_WAVE_175C_PET_EXPRESSION_SLEEPY:
    case BOARD_WAVE_175C_PET_EXPRESSION_SAD:
        return frame == 1 ? 3 : 1;
    case BOARD_WAVE_175C_PET_EXPRESSION_WORRIED:
        return frame == 1 ? 1 : 0;
    case BOARD_WAVE_175C_PET_EXPRESSION_IDLE:
    case BOARD_WAVE_175C_PET_EXPRESSION_HAPPY:
        return frame == 1 ? 2 : 0;
    default:
        return 0;
    }
}

static limb_pose_t limb_pose_for_expression(board_wave_175c_pet_expression_t expression, size_t frame)
{
    limb_pose_t pose = {0};
    switch (expression) {
    case BOARD_WAVE_175C_PET_EXPRESSION_LISTENING:
        pose.left_hand_x = frame == 1 ? 3 : 1;
        pose.left_hand_y = frame == 1 ? -8 : -5;
        pose.right_hand_x = frame == 2 ? -3 : -1;
        pose.right_hand_y = frame == 2 ? -8 : -5;
        pose.left_foot_y = frame == 1 ? 1 : 0;
        pose.right_foot_y = frame == 2 ? 1 : 0;
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_THINKING:
        pose.left_hand_x = 11;
        pose.left_hand_y = -20 + (int)(frame % 2);
        pose.right_hand_x = -2;
        pose.right_hand_y = -1;
        pose.left_foot_x = -1;
        pose.right_foot_x = 1;
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_SPEAKING:
        pose.left_hand_x = frame == 2 ? -4 : -2;
        pose.left_hand_y = frame == 2 ? -4 : -2;
        pose.right_hand_x = frame == 0 ? -9 : (frame == 1 ? -13 : -7);
        pose.right_hand_y = frame == 0 ? -12 : (frame == 1 ? -18 : -9);
        pose.left_foot_y = frame == 2 ? 1 : 0;
        pose.right_foot_y = frame == 1 ? -1 : 0;
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_HAPPY:
        pose.left_hand_x = frame == 1 ? -5 : -3;
        pose.left_hand_y = frame == 1 ? -9 : -6;
        pose.right_hand_x = frame == 1 ? 5 : 3;
        pose.right_hand_y = frame == 1 ? -9 : -6;
        pose.left_foot_x = -1;
        pose.right_foot_x = 1;
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING:
        pose.left_hand_x = frame == 1 ? -7 : -3;
        pose.left_hand_y = frame == 1 ? -10 : -4;
        pose.right_hand_x = frame == 1 ? 7 : 3;
        pose.right_hand_y = frame == 1 ? -10 : -4;
        pose.left_foot_x = frame == 1 ? -3 : (frame == 3 ? 2 : 0);
        pose.right_foot_x = frame == 1 ? 3 : (frame == 3 ? -2 : 0);
        pose.left_foot_y = frame == 1 ? -6 : (frame == 3 ? 3 : 0);
        pose.right_foot_y = frame == 1 ? -6 : (frame == 3 ? 3 : 0);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_LAUGHING:
        pose.left_hand_x = frame == 1 ? -8 : -5;
        pose.left_hand_y = frame == 1 ? -12 : -8;
        pose.right_hand_x = frame == 1 ? 8 : 5;
        pose.right_hand_y = frame == 1 ? -12 : -8;
        pose.left_foot_x = frame == 1 ? -2 : 0;
        pose.right_foot_x = frame == 1 ? 2 : 0;
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_SHY:
        pose.left_hand_x = 12;
        pose.left_hand_y = -28 + (frame == 1 ? -2 : 0);
        pose.right_hand_x = -12;
        pose.right_hand_y = -27 + (frame == 1 ? -2 : 0);
        pose.left_foot_x = -2;
        pose.right_foot_x = 2;
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_SLEEPY:
        pose.left_hand_x = -1;
        pose.left_hand_y = 4;
        pose.right_hand_x = 1;
        pose.right_hand_y = 4;
        pose.left_foot_y = 2;
        pose.right_foot_y = 2;
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_SAD:
    case BOARD_WAVE_175C_PET_EXPRESSION_CRYING:
        pose.left_hand_x = -2;
        pose.left_hand_y = 6 + (int)(frame % 2);
        pose.right_hand_x = 2;
        pose.right_hand_y = 6 + (int)(frame % 2);
        pose.left_foot_x = 1;
        pose.right_foot_x = -1;
        pose.left_foot_y = 2;
        pose.right_foot_y = 2;
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_FRUSTRATED:
        pose.left_hand_x = frame == 1 ? -9 : -6;
        pose.left_hand_y = frame == 1 ? -6 : -3;
        pose.right_hand_x = frame == 1 ? 9 : 6;
        pose.right_hand_y = frame == 1 ? -6 : -3;
        pose.left_foot_x = frame == 1 ? -1 : 1;
        pose.right_foot_x = frame == 1 ? 1 : -1;
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_CURIOUS:
        pose.left_hand_x = frame == 0 ? 0 : 3;
        pose.left_hand_y = frame == 2 ? -7 : -4;
        pose.right_hand_x = frame == 1 ? -6 : -3;
        pose.right_hand_y = frame == 1 ? -12 : -7;
        pose.left_foot_x = frame == 2 ? -2 : 0;
        pose.right_foot_x = frame == 1 ? 2 : 0;
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_WORRIED:
        pose.left_hand_x = 5;
        pose.left_hand_y = -14 + (frame == 1 ? 2 : 0);
        pose.right_hand_x = frame == 1 ? 3 : 0;
        pose.right_hand_y = frame == 1 ? 3 : 1;
        pose.left_foot_x = -1;
        pose.right_foot_x = 1;
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_IDLE:
    default:
        pose.left_hand_y = frame == 1 ? 1 : 0;
        pose.right_hand_y = frame == 1 ? -1 : 0;
        pose.left_foot_y = frame == 1 ? 1 : 0;
        pose.right_foot_y = frame == 1 ? 0 : 1;
        break;
    }
    return pose;
}

static void draw_tail(uint16_t *pixels, int y_shift, bool raised)
{
    const int lift = raised ? -4 : 0;
    const int root_y = 116 + y_shift;
    const int bend_y = 120 + y_shift + lift;
    const int rise_y = 107 + y_shift + lift;
    const int tip_y = 84 + y_shift + lift;

    draw_thick_line(pixels, 105, root_y, 126, bend_y, 8, PX_OUTLINE_SOFT);
    draw_thick_line(pixels, 126, bend_y, 143, rise_y, 8, PX_OUTLINE_SOFT);
    draw_thick_line(pixels, 143, rise_y, 146, 94 + y_shift + lift, 8, PX_OUTLINE_SOFT);

    draw_thick_line(pixels, 105, root_y, 126, bend_y, 6, PX_OUTLINE);
    draw_thick_line(pixels, 126, bend_y, 143, rise_y, 6, PX_OUTLINE);
    draw_thick_line(pixels, 143, rise_y, 146, 94 + y_shift + lift, 6, PX_OUTLINE);

    draw_thick_line(pixels, 107, root_y, 126, bend_y, 4, PX_FUR_LIGHT);
    draw_thick_line(pixels, 126, bend_y, 143, rise_y, 4, PX_FUR_LIGHT);
    draw_thick_line(pixels, 143, rise_y, 146, 94 + y_shift + lift, 4, PX_FUR_LIGHT);

    draw_thick_line(pixels, 117, 115 + y_shift, 115, 123 + y_shift, 3, PX_FUR_DARK);
    draw_thick_line(pixels, 130, 114 + y_shift + lift, 136, 120 + y_shift + lift, 3, PX_FUR);
    draw_thick_line(pixels, 138, 103 + y_shift + lift, 147, 105 + y_shift + lift, 3, PX_FUR_DARK);
    draw_thick_line(pixels, 141, 94 + y_shift + lift, 149, 95 + y_shift + lift, 3, PX_FUR);

    draw_four_point_star(pixels, 145, tip_y, 12, PX_OUTLINE);
    draw_four_point_star(pixels, 145, tip_y, 9, PX_TAIL_CORAL);
}

static void draw_ears(uint16_t *pixels, int y_shift, int ear_tilt)
{
    const int upright_tip_x = 49 + ear_tilt;
    const int folded_shift = ear_tilt / 2;

    fill_triangle(pixels, 32, 58 + y_shift, upright_tip_x, 12 + y_shift, 70, 54 + y_shift, PX_OUTLINE_SOFT);
    fill_triangle(pixels, 35, 57 + y_shift, upright_tip_x, 15 + y_shift, 68, 53 + y_shift, PX_OUTLINE);
    fill_triangle(pixels, 40, 54 + y_shift, upright_tip_x + 1, 19 + y_shift, 64, 52 + y_shift, PX_FUR);
    fill_triangle(pixels, 45, 51 + y_shift, upright_tip_x + 2, 26 + y_shift, 59, 51 + y_shift, PX_EAR_INNER);
    fill_triangle(pixels, 44, 39 + y_shift, upright_tip_x, 16 + y_shift, 56, 39 + y_shift, PX_FUR_LIGHT);

    fill_triangle(pixels, 91, 51 + y_shift, 116 - folded_shift, 25 + y_shift, 134, 54 + y_shift, PX_OUTLINE_SOFT);
    fill_ellipse(pixels, 121 - folded_shift, 39 + y_shift, 15, 12, PX_OUTLINE_SOFT);
    fill_triangle(pixels, 94, 50 + y_shift, 116 - folded_shift, 28 + y_shift, 131, 53 + y_shift, PX_OUTLINE);
    fill_ellipse(pixels, 120 - folded_shift, 39 + y_shift, 12, 9, PX_OUTLINE);
    fill_triangle(pixels, 98, 49 + y_shift, 115 - folded_shift, 31 + y_shift, 128, 50 + y_shift, PX_FUR_DARK);
    fill_ellipse(pixels, 119 - folded_shift, 39 + y_shift, 9, 6, PX_FUR_DARK);
    fill_triangle(pixels, 103, 47 + y_shift, 115 - folded_shift, 34 + y_shift, 125, 48 + y_shift, PX_TAIL_CORAL);
    fill_ellipse(pixels, 118 - folded_shift, 40 + y_shift, 6, 4, PX_TAIL_CORAL);
}

static void draw_head(uint16_t *pixels, int y_shift, int ear_tilt)
{
    draw_ears(pixels, y_shift, ear_tilt);
    fill_ellipse(pixels, 80, 64 + y_shift, 51, 44, PX_OUTLINE_SOFT);
    fill_ellipse(pixels, 80, 64 + y_shift, 48, 41, PX_OUTLINE);
    fill_ellipse(pixels, 80, 63 + y_shift, 44, 37, PX_FUR);
    fill_ellipse(pixels, 57, 47 + y_shift, 20, 14, PX_FUR_LIGHT);
    fill_ellipse(pixels, 104, 50 + y_shift, 23, 16, PX_FUR_DARK);
    fill_ellipse(pixels, 112, 72 + y_shift, 13, 16, PX_FUR_SHADOW);
    fill_ellipse(pixels, 49, 79 + y_shift, 8, 7, PX_FACE);
    fill_ellipse(pixels, 111, 79 + y_shift, 8, 7, PX_FACE);
    fill_ellipse(pixels, 80, 81 + y_shift, 23, 15, PX_FACE_SHADOW);
    fill_ellipse(pixels, 80, 82 + y_shift, 20, 13, PX_MUZZLE);
    fill_ellipse(pixels, 72, 77 + y_shift, 7, 6, PX_FUR_HILITE);
}

static void draw_body(uint16_t *pixels, int y_shift, bool lifted, limb_pose_t pose)
{
    const int lift = lifted ? -7 : 0;
    draw_tail(pixels, y_shift + lift, lifted);
    fill_ellipse(pixels, 80, 154, 31, 4, PX_OUTLINE_SOFT);
    fill_ellipse(pixels, 80, 116 + y_shift + lift, 29, 31, PX_OUTLINE);
    fill_ellipse(pixels, 80, 115 + y_shift + lift, 25, 28, PX_BODY);
    fill_ellipse(pixels, 96, 124 + y_shift + lift, 13, 17, PX_BODY_SHADOW);
    fill_ellipse(pixels, 67, 106 + y_shift + lift, 12, 10, PX_BODY_LIGHT);
    fill_ellipse(pixels, 80, 122 + y_shift + lift, 14, 20, PX_CHEST);

    const int left_hand_x = 42 + pose.left_hand_x;
    const int left_hand_y = 130 + y_shift + lift + pose.left_hand_y;
    const int right_hand_x = 118 + pose.right_hand_x;
    const int right_hand_y = 130 + y_shift + lift + pose.right_hand_y;
    draw_thick_line(pixels, 53, 111 + y_shift + lift, left_hand_x - 2, left_hand_y, 7, PX_OUTLINE);
    draw_thick_line(pixels, 55, 111 + y_shift + lift, left_hand_x + 1, left_hand_y - 2, 4, PX_BODY);
    draw_thick_line(pixels, 107, 111 + y_shift + lift, right_hand_x + 2, right_hand_y, 7, PX_OUTLINE);
    draw_thick_line(pixels, 105, 111 + y_shift + lift, right_hand_x - 1, right_hand_y - 2, 4, PX_BODY);
    fill_ellipse(pixels, left_hand_x, left_hand_y, 7, 6, PX_BODY_LIGHT);
    fill_ellipse(pixels, right_hand_x, right_hand_y, 7, 6, PX_BODY_LIGHT);

    const int left_foot_x = 65 + pose.left_foot_x;
    const int left_foot_y = 143 + y_shift + lift + pose.left_foot_y;
    const int right_foot_x = 95 + pose.right_foot_x;
    const int right_foot_y = 143 + y_shift + lift + pose.right_foot_y;
    fill_ellipse(pixels, left_foot_x, left_foot_y, 12, 7, PX_OUTLINE);
    fill_ellipse(pixels, right_foot_x, right_foot_y, 12, 7, PX_OUTLINE);
    fill_ellipse(pixels, left_foot_x, left_foot_y - 2, 8, 4, PX_BODY_LIGHT);
    fill_ellipse(pixels, right_foot_x, right_foot_y - 2, 8, 4, PX_BODY_LIGHT);

    draw_thick_line(pixels, 70, 119 + y_shift + lift, 80, 123 + y_shift + lift, 1, PX_FACE_SHADOW);
    draw_thick_line(pixels, 80, 123 + y_shift + lift, 90, 119 + y_shift + lift, 1, PX_FACE_SHADOW);
}

static void draw_fox_muzzle(uint16_t *pixels, int y_shift)
{
    fill_ellipse(pixels, 80, 78 + y_shift, 5, 4, PX_NOSE);
    fill_ellipse(pixels, 78, 77 + y_shift, 1, 1, PX_FUR_HILITE);
    draw_thick_line(pixels, 80, 81 + y_shift, 80, 87 + y_shift, 1, PX_MOUTH);
    draw_thick_line(pixels, 80, 87 + y_shift, 73, 91 + y_shift, 1, PX_MOUTH);
    draw_thick_line(pixels, 80, 87 + y_shift, 87, 91 + y_shift, 1, PX_MOUTH);
    fill_ellipse(pixels, 62, 81 + y_shift, 1, 1, PX_FACE_SHADOW);
    fill_ellipse(pixels, 66, 85 + y_shift, 1, 1, PX_FACE_SHADOW);
    fill_ellipse(pixels, 98, 81 + y_shift, 1, 1, PX_FACE_SHADOW);
    fill_ellipse(pixels, 94, 85 + y_shift, 1, 1, PX_FACE_SHADOW);
}

static void draw_open_eye(uint16_t *pixels, int x, int y, uint16_t color)
{
    fill_ellipse(pixels, x, y, 8, 10, PX_MOUTH);
    fill_ellipse(pixels, x, y, 5, 7, color);
    fill_ellipse(pixels, x - 2, y - 3, 2, 2, PX_FUR_HILITE);
}

static void draw_open_eye_offset(uint16_t *pixels, int x, int y, int look_x, int look_y, uint16_t color)
{
    fill_ellipse(pixels, x, y, 8, 10, PX_MOUTH);
    fill_ellipse(pixels, x + look_x, y + look_y, 5, 7, color);
    fill_ellipse(pixels, x + look_x - 2, y + look_y - 3, 2, 2, PX_FUR_HILITE);
}

static void draw_flat_eye(uint16_t *pixels, int x, int y, uint16_t color)
{
    fill_rounded_rect(pixels, x - 8, y - 2, 16, 5, 2, color);
}

static void draw_happy_eye(uint16_t *pixels, int x, int y)
{
    draw_thick_line(pixels, x - 8, y + 4, x, y - 2, 2, PX_EYE);
    draw_thick_line(pixels, x, y - 2, x + 8, y + 4, 2, PX_EYE);
}

static void draw_sad_eye(uint16_t *pixels, int x, int y)
{
    draw_thick_line(pixels, x - 8, y - 2, x + 8, y + 4, 2, PX_EYE);
}

static void draw_mouth(uint16_t *pixels, int cx, int cy, int width, int mood)
{
    if (mood > 0) {
        draw_thick_line(pixels, cx - width / 2, cy, cx, cy + 5, 1, PX_MOUTH);
        draw_thick_line(pixels, cx, cy + 5, cx + width / 2, cy, 1, PX_MOUTH);
    } else if (mood < 0) {
        draw_thick_line(pixels, cx - width / 2, cy + 5, cx, cy, 1, PX_MOUTH);
        draw_thick_line(pixels, cx, cy, cx + width / 2, cy + 5, 1, PX_MOUTH);
    } else {
        fill_rounded_rect(pixels, cx - width / 2, cy, width, 4, 2, PX_MOUTH);
    }
}

static void draw_listening_waves(uint16_t *pixels, int y_shift, size_t frame)
{
    const int pulse = (int)(frame % 3) * 4;
    draw_thick_line(pixels, 28 - pulse, 73 + y_shift, 18 - pulse, 63 + y_shift, 1, PX_EYE_DIM);
    draw_thick_line(pixels, 28 - pulse, 83 + y_shift, 18 - pulse, 93 + y_shift, 1, PX_EYE_DIM);
    draw_thick_line(pixels, 132 + pulse, 73 + y_shift, 142 + pulse, 63 + y_shift, 1, PX_EYE_DIM);
    draw_thick_line(pixels, 132 + pulse, 83 + y_shift, 142 + pulse, 93 + y_shift, 1, PX_EYE_DIM);
    if (frame == 2) {
        draw_thick_line(pixels, 23, 78 + y_shift, 13, 78 + y_shift, 1, PX_EYE);
        draw_thick_line(pixels, 137, 78 + y_shift, 147, 78 + y_shift, 1, PX_EYE);
    }
}

static void draw_expression(
    uint16_t *pixels,
    board_wave_175c_pet_expression_t expression,
    size_t frame,
    int y_shift)
{
    const int left_eye_x = 63;
    const int right_eye_x = 97;
    const int eye_y = 66 + y_shift;
    draw_fox_muzzle(pixels, y_shift);
    switch (expression) {
    case BOARD_WAVE_175C_PET_EXPRESSION_SLEEPY:
        draw_flat_eye(pixels, left_eye_x, eye_y, PX_EYE_DIM);
        draw_flat_eye(pixels, right_eye_x, eye_y, PX_EYE_DIM);
        draw_mouth(pixels, 80, 88 + y_shift, 9, 0);
        draw_thick_line(pixels, 119, 34 + y_shift - (int)frame * 3, 128, 27 + y_shift - (int)frame * 3, 1, frame == 0 ? PX_EYE_DIM : PX_EYE);
        draw_thick_line(pixels, 127, 26 + y_shift - (int)frame * 3, 136, 26 + y_shift - (int)frame * 3, 1, PX_EYE_DIM);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_LISTENING:
        draw_open_eye_offset(pixels, left_eye_x, eye_y, 0, frame == 2 ? -1 : 0, PX_EYE);
        draw_open_eye_offset(pixels, right_eye_x, eye_y, 0, frame == 2 ? -1 : 0, PX_EYE);
        draw_mouth(pixels, 80, 88 + y_shift, 8, 0);
        draw_listening_waves(pixels, y_shift, frame);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_THINKING:
        draw_open_eye_offset(pixels, left_eye_x, eye_y, (int)frame - 1, 0, PX_EYE_DIM);
        draw_open_eye_offset(pixels, right_eye_x, eye_y - 2, (int)frame - 1, 0, PX_EYE);
        draw_mouth(pixels, 80, 88 + y_shift, 9, 0);
        fill_ellipse(pixels, 116, 38 + y_shift, frame == 0 ? 5 : 3, frame == 0 ? 5 : 3, frame == 0 ? PX_YELLOW : PX_EYE_DIM);
        fill_ellipse(pixels, 128, 36 + y_shift, frame == 1 ? 5 : 3, frame == 1 ? 5 : 3, frame == 1 ? PX_YELLOW : PX_EYE_DIM);
        fill_ellipse(pixels, 140, 39 + y_shift, frame == 2 ? 5 : 3, frame == 2 ? 5 : 3, frame == 2 ? PX_YELLOW : PX_EYE_DIM);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_SPEAKING:
        draw_open_eye(pixels, left_eye_x, eye_y, PX_EYE);
        draw_open_eye(pixels, right_eye_x, eye_y, PX_EYE);
        fill_ellipse(pixels, 80, 88 + y_shift, frame == 0 ? 5 : (frame == 1 ? 8 : 11), frame == 0 ? 4 : (frame == 1 ? 5 : 7), PX_MOUTH);
        fill_ellipse(pixels, 80, 87 + y_shift, frame == 0 ? 2 : (frame == 1 ? 4 : 6), frame == 0 ? 1 : (frame == 1 ? 2 : 3), PX_EYE_DIM);
        fill_ellipse(pixels, 52, 82 + y_shift, frame == 2 ? 7 : 5, 2, PX_BLUSH);
        fill_ellipse(pixels, 108, 82 + y_shift, frame == 2 ? 7 : 5, 2, PX_BLUSH);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_SAD:
        draw_sad_eye(pixels, left_eye_x, eye_y);
        draw_sad_eye(pixels, right_eye_x, eye_y);
        fill_ellipse(pixels, 58, 77 + y_shift, 2, 3, frame == 1 ? PX_TEAR : PX_EYE_DIM);
        draw_mouth(pixels, 80, 89 + y_shift, frame == 1 ? 12 : 16, -1);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_SHY:
        draw_flat_eye(pixels, left_eye_x, eye_y + 1, PX_EYE);
        draw_open_eye_offset(pixels, right_eye_x, eye_y, -2, 1, PX_EYE);
        fill_ellipse(pixels, 52, 82 + y_shift, frame == 0 ? 11 : 14, frame == 0 ? 5 : 6, PX_BLUSH);
        fill_ellipse(pixels, 108, 82 + y_shift, frame == 0 ? 11 : 14, frame == 0 ? 5 : 6, PX_BLUSH);
        fill_ellipse(pixels, 48, 93 + y_shift, 8, 6, PX_BODY_LIGHT);
        draw_mouth(pixels, 80, 89 + y_shift, 8, 1);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_FRUSTRATED:
        draw_thick_line(pixels, 55 + (int)frame * 2, 60 + y_shift, 70 - (int)frame, 67 + y_shift, 2, PX_ORANGE);
        draw_thick_line(pixels, 105 - (int)frame * 2, 60 + y_shift, 90 + (int)frame, 67 + y_shift, 2, PX_ORANGE);
        draw_mouth(pixels, 80, 90 + y_shift, 18, -1);
        draw_thick_line(pixels, 119 + (int)frame * 2, 33 + y_shift - (int)frame * 2, 130 + (int)frame * 2, 24 + y_shift - (int)frame, 2, PX_ORANGE);
        draw_thick_line(pixels, 126 + (int)frame * 2, 35 + y_shift - (int)frame * 2, 136 + (int)frame * 2, 35 + y_shift - (int)frame * 2, 1, PX_ORANGE);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_LAUGHING:
        draw_happy_eye(pixels, left_eye_x, eye_y);
        draw_happy_eye(pixels, right_eye_x, eye_y);
        if (frame == 1) {
            fill_ellipse(pixels, 80, 88 + y_shift, 12, 8, PX_MOUTH);
            fill_ellipse(pixels, 80, 87 + y_shift, 7, 4, PX_EYE_DIM);
        } else {
            draw_mouth(pixels, 80, 88 + y_shift, 22, 1);
        }
        fill_ellipse(pixels, 51, 80 + y_shift, 9, 3, PX_BLUSH);
        fill_ellipse(pixels, 109, 80 + y_shift, 9, 3, PX_BLUSH);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_CRYING:
        draw_sad_eye(pixels, left_eye_x, eye_y);
        draw_sad_eye(pixels, right_eye_x, eye_y);
        fill_ellipse(pixels, 58, 78 + y_shift + (int)frame * 3, 3, 5 + (int)frame, PX_TEAR);
        fill_ellipse(pixels, 102, 78 + y_shift + (int)frame * 3, 3, 5 + (int)frame, PX_TEAR);
        if (frame == 2) {
            fill_ellipse(pixels, 58, 98 + y_shift, 4, 2, PX_TEAR);
            fill_ellipse(pixels, 102, 98 + y_shift, 4, 2, PX_TEAR);
        }
        draw_mouth(pixels, 80, 91 + y_shift, 14, -1);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_CURIOUS:
        draw_open_eye_offset(pixels, left_eye_x, eye_y, (int)frame - 1, 0, PX_EYE);
        fill_ellipse(pixels, right_eye_x + (int)frame - 1, eye_y, frame == 1 ? 7 : 6, frame == 1 ? 7 : 6, PX_EYE);
        draw_mouth(pixels, 80, 88 + y_shift, 8, 0);
        draw_thick_line(pixels, 118 + (int)frame * 3, 31 + y_shift, 118 + (int)frame * 3, 41 + y_shift, 2, PX_YELLOW);
        fill_ellipse(pixels, 118 + (int)frame * 3, 50 + y_shift, 3, 3, PX_YELLOW);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_WORRIED:
        draw_sad_eye(pixels, left_eye_x, eye_y);
        draw_open_eye_offset(pixels, right_eye_x, eye_y, frame == 1 ? -1 : 0, 0, PX_EYE_DIM);
        draw_mouth(pixels, 80, 89 + y_shift, frame == 1 ? 10 : 12, -1);
        fill_ellipse(pixels, 119, 38 + y_shift + (int)frame * 2, 4, 9, frame == 1 ? PX_YELLOW : PX_ORANGE);
        fill_ellipse(pixels, 119, 52 + y_shift + (int)frame * 2, 2, 2, PX_ORANGE);
        break;
    case BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING:
    case BOARD_WAVE_175C_PET_EXPRESSION_HAPPY:
    case BOARD_WAVE_175C_PET_EXPRESSION_IDLE:
    default:
        if (expression == BOARD_WAVE_175C_PET_EXPRESSION_HAPPY) {
            draw_happy_eye(pixels, left_eye_x, eye_y);
            draw_happy_eye(pixels, right_eye_x, eye_y);
            fill_ellipse(pixels, 52, 82 + y_shift, 8, 3, PX_BLUSH);
            fill_ellipse(pixels, 108, 82 + y_shift, 8, 3, PX_BLUSH);
            draw_mouth(pixels, 80, 88 + y_shift, 17, 1);
        } else {
            if (expression == BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING) {
                draw_happy_eye(pixels, left_eye_x, eye_y);
                draw_happy_eye(pixels, right_eye_x, eye_y);
                draw_mouth(pixels, 80, 88 + y_shift, 14, 1);
            } else {
                draw_open_eye(pixels, left_eye_x, eye_y, PX_EYE);
                draw_open_eye(pixels, right_eye_x, eye_y, PX_EYE);
                draw_mouth(pixels, 80, 89 + y_shift, 12, 1);
            }
        }
        break;
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
    const limb_pose_t pose = limb_pose_for_expression(sprite->expression, frame);
    int ear_tilt = 0;
    if (sprite->expression == BOARD_WAVE_175C_PET_EXPRESSION_LISTENING) {
        ear_tilt = frame == 1 ? 5 : (frame == 2 ? -3 : 0);
    } else if (sprite->expression == BOARD_WAVE_175C_PET_EXPRESSION_CURIOUS) {
        ear_tilt = frame == 1 ? 4 : (frame == 2 ? -4 : 0);
    } else if (sprite->expression == BOARD_WAVE_175C_PET_EXPRESSION_FRUSTRATED) {
        ear_tilt = frame == 1 ? -3 : 0;
    } else if (sprite->expression == BOARD_WAVE_175C_PET_EXPRESSION_SAD ||
               sprite->expression == BOARD_WAVE_175C_PET_EXPRESSION_WORRIED) {
        ear_tilt = frame == 1 ? -4 : -2;
    }

    draw_body(out_pixels, y_shift, lifted, pose);
    draw_head(out_pixels, y_shift, ear_tilt);
    draw_expression(out_pixels, sprite->expression, frame, y_shift);

    if (sprite->expression == BOARD_WAVE_175C_PET_EXPRESSION_BOUNCING && frame == 1) {
        fill_ellipse(out_pixels, 60, 153, 10, 2, PX_EYE_DIM);
        fill_ellipse(out_pixels, 100, 153, 10, 2, PX_EYE_DIM);
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
#undef PX_OUTLINE_SOFT
#undef PX_FUR_DARK
#undef PX_FUR
#undef PX_FUR_LIGHT
#undef PX_FUR_HILITE
#undef PX_FUR_SHADOW
#undef PX_EAR_INNER
#undef PX_FACE
#undef PX_FACE_SHADOW
#undef PX_MUZZLE
#undef PX_EYE
#undef PX_EYE_DIM
#undef PX_MOUTH
#undef PX_NOSE
#undef PX_CHEST
#undef PX_BLUSH
#undef PX_BODY
#undef PX_BODY_LIGHT
#undef PX_BODY_SHADOW
#undef PX_TAIL_CORAL
#undef PX_YELLOW
#undef PX_ORANGE
#undef PX_TEAR
#undef SPRITE_CENTER_X
#undef SPRITE_CENTER_Y
#undef SPRITE_PIXELS
