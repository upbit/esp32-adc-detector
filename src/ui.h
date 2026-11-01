#pragma once

#include "lvgl.h"

#include "battery.h"
#include "history.h"
#include "network/wifi.h"
#include "time/clock.h"

void ui_init(lv_display_t *display);
void ui_show_battery(const battery_reading_t *reading);
void ui_show_history(const history_view_t *day, const history_view_t *week);
bool ui_handle_history_tap(int x, int y, int64_t now_us);
typedef enum { UI_ACTION_NONE, UI_ACTION_OPEN_WIFI, UI_ACTION_CANCEL_WIFI, UI_ACTION_SYNC_TIME } ui_action_t;
ui_action_t ui_handle_network_tap(int x, int y, int64_t now_us);
void ui_show_network(const wifi_view_t *wifi, const clock_view_t *clock);
