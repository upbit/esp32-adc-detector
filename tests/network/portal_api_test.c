#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network/portal.c"

const unsigned char page_start[] asm("_binary_portal_html_start") = "page";
const unsigned char page_end[] asm("_binary_portal_html_end") = "";
static portal_command_t queued;
static bool sent, full;
static int64_t monotonic, tick = 100;

QueueHandle_t xQueueCreate(unsigned count, unsigned size) { (void)count; (void)size; return (void *)1; }
int xQueueSend(QueueHandle_t queue, const void *item, unsigned timeout)
{ (void)queue; (void)timeout; if (full) return 0; memcpy(&queued, item, sizeof(queued)); sent = true; return pdTRUE; }
int xQueueReceive(QueueHandle_t queue, void *item, unsigned timeout)
{ (void)queue; (void)timeout; if (!sent) return 0; memcpy(item, &queued, sizeof(queued)); sent = false; return pdTRUE; }
void xQueueReset(QueueHandle_t queue) { (void)queue; sent = false; }
void vQueueDelete(QueueHandle_t queue) { (void)queue; }
int64_t esp_timer_get_time(void) { monotonic += tick; return monotonic; }
esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config)
{ assert(config->recv_wait_timeout == 1 && config->send_wait_timeout == 1); *handle = (void *)1; return ESP_OK; }
esp_err_t httpd_stop(httpd_handle_t handle) { (void)handle; return ESP_OK; }
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *route) { (void)handle; (void)route; return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type) { (void)req; (void)type; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *name, const char *value) { (void)req; (void)name; (void)value; return ESP_OK; }
esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status) { req->status = atoi(status); return ESP_OK; }
esp_err_t httpd_resp_send(httpd_req_t *req, const char *body, int length)
{ if (length < 0) length = strlen(body); assert((unsigned)length < sizeof(req->reply)); memcpy(req->reply, body, length); req->reply[length] = 0; return ESP_OK; }
esp_err_t httpd_resp_send_err(httpd_req_t *req, int status, const char *message)
{ req->status = status; return httpd_resp_send(req, message, -1); }
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *name, char *value, size_t size)
{ (void)name; if (!req->type || strlen(req->type) >= size) return ESP_FAIL; strcpy(value, req->type); return ESP_OK; }
int httpd_req_recv(httpd_req_t *req, char *buffer, size_t size)
{ if (size > req->fragment) size = req->fragment; memcpy(buffer, req->body + req->offset, size); req->offset += size; return size; }

static void config_case(const char *body, bool valid)
{
    httpd_req_t req = {.body = body, .content_len = strlen(body), .type = "application/json", .fragment = 3, .status = 200};
    sent = false; assert(configure(&req) == ESP_OK);
    assert(sent == valid); assert(req.status == (valid ? 200 : 400));
}

int main(void)
{
    assert(portal_start() == ESP_OK);
    config_case("{\"ssid\":\"Home\",\"password\":\"password\"}", true);
    assert(!strcmp(queued.credentials.ssid, "Home") && !strcmp(queued.credentials.password, "password"));
    config_case("{\"ssid\":\"Guest\",\"password\":\"\"}", true);
    config_case("{\"ssid\":\"一二三四五六七八九十\",\"password\":\"password\"}", true);
    config_case("{\"ssid\":\"一二三四五六七八九十一\",\"password\":\"password\"}", false);
    config_case("{\"ssid\":\"\",\"password\":\"password\"}", false);
    config_case("{\"ssid\":\"Home\",\"password\":\"short\"}", false);
    config_case("{\"ssid\":123,\"password\":\"password\"}", false);
    config_case("{\"ssid\":\"Home\"}", false);
    config_case("null", false);
    config_case("{\"ssid\":\"Home\",\"password\":\"password\"}garbage", false);
    config_case("{\"ssid\":\"Home\",\"password\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}", true);
    config_case("{\"ssid\":\"Home\",\"password\":\"g123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}", false);

    httpd_req_t req = {.body = "{}", .content_len = 2, .type = "text/plain", .fragment = 2, .status = 200};
    sent = false; configure(&req); assert(req.status == 400 && !sent);
    req.content_len = 513; req.type = "application/json"; configure(&req); assert(req.status == 400);
    full = true; req = (httpd_req_t){0}; request_scan(&req); assert(req.status == 503); full = false;
    request_scan(&req); assert(queued.kind == PORTAL_SCAN);

    wifi_ap_record_t records[4] = {
        {.ssid = "Home", .rssi = -45, .authmode = WIFI_AUTH_WPA2_PSK},
        {.ssid = "Home", .rssi = -65, .authmode = WIFI_AUTH_WPA2_PSK},
        {.ssid = "Guest", .rssi = -70, .authmode = WIFI_AUTH_OPEN},
        {0},
    };
    portal_set_networks(records, 4); list_networks(&req);
    cJSON *json = cJSON_Parse(req.reply), *list = cJSON_GetObjectItem(json, "networks");
    assert(cJSON_GetArraySize(list) == 2);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(cJSON_GetArrayItem(list, 1), "secure"))); cJSON_Delete(json);
    portal_publish(PORTAL_TIME_FAILED, false); status(&req); assert(strstr(req.reply, "time_failed"));

    tick = 500000; sent = false;
    req = (httpd_req_t){.body = "{\"ssid\":\"Home\",\"password\":\"password\"}", .content_len = 37,
        .type = "application/json", .fragment = 1};
    assert(configure(&req) == ESP_FAIL && !sent);
    assert(portal_stop() == ESP_OK && !commands && !server);
    puts("PASS: real HTTP handlers, JSON validation, UTF-8 byte limits, open/PSK networks, queue pressure, duplicate scans and slow-request bound");
}
