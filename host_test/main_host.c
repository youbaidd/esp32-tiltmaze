// Host-side reproduction of TiltMaze's render pipeline. Compiles the real
// maze.c/game.c/render.c against stub headers (in stubs/) instead of the
// ESP-IDF display/IMU/FreeRTOS APIs, so the exact drawing code that runs on
// the board can be exercised - and checked with ASan/UBSan - without
// flashing hardware. Dumps the assembled frame to a PPM image.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "game.h"

#define SWAP16(v) ((uint16_t)((((v) >> 8) & 0xFF) | (((v) & 0xFF) << 8)))
#define BAND_PIXELS (LCD_H_RES * BAND_ROWS)

static uint16_t s_band_buf[BAND_PIXELS];
static uint16_t s_screen[LCD_V_RES][LCD_H_RES];

uint16_t *display_acquire_band(void)
{
    return s_band_buf;
}

int display_flush_band(int band_index, const uint16_t *buffer)
{
    const int y0 = band_index * BAND_ROWS;
    for (int row = 0; row < BAND_ROWS; row++) {
        memcpy(s_screen[y0 + row], buffer + row * LCD_H_RES, LCD_H_RES * sizeof(uint16_t));
    }
    return 0;
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("fopen");
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", LCD_H_RES, LCD_V_RES);
    for (int y = 0; y < LCD_V_RES; y++) {
        for (int x = 0; x < LCD_H_RES; x++) {
            const uint16_t stored = s_screen[y][x];
            const uint16_t v = SWAP16(stored);  // undo the panel byte-swap
            const int r5 = (v >> 11) & 0x1F;
            const int g6 = (v >> 5) & 0x3F;
            const int b5 = v & 0x1F;
            const uint8_t r8 = (uint8_t)((r5 << 3) | (r5 >> 2));
            const uint8_t g8 = (uint8_t)((g6 << 2) | (g6 >> 4));
            const uint8_t b8 = (uint8_t)((b5 << 3) | (b5 >> 2));
            fputc(r8, f);
            fputc(g8, f);
            fputc(b8, f);
        }
    }
    fclose(f);
}

void render_init(void);
void render_frame(void);

int main(int argc, char **argv)
{
    const char *out_path = (argc > 1) ? argv[1] : "frame.ppm";
    const int frames = (argc > 2) ? atoi(argv[2]) : 1;

    srand(1234);

    game_init();
    render_init();

    for (int i = 0; i < frames; i++) {
        render_frame();
    }

    write_ppm(out_path);
    fprintf(stderr, "wrote %s (%d frame(s) rendered)\n", out_path, frames);
    return 0;
}
