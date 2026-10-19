#pragma once

#include "lvgl.h"

#include "display.h"
#include "radar.h"
#include "touch.h"

void ui_init(lv_display_t *display);
void ui_set_tier(radar_tier_t tier);
void ui_show_stats(const display_stats_t *display, const radar_stats_t *radar);
void ui_show_touch(touch_status_t status);
