#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum { CLOCK_NORMAL, CLOCK_SYNCED, CLOCK_FAILED } clock_feedback_t;
typedef struct {
    bool valid;
    char text[6];
    clock_feedback_t feedback;
} clock_view_t;

void clock_init(void);
esp_err_t clock_start(void);
void clock_stop(void);
// Main task only. True once per valid SNTP notification, including unchanged time.
bool clock_poll(int64_t now_us);
void clock_fail(int64_t now_us);
void clock_get_view(int64_t now_us, clock_view_t *view);
