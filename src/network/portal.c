#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "portal.h"

extern const unsigned char page_start[] asm("_binary_portal_html_start");
extern const unsigned char page_end[] asm("_binary_portal_html_end");

static httpd_handle_t server;
static QueueHandle_t commands;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static portal_state_t state;
static bool scanning;
static wifi_ap_record_t networks[PORTAL_NETWORKS_MAX];
static unsigned network_count;

static esp_err_t json_response(httpd_req_t *req, cJSON *json)
{
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!text) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    free(text);
    return err;
}

static esp_err_t page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)page_start, page_end - page_start);
}

static esp_err_t status(httpd_req_t *req)
{
    static const char *names[] = {"ready", "connecting", "connected", "synced", "time_failed",
        "connect_failed", "save_failed", "scan_failed"};
    portENTER_CRITICAL(&lock);
    portal_state_t current = state;
    bool active = scanning;
    portEXIT_CRITICAL(&lock);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "state", names[current]);
    cJSON_AddBoolToObject(json, "scanning", active);
    return json_response(req, json);
}

static esp_err_t list_networks(httpd_req_t *req)
{
    wifi_ap_record_t copy[PORTAL_NETWORKS_MAX];
    portENTER_CRITICAL(&lock);
    unsigned count = network_count;
    memcpy(copy, networks, count * sizeof(*copy));
    bool active = scanning;
    portEXIT_CRITICAL(&lock);
    cJSON *json = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(json, "networks");
    cJSON_AddBoolToObject(json, "scanning", active);
    for (unsigned i = 0; i < count; i++) {
        char ssid[33];
        memcpy(ssid, copy[i].ssid, 32);
        ssid[32] = '\0';
        if (!ssid[0]) continue;
        bool duplicate = false;
        for (unsigned j = 0; j < i; j++) {
            if (memcmp(copy[i].ssid, copy[j].ssid, 32) == 0) duplicate = true;
        }
        if (duplicate) continue;
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "ssid", ssid);
        cJSON_AddNumberToObject(entry, "rssi", copy[i].rssi);
        cJSON_AddBoolToObject(entry, "secure", copy[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(list, entry);
    }
    return json_response(req, json);
}

static esp_err_t request_scan(httpd_req_t *req)
{
    portal_command_t command = {.kind = PORTAL_SCAN};
    if (xQueueSend(commands, &command, 0) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, "Busy", HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(req, "{}", 2);
}

static esp_err_t configure(httpd_req_t *req)
{
    char type[48];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strncmp(type, "application/json", 16) || (type[16] && type[16] != ';'))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON content type required");
    if (req->content_len <= 0 || req->content_len > 512)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request length");
    char body[513];
    unsigned received = 0;
    int64_t receive_until = esp_timer_get_time() + 1000000;
    while (received < req->content_len) {
        if (esp_timer_get_time() >= receive_until) return ESP_FAIL;
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) return ESP_FAIL; // Bound by the HTTP server receive timeout.
        received += count;
    }
    body[received] = '\0';
    cJSON *json = cJSON_ParseWithLengthOpts(body, received + 1, NULL, true);
    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
    portal_command_t command = {.kind = PORTAL_CONNECT};
    bool okay = cJSON_IsString(ssid) && cJSON_IsString(password);
    if (okay) {
        size_t s = strlen(ssid->valuestring), p = strlen(password->valuestring);
        okay = s > 0 && s <= 32 && (p == 0 || (p >= 8 && p <= 63));
        if (p == 64) {
            okay = s > 0 && s <= 32 && strspn(password->valuestring, "0123456789abcdefABCDEF") == 64;
        }
        if (okay) {
            memcpy(command.credentials.ssid, ssid->valuestring, s);
            memcpy(command.credentials.password, password->valuestring, p);
        }
    }
    cJSON_Delete(json);
    memset(body, 0, sizeof(body));
    if (!okay) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID or password");
    BaseType_t sent = xQueueSend(commands, &command, 0);
    memset(&command, 0, sizeof(command));
    if (sent != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, "Busy", HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(req, "{}", 2);
}

esp_err_t portal_start(void)
{
    commands = xQueueCreate(4, sizeof(portal_command_t));
    if (!commands) return ESP_ERR_NO_MEM;
    network_count = 0;
    portal_publish(PORTAL_READY, false);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 1;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) { portal_stop(); return err; }
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status},
        {.uri = "/api/networks", .method = HTTP_GET, .handler = list_networks},
        {.uri = "/api/scan", .method = HTTP_POST, .handler = request_scan},
        {.uri = "/api/config", .method = HTTP_POST, .handler = configure},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(*routes); i++) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) { portal_stop(); return err; }
    }
    return ESP_OK;
}

esp_err_t portal_stop(void)
{
    if (server) {
        esp_err_t err = httpd_stop(server);
        if (err != ESP_OK) return err;
    }
    server = NULL;
    if (commands) { xQueueReset(commands); vQueueDelete(commands); }
    commands = NULL;
    return ESP_OK;
}

bool portal_take_command(portal_command_t *command)
{
    return commands && xQueueReceive(commands, command, 0) == pdTRUE;
}

void portal_publish(portal_state_t next, bool active)
{
    portENTER_CRITICAL(&lock);
    state = next;
    scanning = active;
    portEXIT_CRITICAL(&lock);
}

void portal_set_networks(const wifi_ap_record_t *records, unsigned count)
{
    if (count > PORTAL_NETWORKS_MAX) count = PORTAL_NETWORKS_MAX;
    portENTER_CRITICAL(&lock);
    network_count = count;
    if (count) memcpy(networks, records, count * sizeof(*records));
    scanning = false;
    portEXIT_CRITICAL(&lock);
}
