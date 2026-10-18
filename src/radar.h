#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "display.h"

typedef enum {
    RADAR_TIER_10,
    RADAR_TIER_25,
    RADAR_TIER_35,
    RADAR_TIER_50,
    RADAR_TIER_FULL, // Whole circle every frame.
    RADAR_TIER_COUNT,
} radar_tier_t;

typedef struct {
    float render_ms;  // Core 1 time spent filling bands, per frame.
    uint32_t frames;  // Frames rendered in the interval.
} radar_stats_t;

// Geometry shared with the UI (circle sits below the info area).
#define RADAR_CENTER_X 184
#define RADAR_CENTER_Y 264
#define RADAR_RADIUS 176

// Builds the angle table and starts the core 1 render task. Bands are handed to
// the display task through radar_pump(), which must run on the display task.
void radar_init(void);
// Tier switch; takes effect at the next frame boundary. The next frame redraws the
// whole circle so a shorter trail leaves no stale pixels.
void radar_set_tier(radar_tier_t tier);
// Percentage of the full screen (164,864 px) redrawn per frame for a tier.
int radar_tier_percent(radar_tier_t tier);
// Queue ready bands to the display, waiting up to `timeout_ms` for the first one.
// Returns after a frame's last band or when none is ready. Display task only.
void radar_pump(uint32_t timeout_ms);
radar_stats_t radar_take_stats(void);
