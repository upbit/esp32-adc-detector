#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi.h"

#define PORTAL_NETWORKS_MAX 20
typedef struct {
    char ssid[33];
    char password[65];
} wifi_credentials_t;

typedef enum { PORTAL_SCAN, PORTAL_CONNECT } portal_command_kind_t;
typedef struct {
    portal_command_kind_t kind;
    wifi_credentials_t credentials;
} portal_command_t;

typedef enum {
    PORTAL_READY, PORTAL_CONNECTING, PORTAL_CONNECTED,
    PORTAL_SYNCED, PORTAL_TIME_FAILED, PORTAL_CONNECT_FAILED,
    PORTAL_SAVE_FAILED, PORTAL_SCAN_FAILED
} portal_state_t;

esp_err_t portal_start(void);
esp_err_t portal_stop(void);
bool portal_take_command(portal_command_t *command);
void portal_publish(portal_state_t state, bool scanning);
void portal_set_networks(const wifi_ap_record_t *records, unsigned count);
