#pragma once

#include <stdint.h>

// Generates and stores a perfect (spanning-tree) maze over a MAZE_COLS x
// MAZE_ROWS grid, and turns it into the flat list of wall segments the
// renderer and the ball's collision both walk.

typedef struct {
    float x0, y0, x1, y1;  // always axis aligned: either x0==x1 or y0==y1
} wall_segment_t;

// Builds a new random maze in place. Safe to call again at any time to
// regenerate (a fresh solve, or the same one - the ball does not care).
void maze_generate(uint32_t seed);

// Returns the current wall list and its length.
const wall_segment_t *maze_walls(int *out_count);

// Screen-space centers of the start and goal cells.
void maze_start_pos(float *x, float *y);
void maze_goal_pos(float *x, float *y);
