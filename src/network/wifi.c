#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "portal.h"
#include "wifi.h"
#include "time/clock.h"

#define RETRY_US INT64_C(43200000000)
#define ATTEMPT_US INT64_C(60000000)
#define PORTAL_US INT64_C(300000000)

typedef enum { OFF, PORTAL, CONNECTING, SYNCING, RESULT, STOPPING } phase_t;
typedef enum { ASSOCIATED, GOT_IP, DISCONNECTED, SCANNED } event_kind_t;
typedef struct { event_kind_t kind; unsigned generation; int reason; } radio_event_t;

static phase_t phase;
static wifi_view_t view;
static wifi_credentials_t saved, pending;
static nvs_handle_t storage;
static QueueHandle_t events;
static esp_netif_t *station, *access_point;
static esp_event_handler_instance_t wifi_handler, ip_handler;
static bool stack_ready, initialized, started, portal_running, scan_active, disconnect_pending;
static unsigned generation;
static int64_t attempt_until, portal_until, next_attempt, result_until, connected_at, cleanup_after;
static portMUX_TYPE event_lock = portMUX_INITIALIZER_UNLOCKED;
static bool overflow;

static void publish(portal_state_t state)
{
    portal_publish(state, scan_active);
}

static void event_callback(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    radio_event_t event = {.generation = (unsigned)(uintptr_t)arg};
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) event.kind = GOT_IP;
    else if (id == WIFI_EVENT_STA_CONNECTED) event.kind = ASSOCIATED;
    else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        event.kind = DISCONNECTED;
        event.reason = ((wifi_event_sta_disconnected_t *)data)->reason;
    } else if (id == WIFI_EVENT_SCAN_DONE) {
        event.kind = SCANNED;
        event.reason = ((wifi_event_sta_scan_done_t *)data)->status;
    } else return;
    if (xQueueSend(events, &event, 0) != pdTRUE) {
        portENTER_CRITICAL(&event_lock);
        overflow = true;
        portEXIT_CRITICAL(&event_lock);
    }
}

static void shutdown(int64_t now_us)
{
    clock_stop();
    memset(&pending, 0, sizeof(pending));
    esp_err_t err = portal_running ? portal_stop() : ESP_OK;
    if (err == ESP_OK) portal_running = false;
    scan_active = false;
    view.portal = false;
    view.seconds_remaining = 0;
    if (wifi_handler) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler);
    if (ip_handler) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
    wifi_handler = ip_handler = NULL;
    if (started) {
        esp_err_t stop_err = esp_wifi_stop();
        if (stop_err == ESP_OK || stop_err == ESP_ERR_WIFI_NOT_STARTED) {
            started = false;
            view.active = false;
            view.connected = false;
        } else err = stop_err;
    }
    if (err == ESP_OK && initialized) {
        err = esp_wifi_deinit();
        if (err == ESP_OK) initialized = false;
    }
    if (err != ESP_OK) {
        ESP_LOGE("wifi", "Radio cleanup failed: %s; retrying cleanup", esp_err_to_name(err));
        phase = STOPPING;
        cleanup_after = now_us + 1000000;
        return;
    }
    if (station) esp_netif_destroy_default_wifi(station);
    if (access_point) esp_netif_destroy_default_wifi(access_point);
    station = access_point = NULL;
    disconnect_pending = false;
    xQueueReset(events);
    phase = OFF;
    next_attempt = view.configured ? now_us + RETRY_US : 0;
    ESP_LOGI("wifi", "Wi-Fi stopped and deinitialized; next sync %s", view.configured ? "in 12 hours" : "disabled");
}

static esp_err_t start_radio(bool hotspot)
{
    esp_err_t err;
    if (!stack_ready) {
        err = esp_netif_init();
        if (err != ESP_OK) return err;
        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
        stack_ready = true;
    }
    station = esp_netif_create_default_wifi_sta();
    if (!station) return ESP_ERR_NO_MEM;
    if (hotspot) {
        access_point = esp_netif_create_default_wifi_ap();
        if (!access_point) return ESP_ERR_NO_MEM;
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    initialized = true;
    generation++;
    xQueueReset(events);
    portENTER_CRITICAL(&event_lock);
    overflow = false;
    portEXIT_CRITICAL(&event_lock);
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_callback,
                                             (void *)(uintptr_t)generation, &wifi_handler);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_callback,
                                             (void *)(uintptr_t)generation, &ip_handler);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_mode(hotspot ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_country_code("CN", false);
    if (err != ESP_OK) return err;
    if (hotspot) {
        uint8_t mac[6];
        err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        if (err != ESP_OK) return err;
        snprintf(view.hotspot, sizeof(view.hotspot), "Solar-%02X%02X", mac[4], mac[5]);
        wifi_config_t ap = {.ap = {.channel = 1, .max_connection = 2, .authmode = WIFI_AUTH_OPEN}};
        memcpy(ap.ap.ssid, view.hotspot, strlen(view.hotspot));
        ap.ap.ssid_len = strlen(view.hotspot);
        err = esp_wifi_set_config(WIFI_IF_AP, &ap);
        if (err != ESP_OK) return err;
    }
    err = esp_wifi_start();
    if (err == ESP_OK) view.active = started = true;
    return err;
}

static void scan(void)
{
    if (phase != PORTAL || scan_active || disconnect_pending) return;
    wifi_scan_config_t config = {.show_hidden = true};
    esp_err_t err = esp_wifi_scan_start(&config, false);
    scan_active = err == ESP_OK;
    publish(scan_active ? PORTAL_READY : PORTAL_SCAN_FAILED);
}

static void connect(const wifi_credentials_t *credentials, int64_t now_us)
{
    if (scan_active) esp_wifi_scan_stop();
    scan_active = false;
    pending = *credentials;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, pending.ssid, strlen(pending.ssid));
    memcpy(config.sta.password, pending.password, strlen(pending.password));
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) err = esp_wifi_connect();
    memset(&config, 0, sizeof(config));
    if (err != ESP_OK) {
        ESP_LOGW("wifi", "Connection could not start: %s", esp_err_to_name(err));
        publish(PORTAL_CONNECT_FAILED);
        if (!portal_running) { clock_fail(now_us); shutdown(now_us); }
        return;
    }
    phase = CONNECTING;
    attempt_until = now_us + ATTEMPT_US;
    publish(PORTAL_CONNECTING);
}

static void finish(bool success, int64_t now_us)
{
    clock_stop();
    if (!success) clock_fail(now_us);
    publish(success ? PORTAL_SYNCED : PORTAL_TIME_FAILED);
    if (portal_running && now_us < connected_at + 2000000) {
        // Give the browser one bounded opportunity to receive the connection result.
        phase = RESULT;
        result_until = connected_at + 2000000;
    } else shutdown(now_us);
}

static void start_sync(int64_t now_us)
{
    next_attempt = 0;
    esp_err_t err = start_radio(false);
    if (err == ESP_OK) connect(&saved, now_us);
    else {
        ESP_LOGW("wifi", "Time sync radio initialization failed: %s", esp_err_to_name(err));
        clock_fail(now_us);
        shutdown(now_us);
    }
}

esp_err_t wifi_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err; // Never erase unrelated NVS data on an initialization error.
    err = nvs_open("solar_wifi", NVS_READWRITE, &storage);
    if (err != ESP_OK) return err;
    events = xQueueCreate(16, sizeof(radio_event_t));
    if (!events) { nvs_close(storage); storage = 0; return ESP_ERR_NO_MEM; }
    size_t size = sizeof(saved);
    err = nvs_get_blob(storage, "credentials", &saved, &size);
    if (err == ESP_OK && size == sizeof(saved) && saved.ssid[0] &&
        memchr(saved.ssid, 0, sizeof(saved.ssid)) && memchr(saved.password, 0, sizeof(saved.password)))
        view.configured = true;
    else if (err != ESP_ERR_NVS_NOT_FOUND) ESP_LOGW("wifi", "Credentials unavailable; manual provisioning required");
    ESP_LOGI("wifi", "Startup: radio off, credentials %s", view.configured ? "saved" : "absent");
    next_attempt = view.configured ? 1 : 0;
    return ESP_OK;
}

void wifi_open_portal(int64_t now_us)
{
    if (!events || phase == STOPPING) return;
    if (phase != OFF) shutdown(now_us);
    if (phase != OFF) return;
    esp_err_t err = start_radio(true);
    if (err == ESP_OK) {
        err = portal_start();
        if (err != ESP_OK) portal_running = portal_stop() != ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE("wifi", "Hotspot failed: %s", esp_err_to_name(err));
        clock_fail(now_us);
        shutdown(now_us);
        return;
    }
    portal_running = view.portal = true;
    phase = PORTAL;
    portal_until = now_us + PORTAL_US;
    view.seconds_remaining = 300;
    scan();
    ESP_LOGI("wifi", "Provisioning hotspot %s at http://192.168.4.1 (5 minute limit)", view.hotspot);
}

void wifi_cancel(int64_t now_us)
{
    if (events && phase != OFF) shutdown(now_us);
}

void wifi_sync_time(int64_t now_us)
{
    if (!events || !view.configured || phase != OFF) return;
    ESP_LOGI("wifi", "Manual time sync requested; resetting 12 hour schedule");
    start_sync(now_us);
}

void wifi_get_view(wifi_view_t *snapshot) { *snapshot = view; }

void wifi_poll(int64_t now_us)
{
    if (!events) return;
    if (phase == STOPPING) {
        if (now_us >= cleanup_after) shutdown(now_us);
        return;
    }
    portENTER_CRITICAL(&event_lock);
    bool lost = overflow;
    overflow = false;
    portEXIT_CRITICAL(&event_lock);
    if (lost) {
        ESP_LOGE("wifi", "Event overflow; shutting radio down");
        clock_fail(now_us);
        shutdown(now_us);
        return;
    }
    if (phase == OFF && next_attempt && now_us >= next_attempt) {
        start_sync(now_us);
    }
    radio_event_t event;
    while (xQueueReceive(events, &event, 0) == pdTRUE) {
        if (event.generation != generation || phase == OFF) continue;
        if (event.kind == ASSOCIATED && phase == CONNECTING) view.connected = true;
        if (event.kind == SCANNED) {
            if (phase == PORTAL && scan_active) {
                // Single-main-task scratch buffer; keep it off the ESP32 main stack.
                static wifi_ap_record_t records[PORTAL_NETWORKS_MAX];
                uint16_t count = PORTAL_NETWORKS_MAX;
                esp_err_t err = event.reason ? ESP_FAIL : esp_wifi_scan_get_ap_records(&count, records);
                portal_set_networks(records, err == ESP_OK ? count : 0);
                scan_active = false;
                publish(err == ESP_OK ? PORTAL_READY : PORTAL_SCAN_FAILED);
            } else esp_wifi_clear_ap_list();
        }
        if (event.kind == GOT_IP && phase == CONNECTING) {
            view.connected = true;
            connected_at = now_us;
            if (portal_running) {
                esp_err_t err = nvs_set_blob(storage, "credentials", &pending, sizeof(pending));
                if (err == ESP_OK) err = nvs_commit(storage);
                if (err != ESP_OK) {
                    publish(PORTAL_SAVE_FAILED);
                    view.portal = false;
                    clock_fail(now_us);
                    phase = RESULT;
                    result_until = now_us + 2000000;
                    continue;
                }
                saved = pending;
                view.configured = true;
            }
            memset(&pending, 0, sizeof(pending));
            view.portal = false;
            publish(PORTAL_CONNECTED);
            phase = SYNCING;
            if (clock_start() != ESP_OK) finish(false, now_us);
        }
        if (event.kind == DISCONNECTED) {
            view.connected = false;
            disconnect_pending = false;
            if (phase == CONNECTING) {
                ESP_LOGW("wifi", "Connection failed, reason=%d", event.reason);
                memset(&pending, 0, sizeof(pending));
                if (portal_running) { phase = PORTAL; publish(PORTAL_CONNECT_FAILED); }
                else { clock_fail(now_us); shutdown(now_us); }
            } else if (phase == SYNCING) finish(clock_poll(now_us), now_us);
        }
    }
    portal_command_t command;
    if (portal_take_command(&command)) {
        if (phase == PORTAL && !disconnect_pending) {
            if (command.kind == PORTAL_SCAN) scan();
            else connect(&command.credentials, now_us);
        }
        memset(&command, 0, sizeof(command));
    }
    if (phase == SYNCING && clock_poll(now_us)) finish(true, now_us);
    if ((phase == CONNECTING || phase == SYNCING) && now_us >= attempt_until) {
        if (phase == CONNECTING && portal_running) {
            disconnect_pending = view.connected;
            esp_wifi_disconnect();
            view.connected = false;
            memset(&pending, 0, sizeof(pending));
            phase = PORTAL;
            publish(PORTAL_CONNECT_FAILED);
        } else finish(false, now_us);
    }
    if ((phase == RESULT && now_us >= result_until) || (portal_running && now_us >= portal_until)) shutdown(now_us);
    if (view.portal) view.seconds_remaining = (int)((portal_until - now_us + 999999) / 1000000);
}
