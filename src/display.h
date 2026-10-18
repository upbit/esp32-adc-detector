#pragma once

#include "lvgl.h"

typedef struct {
    float fps;
    float megabytes_per_second;
    float full_frame_fps;
    float average_ms;
    float maximum_ms;
} display_stats_t;

lv_display_t *display_init(void);
// Snapshot completed transfers and reset the measurement interval.
display_stats_t display_take_stats(void);
