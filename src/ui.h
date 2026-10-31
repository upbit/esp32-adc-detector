#pragma once

#include "lvgl.h"

#include "battery.h"

void ui_init(lv_display_t *display);
void ui_show_battery(const battery_reading_t *reading);
// Main-task-only; hit testing is restricted to the cumulative reading area.
void ui_handle_tap(int x, int y);
