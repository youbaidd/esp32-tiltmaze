#include "maze.h"

#include "config.h"

#define WALL_N 0x01
#define WALL_E 0x02
#define WALL_S 0x04
#define WALL_W 0x08

typedef struct {
    uint8_t walls;    // bits still standing, WALL_N/E/S/W
    uint8_t visited;
} cell_t;

typedef struct {
    int8_t r, c;
} stack_cell_t;

static cell_t s_grid[MAZE_ROWS][MAZE_COLS];
static wall_segment_t s_walls[MAZE_MAX_WALLS];
static int s_wall_count;

static uint32_t s_rng;

// Small, fast, and deterministic from the seed: this only has to shuffle four
// items at a time, not stand up to scrutiny.
static uint32_t xorshift32(void)
{
    uint32_t x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
}

static const int8_t k_dr[4] = {-1, 0, 1, 0};  // N, E, S, W
static const int8_t k_dc[4] = {0, 1, 0, -1};
static const uint8_t k_bit[4] = {WALL_N, WALL_E, WALL_S, WALL_W};
static const uint8_t k_opposite[4] = {WALL_S, WALL_W, WALL_N, WALL_E};

// Randomized depth-first backtracker: carve from a random unvisited neighbour
// until stuck, then pop back to the most recent cell that still has one.
// Every cell gets visited exactly once, so the result is always a spanning
// tree over the grid - a "perfect" maze with exactly one path between any two
// cells, which guarantees the start can always reach the goal.
static void carve(void)
{
    for (int r = 0; r < MAZE_ROWS; r++) {
        for (int c = 0; c < MAZE_COLS; c++) {
            s_grid[r][c].walls = WALL_N | WALL_E | WALL_S | WALL_W;
            s_grid[r][c].visited = 0;
        }
    }

    static stack_cell_t stack[MAZE_ROWS * MAZE_COLS];
    int sp = 0;

    s_grid[0][0].visited = 1;
    stack[sp++] = (stack_cell_t){0, 0};

    while (sp > 0) {
        const stack_cell_t cur = stack[sp - 1];

        uint8_t order[4] = {0, 1, 2, 3};
        for (int i = 3; i > 0; i--) {
            const int j = (int)(xorshift32() % (uint32_t)(i + 1));
            const uint8_t tmp = order[i];
            order[i] = order[j];
            order[j] = tmp;
        }

        int found = -1;
        int fr = 0, fc = 0;
        for (int i = 0; i < 4; i++) {
            const int d = order[i];
            const int nr = cur.r + k_dr[d];
            const int nc = cur.c + k_dc[d];
            if (nr >= 0 && nr < MAZE_ROWS && nc >= 0 && nc < MAZE_COLS &&
                !s_grid[nr][nc].visited) {
                found = d;
                fr = nr;
                fc = nc;
                break;
            }
        }

        if (found < 0) {
            sp--;
            continue;
        }

        s_grid[cur.r][cur.c].walls &= (uint8_t)~k_bit[found];
        s_grid[fr][fc].walls &= (uint8_t)~k_opposite[found];
        s_grid[fr][fc].visited = 1;
        stack[sp++] = (stack_cell_t){(int8_t)fr, (int8_t)fc};
    }
}

static void push_wall(float x0, float y0, float x1, float y1)
{
    s_walls[s_wall_count++] = (wall_segment_t){x0, y0, x1, y1};
}

static void build_wall_list(void)
{
    s_wall_count = 0;

    for (int r = 0; r < MAZE_ROWS; r++) {
        for (int c = 0; c < MAZE_COLS; c++) {
            const float x = MAZE_MARGIN_X + c * CELL_PX;
            const float y = MAZE_MARGIN_Y + r * CELL_PX;
            const uint8_t w = s_grid[r][c].walls;

            // Emitting only N and W per cell covers every interior edge
            // exactly once, since each interior edge is the N wall of the
            // cell below it or the W wall of the cell to its right. The two
            // border edges that scheme misses - the bottom row and right
            // column - are added explicitly below.
            if (w & WALL_N) push_wall(x, y, x + CELL_PX, y);
            if (w & WALL_W) push_wall(x, y, x, y + CELL_PX);
            if (r == MAZE_ROWS - 1 && (w & WALL_S)) {
                push_wall(x, y + CELL_PX, x + CELL_PX, y + CELL_PX);
            }
            if (c == MAZE_COLS - 1 && (w & WALL_E)) {
                push_wall(x + CELL_PX, y, x + CELL_PX, y + CELL_PX);
            }
        }
    }
}

void maze_generate(uint32_t seed)
{
    s_rng = seed ? seed : 1;  // xorshift32 is fixed at zero forever
    carve();
    build_wall_list();
}

const wall_segment_t *maze_walls(int *out_count)
{
    *out_count = s_wall_count;
    return s_walls;
}

void maze_start_pos(float *x, float *y)
{
    *x = MAZE_MARGIN_X + CELL_PX * 0.5f;
    *y = MAZE_MARGIN_Y + CELL_PX * 0.5f;
}

void maze_goal_pos(float *x, float *y)
{
    *x = MAZE_MARGIN_X + (MAZE_COLS - 0.5f) * CELL_PX;
    *y = MAZE_MARGIN_Y + (MAZE_ROWS - 0.5f) * CELL_PX;
}
