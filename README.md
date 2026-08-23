# TiltMaze

A tilt-controlled marble maze for the [Waveshare ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm). Tilt the board to roll the ball through a randomly generated labyrinth to the gold goal. Solving one immediately generates the next.

Built on the same raw ESP-IDF foundation as [FluidBox](https://github.com/V4C38/esp32-fluidbox), a fluid-simulation project for the same board: a band-based renderer that streams the AMOLED panel in horizontal strips over DMA, and the QMI8658 IMU read as a low-passed tilt vector.

## Controls

- **Tilt** the board: rolls the ball.
- **PWR button**, short press: abandon the current maze and generate a new one.

## How it works

- **Maze** — a randomized depth-first backtracker carves a perfect (single-solution) maze over a 7x9 grid each time one is needed, then the grid is flattened into a list of wall line segments. The grid is inset far enough from the panel's edges to clear its rounded corners (see `main/config.h`).
- **Physics** — the ball is a point mass with tilt-driven acceleration, rolling friction, and a speed cap; wall collisions are plain circle-vs-segment tests against that same wall list, run at 120 Hz on core 1.
- **Rendering** — core 0 redraws the maze, ball, and pulsing goal marker band by band, the same DMA-overlapped pipeline FluidBox uses, at roughly 90+ fps.

## Tuning

`main/config.h` holds every constant that affects feel: `TILT_GAIN` and `MAX_ACCEL` for how strongly tilt pushes the ball, `BALL_DAMPING` for rolling friction, `WALL_RESTITUTION` for how bouncy a wall hit is, `MAZE_COLS`/`MAZE_ROWS`/`CELL_PX` for maze size and difficulty. None of these were measured on hardware the way FluidBox's were - they're a reasonable starting point, tune them by feel once you've rolled the ball around.

## Running it

This board needs ESP-IDF **v5.5.5** specifically - `~/esp/esp-idf` on this
machine has since moved to v6.0.2, which is untested against this panel.
Use the pinned checkout instead:

```bash
. ~/esp/v5.5.5/esp-idf/export.sh
cd tiltmaze
idf.py -p /dev/cu.usbmodem14801 flash monitor
```

## Layout

| Path | Contents |
|---|---|
| `main/display.c`, `main/imu.c`, `main/button.c` | Hardware drivers, carried over from FluidBox almost unchanged |
| `main/maze.c` | Maze generation and the wall-segment list |
| `main/game.c` | Ball physics, collision, win detection, regenerate-on-solve |
| `main/render.c` | Band-by-band drawing of walls, ball, and goal |
| `main/main.c` | Boot sequence and the two-task (physics/render) split |
| `host_test/` | Host-native reproduction of the render pipeline, for checking drawing changes without flashing hardware - see its README |

## A bring-up gotcha worth knowing if you're porting this to another project

The LCD reset, DSI power-enable, and touch-reset lines on this board are not
ESP32 GPIOs - they're output pins on the TCA9554 IO expander at I2C address
`0x20` (the same chip the PWR button lives on). That expander is a separate
chip with its own power domain: reflashing or resetting the ESP32 does
**not** reset it, so those lines just hold whatever level some earlier,
unrelated firmware left them at. If they come up asserted, the panel is
electrically held in reset and silently ignores every command sent to it -
while the firmware driving it looks completely healthy (correct rendering,
stable fps, no crashes), because from the ESP32's side nothing is wrong.

`main/display.c`'s `reset_display_lines()`, called first thing in
`display_init()`, pulses those lines low then releases them before anything
else touches the panel or probes for the touch controller. The exact
register layout and timing come from the board repo's own
`examples/esp-idf/90_axp2101_pmu/components/board_variant/board_variant.c`
(`release_touch_reset()`) - the one first-party example that actually
drives this reset. If you're adapting this code (or FluidBox's, which has
the same gap) into a new project on this board, don't drop this function.
