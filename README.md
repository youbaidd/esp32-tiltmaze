# TiltMaze

A tilt-controlled marble maze for the [Waveshare ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm). Tilt the board to roll the ball through a randomly generated labyrinth to the gold goal. Solving one immediately generates the next.

Built on the same raw ESP-IDF foundation as [FluidBox](../esp32-fluidbox): a band-based renderer that streams the AMOLED panel in horizontal strips over DMA, and the QMI8658 IMU read as a low-passed tilt vector.

## Controls

- **Tilt** the board: rolls the ball.
- **PWR button**, short press: abandon the current maze and generate a new one.

## How it works

- **Maze** — a randomized depth-first backtracker carves a perfect (single-solution) maze over an 8x10 grid each time one is needed, then the grid is flattened into a list of wall line segments.
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

## Known issue: panel intermittently went black on real hardware

While bringing this up, the maze+ball+goal render intermittently went full
black on the actual board even though `host_test/` proves the exact same
drawing code renders correctly and passes ASan/UBSan clean - so it wasn't a
logic or memory-safety bug. No crash, no reboot, steady fps/step counters
and healthy task-stack headroom throughout every failure. The QSPI clock
was dropped from 80 MHz to 40 MHz in `main/display.c` as the most likely
fix, since FluidBox's own code already flags 80 MHz over these
GPIO-matrix-routed pins as marginal ("drop to 40 MHz if the panel ever
shows tearing or corrupted pixels"). That change is unverified on-screen as
of this writing - **check the panel and report back** whether the maze
renders correctly now. If it's still wrong, the next things worth trying:
run `host_test/build.sh` after any drawing change to keep ruling logic
bugs out fast, and consider that PWR-button debounce timing, I2C traffic
from the IMU/button poll, or something else contending for CPU/bus time
right around a render call could be worth instrumenting next.
