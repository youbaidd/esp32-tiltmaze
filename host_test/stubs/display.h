#pragma once
#include <stdint.h>

typedef int esp_err_t;

// Host stand-ins: acquire hands back a reusable band-sized buffer, flush
// copies it into the full-screen host framebuffer defined in main_host.c.
uint16_t *display_acquire_band(void);
esp_err_t display_flush_band(int band_index, const uint16_t *buffer);
