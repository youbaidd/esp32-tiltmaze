#pragma once

#include <stdbool.h>

// Owns the ball, the maze's lifecycle, and the win/regenerate state machine.
// Called from the physics task; render reads out a snapshot.

typedef enum {
    GAME_PLAYING,
    GAME_WON,
} game_state_t;

void game_init(void);

// Advances the ball by dt seconds under a tilt-driven acceleration (m/s^2,
// screen plane). Handles wall collisions, the win check, and - once the win
// flash has held long enough - generating the next maze automatically.
void game_step(float dt, float tilt_x, float tilt_y);

// Regenerates immediately, abandoning whatever maze is in progress. Wired to
// the PWR button as a manual "stuck, give me a new one" escape hatch.
void game_force_reset(void);

typedef struct {
    float ball_x, ball_y;
    game_state_t state;
    float win_progress;  // 0..1 through the win flash, only meaningful when GAME_WON
} game_snapshot_t;

void game_get_snapshot(game_snapshot_t *out);
