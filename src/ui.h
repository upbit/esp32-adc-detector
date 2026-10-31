#pragma once

#include "lvgl.h"

#include "ina226.h"

void ui_init(lv_display_t *display);
// NULL clears the live readings after an INA226 initialization/read failure.
void ui_show_sample(const ina226_sample_t *sample);
