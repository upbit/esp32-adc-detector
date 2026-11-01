#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sdk.h"
#include "network/portal.h"
#include "network/wifi.h"
#include "time/clock.h"

// Exercise the actual lifecycle implementation against deterministic SDK events.
#include "network/wifi.c"

const char *WIFI_EVENT = "wifi", *IP_EVENT = "ip";
static unsigned init_count, stop_count, deinit_count, connect_count;
static bool rf, http, sntp, have_saved, have_command;
static esp_err_t start_error, commit_error, stop_error, http_stop_error;
static wifi_credentials_t persisted, staged;
static portal_command_t request;
static portal_state_t http_state;
static void (*sync_callback)(struct timeval *);
static time_t wall;

typedef struct { unsigned capacity, size, count; unsigned char *bytes; } queue_t;
QueueHandle_t xQueueCreate(unsigned capacity, unsigned size)
{
    queue_t *q = calloc(1, sizeof(*q)); assert(q);
    q->capacity = capacity; q->size = size; q->bytes = calloc(capacity, size); assert(q->bytes); return q;
}
int xQueueSend(QueueHandle_t handle, const void *item, unsigned timeout)
{
    (void)timeout; queue_t *q = handle; if (q->count == q->capacity) return 0;
    memcpy(q->bytes + q->count++ * q->size, item, q->size); return pdTRUE;
}
int xQueueReceive(QueueHandle_t handle, void *item, unsigned timeout)
{
    (void)timeout; queue_t *q = handle; if (!q->count) return 0;
    memcpy(item, q->bytes, q->size); q->count--;
    memmove(q->bytes, q->bytes + q->size, q->count * q->size); return pdTRUE;
}
void xQueueReset(QueueHandle_t handle) { ((queue_t *)handle)->count = 0; }
void vQueueDelete(QueueHandle_t handle) { queue_t *q = handle; free(q->bytes); free(q); }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "mock error"; }
esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
esp_netif_t *esp_netif_create_default_wifi_sta(void) { return calloc(1, sizeof(esp_netif_t)); }
esp_netif_t *esp_netif_create_default_wifi_ap(void) { return calloc(1, sizeof(esp_netif_t)); }
void esp_netif_destroy_default_wifi(void *netif) { free(netif); }
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
    void (*callback)(void *, esp_event_base_t, int32_t, void *), void *arg, esp_event_handler_instance_t *instance)
{ (void)base; (void)id; (void)callback; (void)arg; *instance = (void *)1; return ESP_OK; }
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id, esp_event_handler_instance_t instance)
{ (void)base; (void)id; (void)instance; return ESP_OK; }
esp_err_t esp_wifi_init(const wifi_init_config_t *config) { (void)config; init_count++; return ESP_OK; }
esp_err_t esp_wifi_deinit(void) { assert(!rf); deinit_count++; return ESP_OK; }
esp_err_t esp_wifi_start(void) { if (start_error) return start_error; rf = true; return ESP_OK; }
esp_err_t esp_wifi_stop(void) { stop_count++; if (stop_error) return stop_error; rf = false; return ESP_OK; }
esp_err_t esp_wifi_set_storage(int mode) { assert(mode == WIFI_STORAGE_RAM); return ESP_OK; }
esp_err_t esp_wifi_set_mode(int mode) { assert(mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA); return ESP_OK; }
esp_err_t esp_wifi_set_country_code(const char *country, bool enabled) { (void)enabled; assert(!strcmp(country, "CN")); return ESP_OK; }
esp_err_t esp_wifi_set_config(int interface, wifi_config_t *config)
{
    if (interface == WIFI_IF_AP) {
        assert(config->ap.authmode == WIFI_AUTH_OPEN);
        for (unsigned i = 0; i < sizeof(config->ap.password); i++) assert(!config->ap.password[i]);
        assert(config->ap.ssid_len == 10 && !memcmp(config->ap.ssid, "Solar-0101", 10));
    }
    return ESP_OK;
}
esp_err_t esp_wifi_connect(void) { connect_count++; return ESP_OK; }
esp_err_t esp_wifi_disconnect(void) { return ESP_OK; }
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block) { (void)config; assert(!block); return ESP_OK; }
esp_err_t esp_wifi_scan_stop(void) { return ESP_OK; }
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count, wifi_ap_record_t *records) { (void)records; *count = 0; return ESP_OK; }
esp_err_t esp_wifi_clear_ap_list(void) { return ESP_OK; }
esp_err_t esp_read_mac(uint8_t *mac, int type) { (void)type; memset(mac, 1, 6); return ESP_OK; }
void esp_fill_random(void *buffer, size_t size) { memset(buffer, 0xab, size); }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) { (void)name; (void)mode; *handle = 1; return ESP_OK; }
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *size)
{ (void)handle; (void)key; assert(*size == sizeof(persisted)); if (!have_saved) return ESP_ERR_NVS_NOT_FOUND; memcpy(value, &persisted, *size); return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size)
{ (void)handle; (void)key; assert(size == sizeof(staged)); memcpy(&staged, value, size); return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t handle) { (void)handle; if (commit_error) return commit_error; persisted = staged; have_saved = true; return ESP_OK; }
esp_err_t portal_start(void) { http = true; return ESP_OK; }
esp_err_t portal_stop(void) { if (http_stop_error) return http_stop_error; http = have_command = false; return ESP_OK; }
bool portal_take_command(portal_command_t *command) { if (!have_command) return false; *command = request; have_command = false; return true; }
void portal_publish(portal_state_t state, bool scanning) { (void)scanning; http_state = state; }
void portal_set_networks(const wifi_ap_record_t *records, unsigned count) { (void)records; (void)count; }
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config)
{ assert(config->num_of_servers == 3 && !config->wait_for_sync); sync_callback = config->sync_cb; sntp = true; return ESP_OK; }
void esp_netif_sntp_deinit(void) { sntp = false; }
time_t test_time(time_t *out) { if (out) *out = wall; return wall; }

static void reset(bool configured)
{
    if (events) { wifi_cancel(1); vQueueDelete(events); }
    events = NULL; phase = OFF; view = (wifi_view_t){0};
    next_attempt = 0; saved = pending = (wifi_credentials_t){0};
    have_saved = configured; persisted = (wifi_credentials_t){.ssid = "OldNetwork", .password = "oldsecret"};
    init_count = stop_count = deinit_count = connect_count = 0;
    start_error = commit_error = stop_error = http_stop_error = ESP_OK;
    have_command = false; wall = 0; clock_init();
    assert(wifi_init() == ESP_OK);
}
static void emit(event_kind_t kind, int64_t now)
{
    radio_event_t event = {.kind = kind, .generation = generation, .reason = 15};
    assert(xQueueSend(events, &event, 0)); wifi_poll(now);
}
static void submit(int64_t now)
{
    request = (portal_command_t){.kind = PORTAL_CONNECT, .credentials = {.ssid = "NewNetwork", .password = "newsecret"}};
    have_command = true; wifi_poll(now); assert(phase == CONNECTING);
}
static void synchronized_at(int64_t now)
{
    wall = 1800000000;
    struct timeval tv = {.tv_sec = wall}; assert(sntp); sync_callback(&tv); wifi_poll(now);
}
static void off(void) { assert(phase == OFF && !rf && !view.active && !http && !sntp && !initialized && !station && !access_point); }

int main(void)
{
    reset(false);
    wifi_poll(100); wifi_poll(RETRY_US * 3); off(); assert(init_count == 0);
    clock_view_t clock; clock_get_view(100, &clock); assert(!clock.valid && !clock.text[0]);
    wifi_sync_time(RETRY_US * 3 + 100); off(); assert(init_count == 0 && next_attempt == 0);

    wifi_open_portal(100); assert(rf && http && view.portal && !view.connected);
    assert(!strcmp(view.hotspot, "Solar-0101") && view.seconds_remaining == 300);
    submit(200); assert(!have_saved);
    emit(DISCONNECTED, 300); assert(phase == PORTAL && http && !have_saved);
    submit(400); emit(ASSOCIATED, 500); assert(view.connected && view.portal);
    emit(GOT_IP, 600); assert(sntp && have_saved && !view.portal && !strcmp(persisted.ssid, "NewNetwork"));
    synchronized_at(3000600); off(); assert(stop_count == 1 && deinit_count == 1);
    clock_get_view(3000600, &clock); assert(clock.valid && clock.feedback == CLOCK_SYNCED && strlen(clock.text) == 5);
    clock_get_view(5000599, &clock); assert(clock.feedback == CLOCK_SYNCED);
    clock_get_view(5000600, &clock); assert(clock.feedback == CLOCK_NORMAL);
    int64_t due = next_attempt;
    assert(due == 3000600 + RETRY_US);
    wall += 3600; wifi_poll(due - 1); off(); assert(connect_count == 2);
    wifi_poll(due); assert(rf && !http && phase == CONNECTING && connect_count == 3);
    emit(GOT_IP, due + 100); synchronized_at(due + 200); off();
    clock_get_view(due + 200, &clock); assert(clock.feedback == CLOCK_SYNCED); // Same epoch still flashes.

    int64_t race_due = next_attempt;
    wifi_poll(race_due); emit(GOT_IP, race_due + 100);
    struct timeval tv = {.tv_sec = wall}; sync_callback(&tv);
    emit(DISCONNECTED, race_due + 200); off();
    clock_get_view(race_due + 200, &clock); assert(clock.feedback == CLOCK_SYNCED);

    reset(false); wifi_open_portal(100); submit(200); emit(GOT_IP, 300);
    wifi_poll(200 + ATTEMPT_US - 1); assert(rf);
    wifi_poll(200 + ATTEMPT_US); off(); assert(view.configured && next_attempt == 200 + ATTEMPT_US + RETRY_US);
    clock_get_view(200 + ATTEMPT_US, &clock); assert(!clock.valid && clock.feedback == CLOCK_FAILED);
    wifi_poll(next_attempt); assert(rf && phase == CONNECTING); wifi_cancel(next_attempt + 1); off();

    reset(true); wifi_poll(100); assert(rf && !http);
    emit(DISCONNECTED, 200); off(); assert(view.configured && next_attempt == 200 + RETRY_US);
    assert(!strcmp(persisted.ssid, "OldNetwork"));

    reset(false); wifi_open_portal(100); wifi_poll(100 + PORTAL_US); off(); assert(next_attempt == 0);
    wifi_open_portal(200 + PORTAL_US); wifi_cancel(201 + PORTAL_US); off();

    reset(false); wifi_open_portal(100); submit(200); wifi_poll(200 + ATTEMPT_US);
    assert(phase == PORTAL && http && http_state == PORTAL_CONNECT_FAILED && !view.configured);
    assert(portal_until == 100 + PORTAL_US); wifi_cancel(200 + ATTEMPT_US + 1); off();

    reset(true); wifi_open_portal(100); submit(200); commit_error = ESP_FAIL; emit(GOT_IP, 300);
    assert(http_state == PORTAL_SAVE_FAILED && !sntp); wifi_poll(2000300); off();
    assert(!strcmp(persisted.ssid, "OldNetwork"));

    reset(true); start_error = ESP_FAIL; wifi_poll(100); off(); assert(deinit_count == 1);
    reset(true); wifi_poll(100); stop_error = ESP_FAIL; emit(DISCONNECTED, 200);
    assert(phase == STOPPING && initialized && rf); stop_error = ESP_OK; wifi_poll(1000200); off();

    reset(false); wifi_open_portal(100); http_stop_error = ESP_FAIL; wifi_cancel(200);
    assert(phase == STOPPING && !rf && http); http_stop_error = ESP_OK; wifi_poll(1000200); off();

    reset(false); wifi_open_portal(100);
    radio_event_t stale = {.kind = GOT_IP, .generation = generation - 1};
    assert(xQueueSend(events, &stale, 0)); wifi_poll(200); assert(!view.connected && !have_saved);
    for (int i = 0; i < 17; i++) event_callback((void *)(uintptr_t)generation, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
    wifi_poll(300); off();

    reset(true); wifi_poll(100); emit(GOT_IP, 200); synchronized_at(300); off();
    int64_t old_due = next_attempt;
    wifi_sync_time(1000000);
    assert(phase == CONNECTING && view.active && rf && !http && !access_point && !next_attempt);
    assert(connect_count == 2 && !strcmp(persisted.ssid, "OldNetwork"));
    wifi_sync_time(1000001); assert(connect_count == 2); // No concurrent manual requests.
    emit(GOT_IP, 1000100); synchronized_at(1000200); off();
    assert(next_attempt == 1000200 + RETRY_US && next_attempt > old_due);
    clock_get_view(1000200, &clock); assert(clock.feedback == CLOCK_SYNCED);
    wifi_poll(old_due); off(); assert(connect_count == 2);
    int64_t manual_at = old_due + 100;
    wifi_sync_time(manual_at); assert(view.active && !http && !next_attempt);
    wifi_poll(manual_at + ATTEMPT_US); off();
    assert(next_attempt == manual_at + ATTEMPT_US + RETRY_US);
    assert(!strcmp(persisted.ssid, "OldNetwork"));
    clock_get_view(manual_at + ATTEMPT_US, &clock); assert(clock.valid && clock.feedback == CLOCK_FAILED);

    reset(true); start_error = ESP_FAIL; wifi_sync_time(100); off();
    assert(next_attempt == 100 + RETRY_US && !strcmp(persisted.ssid, "OldNetwork"));

    reset(true); wifi_open_portal(100); unsigned before = connect_count;
    wifi_sync_time(200); assert(phase == PORTAL && connect_count == before);
    wifi_cancel(300); off();

    vQueueDelete(events); events = NULL;
    puts("PASS: radio-off boot, provisioning, manual STA sync, 12h reset on success/failure, 60s/5min deadlines, credentials, SNTP feedback and cleanup");
}
