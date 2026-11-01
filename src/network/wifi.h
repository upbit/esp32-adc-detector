#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool portal;
    bool connected;
    bool active;
    bool configured;
    int seconds_remaining;
    char hotspot[33];
} wifi_view_t;

esp_err_t wifi_init(void);
// All lifecycle calls run in the main task, never in HTTP/Wi-Fi callbacks.
void wifi_poll(int64_t now_us);
void wifi_open_portal(int64_t now_us);
void wifi_sync_time(int64_t now_us);
void wifi_cancel(int64_t now_us);
void wifi_get_view(wifi_view_t *view);
