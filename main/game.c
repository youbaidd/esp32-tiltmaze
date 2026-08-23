#include "game.h"

#include <math.h>

#include "config.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "maze.h"

// Guards the handful of floats below. They are only ever touched for a few
// instructions at a time, so a spinlock is cheaper than a semaphore and safe
// to take from both the physics task and the render task.
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static float s_x, s_y, s_vx, s_vy;
static game_state_t s_state;
static float s_win_timer_ms;

static void place_ball_at_start(void)
{
    maze_start_pos(&s_x, &s_y);
    s_vx = 0.0f;
    s_vy = 0.0f;
}

// Caller must hold s_lock.
static void regenerate(void)
{
    maze_generate(esp_random());
    place_ball_at_start();
    s_state = GAME_PLAYING;
    s_win_timer_ms = 0.0f;
}

void game_init(void)
{
    portENTER_CRITICAL(&s_lock);
    regenerate();
    portEXIT_CRITICAL(&s_lock);
}

void game_force_reset(void)
{
    portENTER_CRITICAL(&s_lock);
    regenerate();
    portEXIT_CRITICAL(&s_lock);
}

// Pushes the ball out of every wall segment it is currently overlapping and
// kills the component of its velocity driving it into each one. Segments are
// axis aligned but this is a plain circle-vs-segment test, so it does not
// care which axis - the corners where an N and a W wall meet fall out of the
// closest-point-on-segment math for free.
static void resolve_wall_collisions(void)
{
    int count;
    const wall_segment_t *walls = maze_walls(&count);
    const float reach = BALL_RADIUS + WALL_THICK * 0.5f;
    const float reach2 = reach * reach;

    for (int i = 0; i < count; i++) {
        const wall_segment_t *w = &walls[i];

        const float ex = w->x1 - w->x0;
        const float ey = w->y1 - w->y0;
        const float len2 = ex * ex + ey * ey;

        float t = 0.0f;
        if (len2 > 1e-6f) {
            t = ((s_x - w->x0) * ex + (s_y - w->y0) * ey) / len2;
            if (t < 0.0f) t = 0.0f;
            else if (t > 1.0f) t = 1.0f;
        }

        const float cx = w->x0 + t * ex;
        const float cy = w->y0 + t * ey;
        const float dx = s_x - cx;
        const float dy = s_y - cy;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 >= reach2) {
            continue;
        }

        float dist = sqrtf(dist2);
        float nx, ny;
        if (dist > 1e-5f) {
            nx = dx / dist;
            ny = dy / dist;
        } else {
            // The ball's center landed exactly on the wall's line - push
            // along its normal instead of a zero vector so it cannot stick.
            nx = -ey;
            ny = ex;
            const float nlen = sqrtf(nx * nx + ny * ny);
            if (nlen > 1e-5f) {
                nx /= nlen;
                ny /= nlen;
            } else {
                nx = 1.0f;
                ny = 0.0f;
            }
        }

        const float penetration = reach - dist;
        s_x += nx * penetration;
        s_y += ny * penetration;

        const float vn = s_vx * nx + s_vy * ny;
        if (vn < 0.0f) {
            s_vx -= (1.0f + WALL_RESTITUTION) * vn * nx;
            s_vy -= (1.0f + WALL_RESTITUTION) * vn * ny;
        }
    }
}

void game_step(float dt, float tilt_x, float tilt_y)
{
    portENTER_CRITICAL(&s_lock);

    if (s_state == GAME_WON) {
        s_win_timer_ms += dt * 1000.0f;
        if (s_win_timer_ms >= WIN_FLASH_MS) {
            regenerate();
        }
        portEXIT_CRITICAL(&s_lock);
        return;
    }

    float ax = tilt_x * TILT_GAIN;
    float ay = tilt_y * TILT_GAIN;
    const float amag = sqrtf(ax * ax + ay * ay);
    if (amag > MAX_ACCEL) {
        const float s = MAX_ACCEL / amag;
        ax *= s;
        ay *= s;
    }

    s_vx = (s_vx + ax * dt) * BALL_DAMPING;
    s_vy = (s_vy + ay * dt) * BALL_DAMPING;

    const float speed = sqrtf(s_vx * s_vx + s_vy * s_vy);
    if (speed > MAX_SPEED) {
        const float s = MAX_SPEED / speed;
        s_vx *= s;
        s_vy *= s;
    }

    s_x += s_vx * dt;
    s_y += s_vy * dt;

    resolve_wall_collisions();

    // A hard clamp to the outer boundary as a last resort. Every border cell
    // already carries a wall segment, so this should never actually trigger;
    // it exists so a bug in the collision math fails as "stuck at the edge"
    // rather than as the ball leaving the visible playfield.
    const float min_x = MAZE_MARGIN_X + BALL_RADIUS;
    const float max_x = MAZE_MARGIN_X + MAZE_COLS * CELL_PX - BALL_RADIUS;
    const float min_y = MAZE_MARGIN_Y + BALL_RADIUS;
    const float max_y = MAZE_MARGIN_Y + MAZE_ROWS * CELL_PX - BALL_RADIUS;
    if (s_x < min_x) { s_x = min_x; s_vx = 0.0f; }
    if (s_x > max_x) { s_x = max_x; s_vx = 0.0f; }
    if (s_y < min_y) { s_y = min_y; s_vy = 0.0f; }
    if (s_y > max_y) { s_y = max_y; s_vy = 0.0f; }

    float gx, gy;
    maze_goal_pos(&gx, &gy);
    const float dgx = s_x - gx;
    const float dgy = s_y - gy;
    if (dgx * dgx + dgy * dgy < GOAL_RADIUS * GOAL_RADIUS) {
        s_state = GAME_WON;
        s_win_timer_ms = 0.0f;
        s_vx = 0.0f;
        s_vy = 0.0f;
    }

    portEXIT_CRITICAL(&s_lock);
}

void game_get_snapshot(game_snapshot_t *out)
{
    portENTER_CRITICAL(&s_lock);
    out->ball_x = s_x;
    out->ball_y = s_y;
    out->state = s_state;
    out->win_progress = (s_state == GAME_WON) ? (s_win_timer_ms / (float)WIN_FLASH_MS) : 0.0f;
    portEXIT_CRITICAL(&s_lock);
}
