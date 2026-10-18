#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"

typedef struct {
    float fps;
    float megabytes_per_second;
    float bus_percent; // Pixel payload relative to the 40 MHz 4-line ideal (20 MB/s).
    float average_ms;
    float maximum_ms;
} display_stats_t;

// Called from the SPI ISR when a queued band finished; `context` is the band's.
typedef void (*display_band_done_cb_t)(void *context);

lv_display_t *display_init(void);
// Queue one rectangle (x2/y2 exclusive) of big-endian RGB565 pixels. Blocks only for
// SPI queue space; `done` runs in ISR once sent. `frame_last` closes a stats frame
// that opened with the first band queued after the previous frame closed.
// Call from one task only.
void display_queue_band(int x1, int y1, int x2, int y2, const void *pixels, bool frame_last,
                        display_band_done_cb_t done, void *context);
// Snapshot completed transfers and reset the measurement interval.
display_stats_t display_take_stats(void);
