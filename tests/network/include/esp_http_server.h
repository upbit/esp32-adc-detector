#pragma once
#include "sdk.h"
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTPD_RESP_USE_STRLEN -1
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
typedef void *httpd_handle_t;
typedef struct {
    size_t content_len, offset, fragment;
    const char *body, *type;
    int status;
    char reply[8192];
} httpd_req_t;
typedef struct { unsigned stack_size, recv_wait_timeout, send_wait_timeout; bool lru_purge_enable; } httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
typedef struct { const char *uri; int method; esp_err_t (*handler)(httpd_req_t *); } httpd_uri_t;
esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config);
esp_err_t httpd_stop(httpd_handle_t handle);
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *route);
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *name, const char *value);
esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status);
esp_err_t httpd_resp_send(httpd_req_t *req, const char *body, int length);
esp_err_t httpd_resp_send_err(httpd_req_t *req, int status, const char *message);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *name, char *value, size_t size);
int httpd_req_recv(httpd_req_t *req, char *buffer, size_t size);
