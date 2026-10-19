#pragma once

#include "esp_err.h"

typedef enum {
    TOUCH_SWIPE_NONE,
    TOUCH_SWIPE_UP,
    TOUCH_SWIPE_DOWN,
} touch_swipe_t;

typedef enum {
    TOUCH_STATUS_OFF,
    TOUCH_STATUS_UP,
    TOUCH_STATUS_DOWN,
    TOUCH_STATUS_ERROR,
} touch_status_t;

// Called once at startup. On failure, touch_poll() remains disabled.
esp_err_t touch_init(void);
// Display task only; samples at most every 10 ms and emits an event on release.
// Always writes the latest raw contact/read status, including between samples.
touch_swipe_t touch_poll(touch_status_t *status);
