# host_test

A host-native (plain gcc, no ESP-IDF) reproduction of TiltMaze's render
pipeline, in the spirit of FluidBox's `tools/preview`. It compiles the real
`maze.c`, `game.c`, and `render.c` against stub headers in `stubs/` instead
of the ESP-IDF display/IMU/FreeRTOS APIs, runs `game_init()` + `render_frame()`
on your Mac, and writes the assembled screen out as an image - so drawing
changes and memory bugs (via ASan/UBSan) can be checked without flashing
hardware.

This exists because of a real incident: TiltMaze's maze+ball+goal render
went intermittently full-black on the actual board while this exact
harness proved the drawing code correct and sanitizer-clean the whole
time. It's not a substitute for checking the real panel, but it rules
software logic in or out fast.

## Usage

```bash
./build.sh out.ppm 5    # renders 5 frames, writes out.ppm and out.png
```

Defaults to `frame.ppm` / 1 frame if no arguments are given. Requires
Pillow (`pip install pillow`) for the `.png` conversion; without it you
still get the `.ppm`, which most image viewers and `sips` can open directly.

## How it stays in sync

`build.sh` copies `../main/render.c`, `../main/maze.c`, and `../main/game.c`
into this directory before compiling (C's quoted `#include` always checks
the including file's own directory first, which is how the stub headers win
over the absent real ESP-IDF ones). So it always tests the current sources -
there's nothing to keep manually synced.
