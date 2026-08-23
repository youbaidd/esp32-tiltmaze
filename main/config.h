#pragma once

// Every tunable in TiltMaze lives here, same convention as FluidBox: anything
// you might want to change to alter the feel or difficulty is a constant
// below rather than a number buried in a .c file.

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

#define LCD_H_RES 368
#define LCD_V_RES 448

// The renderer never holds a whole frame. It draws one horizontal band at a
// time into internal SRAM and DMAs it out while drawing the next one, which
// keeps the framebuffer off PSRAM entirely. Same approach as FluidBox.
#define BAND_ROWS 28
#define BAND_COUNT (LCD_V_RES / BAND_ROWS)

#define DISPLAY_BRIGHTNESS 255  // 0..255, written to panel register 0x51

// ---------------------------------------------------------------------------
// Maze grid
// ---------------------------------------------------------------------------

// The maze is a perfect (spanning-tree) grid maze: every cell reachable from
// every other by exactly one path, so a solution always exists.
#define MAZE_COLS 7
#define MAZE_ROWS 9
#define CELL_PX 36

// The panel's corners are physically rounded - FluidBox measured this same
// panel's corner radius at roughly 4.5 mm, which is ~57 px at its 322 ppi
// (see FluidBox's BOX_CORNER_R). A margin at least that wide guarantees the
// maze's own square corners can never land in the rounded-off, invisible
// area: inset a rectangle by the corner radius on every side and its
// corners sit exactly at the centre of each rounding arc, which is always
// inside it. MAZE_COLS/ROWS/CELL_PX above are chosen so this margin clears
// 57 px with room to spare.
#define MAZE_MARGIN_X ((LCD_H_RES - MAZE_COLS * CELL_PX) / 2)
#define MAZE_MARGIN_Y ((LCD_V_RES - MAZE_ROWS * CELL_PX) / 2)

// Drawn thickness of a wall segment. Keep well under CELL_PX - 2*BALL_RADIUS
// or the ball cannot fit through a one-cell-wide corridor.
#define WALL_THICK 5.0f

#define MAZE_MAX_WALLS ((MAZE_COLS + 1) * MAZE_ROWS + (MAZE_ROWS + 1) * MAZE_COLS)

// ---------------------------------------------------------------------------
// Ball physics
// ---------------------------------------------------------------------------

#define BALL_RADIUS 8.0f

// How hard tilt pushes the ball, in px/s^2 per m/s^2 of gravity felt in the
// screen plane (so a dead-flat 90-degree tilt reads as ~9.8 m/s^2 here).
// Not measured on hardware like FluidBox's constants are - this is a
// starting point for feel and is the first thing to retune once you've
// actually rolled the ball around.
#define TILT_GAIN 650.0f

// Ceiling on the acceleration tilt can apply, so a hard knock to the case
// (rather than a slow tilt) cannot fling the ball through geometry it should
// have collided with.
#define MAX_ACCEL 5000.0f

// Per-step velocity multiplier modelling rolling friction. Applied every
// physics step, not every second, so it must stay close to 1.
#define BALL_DAMPING 0.994f

// Hard speed cap. At PHYSICS_HZ, the ball must not be able to cross a whole
// cell in one step or it can tunnel through a wall between two collision
// checks. CELL_PX / (1/PHYSICS_HZ) is the absolute ceiling; this sits well
// under it.
#define MAX_SPEED 900.0f

// How much of the closing speed into a wall survives the bounce. 0 would
// stick the ball dead on contact; 1 would be a perfectly elastic bounce.
#define WALL_RESTITUTION 0.30f

#define PHYSICS_HZ 120

// ---------------------------------------------------------------------------
// Goal
// ---------------------------------------------------------------------------

#define GOAL_RADIUS 10.0f

// How long the "solved" flash holds before a new maze generates.
#define WIN_FLASH_MS 900

// ---------------------------------------------------------------------------
// IMU
// ---------------------------------------------------------------------------

// Time constant of the low pass that separates steady tilt from shake/noise.
#define GRAVITY_LP_HZ 2.0f

#define GRAVITY_MPS2 9.81f

// Maps QMI8658 axes onto screen axes (x right, y down). Reuses the exact
// mapping FluidBox measured on this hardware: flat with the screen up reads
// gravity on IMU -z; tilt right reads +x on IMU +x (screen "up" edge); tilt
// forward reads +y on IMU -y.
#define IMU_MAP_X(ax, ay, az) (ay)
#define IMU_MAP_Y(ax, ay, az) (-(ax))

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

#define DISC_MAX_R 12
