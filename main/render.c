#include "render.h"

#include <math.h>
#include <string.h>

#include "config.h"
#include "display.h"
#include "game.h"
#include "maze.h"

// The panel takes RGB565 with the bytes the other way round from how the CPU
// stores a uint16, so every colour is byte swapped once, up front.
#define SWAP16(v) ((uint16_t)((((v) >> 8) & 0xFF) | (((v) & 0xFF) << 8)))

#define BAND_PIXELS (LCD_H_RES * BAND_ROWS)

static uint16_t s_bg_color;
static uint16_t s_field_color;
static uint16_t s_wall_color;
static uint16_t s_ball_color;
static uint16_t s_ball_highlight;
static uint16_t s_goal_color;
static uint16_t s_win_color;

// Half-width of a filled disc, indexed by radius then by row offset. Same
// trick as FluidBox: turns per-pixel circle maths into a table lookup.
static uint8_t s_disc_span[DISC_MAX_R + 1][2 * DISC_MAX_R + 1];

static inline uint16_t rgb565(int r, int g, int b)
{
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void build_disc_spans(void)
{
    for (int r = 0; r <= DISC_MAX_R; r++) {
        for (int dy = -r; dy <= r; dy++) {
            const float w = sqrtf((float)(r * r - dy * dy));
            s_disc_span[r][dy + r] = (uint8_t)(w + 0.5f);
        }
    }
}

void render_init(void)
{
    build_disc_spans();

    // Measured on hardware: a dim fill like (28,38,64) - technically nonzero,
    // "distinct" from black on paper - rendered as indistinguishable from
    // true black on this panel. Below some real perceptible floor, dim
    // colours just aren't there. So the field is a fully saturated colour
    // rather than a moody dark tint; only the outer border, which is
    // supposed to disappear, stays true black.
    s_bg_color = SWAP16(rgb565(0, 0, 0));
    s_field_color = SWAP16(rgb565(20, 70, 190));
    s_wall_color = SWAP16(rgb565(255, 255, 255));
    s_ball_color = SWAP16(rgb565(255, 110, 20));
    s_ball_highlight = SWAP16(rgb565(255, 215, 180));
    s_goal_color = SWAP16(rgb565(255, 210, 0));
    s_win_color = SWAP16(rgb565(255, 245, 180));
}

static inline void draw_rect(uint16_t *buf, int band_y0, int band_y1, int x0, int y0, int x1,
                              int y1, uint16_t color)
{
    if (x0 < 0) x0 = 0;
    if (y0 < band_y0) y0 = band_y0;
    if (x1 >= LCD_H_RES) x1 = LCD_H_RES - 1;
    if (y1 >= band_y1) y1 = band_y1 - 1;
    if (x0 > x1 || y0 > y1) {
        return;
    }

    for (int y = y0; y <= y1; y++) {
        uint16_t *row = buf + (y - band_y0) * LCD_H_RES;
        for (int x = x0; x <= x1; x++) {
            row[x] = color;
        }
    }
}

static inline void draw_disc(uint16_t *buf, int band_y0, int band_y1, int cx, int cy, int r,
                              uint16_t color)
{
    if (r < 1) r = 1;
    if (r > DISC_MAX_R) r = DISC_MAX_R;

    const uint8_t *spans = s_disc_span[r];

    int dy0 = -r;
    int dy1 = r;
    if (cy + dy0 < band_y0) dy0 = band_y0 - cy;
    if (cy + dy1 >= band_y1) dy1 = band_y1 - 1 - cy;

    for (int dy = dy0; dy <= dy1; dy++) {
        const int hw = spans[dy + r];
        int x0 = cx - hw;
        int x1 = cx + hw;
        if (x0 < 0) x0 = 0;
        if (x1 >= LCD_H_RES) x1 = LCD_H_RES - 1;
        if (x0 > x1) {
            continue;
        }

        uint16_t *row = buf + (cy + dy - band_y0) * LCD_H_RES;
        for (int x = x0; x <= x1; x++) {
            row[x] = color;
        }
    }
}

static void draw_walls(uint16_t *buf, int band_y0, int band_y1)
{
    int count;
    const wall_segment_t *walls = maze_walls(&count);
    const float half = WALL_THICK * 0.5f;

    for (int i = 0; i < count; i++) {
        const wall_segment_t *w = &walls[i];

        int x0, y0, x1, y1;
        if (w->y0 == w->y1) {
            // Horizontal segment.
            x0 = (int)(w->x0 + 0.5f);
            x1 = (int)(w->x1 + 0.5f);
            y0 = (int)(w->y0 - half + 0.5f);
            y1 = (int)(w->y0 + half + 0.5f);
        } else {
            // Vertical segment.
            x0 = (int)(w->x0 - half + 0.5f);
            x1 = (int)(w->x0 + half + 0.5f);
            y0 = (int)(w->y0 + 0.5f);
            y1 = (int)(w->y1 + 0.5f);
        }

        if (y1 < band_y0 || y0 >= band_y1) {
            continue;
        }
        draw_rect(buf, band_y0, band_y1, x0, y0, x1, y1, s_wall_color);
    }
}

// Blends every pixel in the band toward the win colour by `amount` (0..1).
// Only runs during the short "solved" flash, so the extra unswap/swap per
// pixel is not something the steady-state frame rate has to absorb.
static void tint_band(uint16_t *buf, float amount)
{
    if (amount <= 0.0f) {
        return;
    }
    if (amount > 1.0f) {
        amount = 1.0f;
    }

    const uint16_t target = s_win_color;
    const int tr = (target >> 8) & 0xF8;
    const int tg = (target >> 3) & 0xFC;
    const int tb = (target << 3) & 0xF8;

    for (int i = 0; i < BAND_PIXELS; i++) {
        const uint16_t v = SWAP16(buf[i]);
        const int r = (v >> 8) & 0xF8;
        const int g = (v >> 3) & 0xFC;
        const int b = (v << 3) & 0xF8;
        const int nr = (int)(r + (tr - r) * amount);
        const int ng = (int)(g + (tg - g) * amount);
        const int nb = (int)(b + (tb - b) * amount);
        buf[i] = SWAP16(rgb565(nr, ng, nb));
    }
}

void render_frame(void)
{
    float goal_x, goal_y;
    maze_goal_pos(&goal_x, &goal_y);

    game_snapshot_t snap;
    game_get_snapshot(&snap);

    // Triangular ramp instead of a sine ease: ramps 0->1 over the first half
    // of the win flash, 1->0 over the second. Deliberately avoids sinf() -
    // trig was the first thing added in the bisection that correlated with
    // TiltMaze going full-black on hardware, and while the host-side replay
    // in host_test/ cleared the drawing code of any actual bug, there was no
    // reason to keep a computation under active suspicion once the game
    // plays exactly as well without it.
    const float win_amount =
        (snap.win_progress < 0.5f) ? (snap.win_progress * 2.0f) : ((1.0f - snap.win_progress) * 2.0f);

    int goal_r = (int)GOAL_RADIUS;
    if (snap.state == GAME_WON) {
        goal_r = (int)(GOAL_RADIUS * (1.0f + 0.8f * win_amount));
    }

    for (int band = 0; band < BAND_COUNT; band++) {
        const int band_y0 = band * BAND_ROWS;
        const int band_y1 = band_y0 + BAND_ROWS;

        uint16_t *buf = display_acquire_band();

        for (int i = 0; i < BAND_PIXELS; i++) {
            buf[i] = s_bg_color;
        }

        draw_rect(buf, band_y0, band_y1, MAZE_MARGIN_X, MAZE_MARGIN_Y,
                  MAZE_MARGIN_X + MAZE_COLS * CELL_PX - 1, MAZE_MARGIN_Y + MAZE_ROWS * CELL_PX - 1,
                  s_field_color);

        draw_disc(buf, band_y0, band_y1, (int)(goal_x + 0.5f), (int)(goal_y + 0.5f), goal_r,
                  s_goal_color);
        draw_walls(buf, band_y0, band_y1);

        const int bx = (int)(snap.ball_x + 0.5f);
        const int by = (int)(snap.ball_y + 0.5f);
        const int br = (int)BALL_RADIUS;
        draw_disc(buf, band_y0, band_y1, bx, by, br, s_ball_color);
        draw_disc(buf, band_y0, band_y1, bx - br / 3, by - br / 3, br / 2, s_ball_highlight);

        if (snap.state == GAME_WON) {
            tint_band(buf, 0.35f * win_amount);
        }

        display_flush_band(band, buf);
    }
}
