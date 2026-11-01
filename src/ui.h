#pragma once

#include "lvgl.h"

#include "battery.h"
#include "network/wifi.h"
#include "time/clock.h"

void ui_init(lv_display_t *display);
void ui_show_battery(const battery_reading_t *reading);
// Main-task-only; hit testing is restricted to the cumulative reading area.
void ui_handle_tap(int x, int y);
typedef enum { UI_ACTION_NONE, UI_ACTION_OPEN_WIFI, UI_ACTION_CANCEL_WIFI, UI_ACTION_SYNC_TIME } ui_action_t;
ui_action_t ui_handle_network_tap(int x, int y, int64_t now_us);
void ui_show_network(const wifi_view_t *wifi, const clock_view_t *clock);
