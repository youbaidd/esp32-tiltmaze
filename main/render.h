#pragma once

// Draws one frame of the maze, band by band, straight to the panel. Plain
// software rasterizer: filled rects for the axis-aligned walls, filled discs
// for the ball and the goal marker. No framebuffer for the whole screen; see
// display.h.

void render_init(void);

// Reads the current game snapshot and paints it. Blocks until the last band
// has been handed to the DMA engine.
void render_frame(void);
